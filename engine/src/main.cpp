#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "api/ApiServer.h"
#include "compiler/EditCompiler.h"
#include "effects/BuiltinEffects.h"
#include "library/SampleLibrary.h"
#include "library/StarterPack.h"
#include "listening/Listening.h"
#include "listening/Maps.h"
#include "live/LiveEngine.h"
#include "plugins/PluginCatalog.h"
#include "render/Render.h"
#include "session/RecentProjects.h"

#include <nlohmann/json.hpp>
#include <tracktion_engine/tracktion_engine.h>

#include <csignal>
#include <fstream>
#include <optional>
#include <signal.h>
#include <iostream>

namespace
{
    constexpr const char* usage = R"(GrooveKit engine

Usage:
  groovekit-engine render <song.json> -o <out.wav> [options]
      --sample-rate <hz>     default 48000
      --bit-depth <bits>     16, 24 or 32 (default 24)
      --start <ticks>        default 0
      --end <ticks>          default: end of the last clip
      --tail <seconds>       extra time after the end (default 0)
  groovekit-engine serve <project-folder> [--port <port>] [--library <index.json>] [--no-audio 1]
                         [--parent-pid <pid>]
      Opens (or creates) the project and serves the JSON-RPC API on ws://127.0.0.1:<port>
      (default 7878) until interrupted. The sample library index defaults to
      ~/Library/Application Support/GrooveKit/library.json; with no folders set, ~/Music/Samples
      is added. Library folders are rescanned in the background on start.
  groovekit-engine validate <song.json>
  groovekit-engine upgrade <song.json>
      Rewrites a song made by an older GrooveKit in the current format.
  groovekit-engine analyze <file.wav>
  groovekit-engine effects

Add --verbose to see engine log messages.
)";

    /** Engine log output goes to stderr, and only with --verbose, so stdout stays machine-readable. */
    struct StderrLogger : juce::Logger
    {
        bool enabled = false;

        void logMessage (const juce::String& message) override
        {
            if (enabled)
                std::cerr << message << "\n";
        }
    };

    struct Arguments
    {
        std::vector<std::string> positional;
        std::map<std::string, std::string> options;
        bool verbose = false;

        std::optional<std::string> get (const std::string& name) const
        {
            auto it = options.find (name);
            return it != options.end() ? std::optional (it->second) : std::nullopt;
        }
    };

    Arguments parseArguments (int argc, char* argv[])
    {
        Arguments args;

        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];

            if (a == "--verbose")
                args.verbose = true;
            else if (a.starts_with ("-") && i + 1 < argc)
                args.options[a] = argv[++i];
            else if (a.starts_with ("-"))
                throw std::runtime_error ("Missing value for " + a);
            else
                args.positional.push_back (a);
        }

        return args;
    }

    int validate (const std::filesystem::path& song)
    {
        auto project = gk::model::loadProject (song);
        auto issues = gk::model::validate (project);

        for (auto& i : issues)
            std::cout << i.path << ": " << i.message << "\n";

        if (! issues.empty())
            return 1;

        std::cout << "OK\n";
        return 0;
    }

    /** Rewrites a song from an older GrooveKit in the current schema. */
    int upgrade (const std::filesystem::path& song)
    {
        std::ifstream in (song);

        if (! in)
            throw std::runtime_error ("Can't open " + song.string());

        auto json = gk::model::Json::parse (in);
        in.close();

        if (! gk::model::migrateProject (json))
        {
            std::cout << "Already up to date\n";
            return 0;
        }

        gk::model::parseProject (json);
        std::ofstream (song) << json.dump (2) << "\n";
        std::cout << "Upgraded " << song.string() << "\n";
        return 0;
    }

    std::atomic<bool> stopRequested { false };

    /** Serving engines register here, one file per process, so clients (the app, the MCP server)
        find the engine serving a project, and a second engine never opens a project one serves. */
    std::filesystem::path engineRegistry()
    {
        return gk::EngineContext::getAppDataFolder() / "engines";
    }

    /** The registration of a live engine serving the project. Removes stale ones on the way. */
    std::optional<nlohmann::json> findServingEngine (const std::filesystem::path& project)
    {
        std::error_code ec;

        for (auto& entry : std::filesystem::directory_iterator (engineRegistry(), ec))
        {
            try
            {
                std::ifstream in (entry.path());
                auto info = nlohmann::json::parse (in);
                const auto pid = info.value ("pid", 0);

                if (pid <= 0 || (kill (pid, 0) != 0 && errno == ESRCH))
                {
                    in.close();
                    std::filesystem::remove (entry.path(), ec);
                    continue;
                }

                if (std::filesystem::weakly_canonical (info.value ("project", std::string())) == std::filesystem::weakly_canonical (project))
                    return info;
            }
            catch (const std::exception&)
            {
            }
        }

        return {};
    }

    int serve (const Arguments& args)
    {
        if (args.positional.size() != 2)
            throw std::runtime_error ("serve needs a project folder");

        const int port = std::stoi (args.get ("--port").value_or ("7878"));
        const auto projectDir = std::filesystem::absolute (args.positional[1]);

        // Two engines writing one project would overwrite each other's changes.
        if (auto other = findServingEngine (projectDir))
            throw std::runtime_error (projectDir.string() + " is already open in the engine at " + other->value ("url", std::string())
                                      + " (process " + std::to_string (other->value ("pid", 0)) + ")");

        gk::EngineContext context ({ .outOfProcessPluginScanning = true });
        gk::PluginCatalog plugins (context.getEngine(), gk::PluginCatalog::getDefaultListFile());
        gk::library::SampleLibrary library (args.get ("--library").value_or (gk::library::SampleLibrary::getDefaultIndexFile().string()));

        // With the user's own index, start with ~/Music/Samples until they pick folders.
        if (! args.get ("--library"))
            library.addDefaultFolderIfNone();

        // Catch up on files added or changed since last time, unless the user scans by hand.
        if (library.getAutoScan())
            library.startScan();

        // The starter samples, downloaded on request (library.downloadStarter). With their own
        // library index (tests), the pack goes beside it rather than into ~/Music/Samples.
        gk::library::StarterPack starter (library, args.get ("--library")
            ? std::filesystem::path (*args.get ("--library")).replace_extension (".starter") / "GrooveKit Starter" : std::filesystem::path());

        // Hearing what the samples sound like, once the listening models are downloaded. With
        // their own library index (tests), the sound index, styles, maps and verdicts go beside it.
        gk::listening::Listening::Files listeningFiles;
        listeningFiles.maps = gk::listening::MapStore::getDefaultFolder();
        listeningFiles.kicks = gk::listening::KickIndex::getDefaultFile();

        if (auto index = args.get ("--library"))
        {
            const std::filesystem::path base (*index);
            listeningFiles.soundIndex = std::filesystem::path (base).replace_extension (".sounds.bin");
            listeningFiles.styles = std::filesystem::path (base).replace_extension (".styles");
            listeningFiles.taste = std::filesystem::path (base).replace_extension (".taste.json");
            listeningFiles.maps = std::filesystem::path (base).replace_extension (".maps");
            listeningFiles.kicks = std::filesystem::path (base).replace_extension (".kicks.json");
        }

        gk::listening::Listening listening (library, listeningFiles);
        listening.startIndexing();

        const auto pid = static_cast<int> (getpid());
        const auto registration = engineRegistry() / (std::to_string (pid) + ".json");
        const auto recents = gk::EngineContext::getAppDataFolder() / "recent.json";
        const bool audio = ! args.get ("--no-audio");

        auto registerAs = [&] (const std::filesystem::path& project)
        {
            std::filesystem::create_directories (engineRegistry());
            std::ofstream out (registration);
            out << nlohmann::json { { "url", "ws://127.0.0.1:" + std::to_string (port) }, { "port", port }, { "pid", pid },
                                    { "project", project.string() } }.dump (2);
        };

        // What belongs to the open project; project.open and project.new replace it.
        struct Workspace
        {
            std::unique_ptr<gk::Session> session;
            std::unique_ptr<gk::LiveEngine> live;
            std::unique_ptr<gk::ApiHandler> handler;
        };

        Workspace workspace;
        std::unique_ptr<gk::ApiServer> server;
        std::function<void (const std::filesystem::path&)> openProject;

        auto open = [&] (const std::filesystem::path& dir)
        {
            Workspace w;
            w.session = gk::Session::open (dir);
            w.live = std::make_unique<gk::LiveEngine> (context, *w.session);

            if (audio)
                w.live->openDefaultAudioDevice();

            w.handler = std::make_unique<gk::ApiHandler> (*w.session, context, &library, w.live.get(), &plugins, &listening, &starter);
            w.handler->onOpenProject = openProject;
            return w;
        };

        openProject = [&] (const std::filesystem::path& dir)
        {
            if (auto other = findServingEngine (dir); other && other->value ("pid", 0) != pid)
                throw std::runtime_error (dir.string() + " is already open in the engine at " + other->value ("url", std::string())
                                          + " (process " + std::to_string (other->value ("pid", 0)) + ")");

            // After the response to project.open has gone out, since this replaces the handler sending it.
            juce::MessageManager::callAsync ([&, dir]
            {
                const auto previous = *workspace.session->getProjectDir();

                // One project's Edit at a time on the audio device; each part uses the ones before it.
                workspace.handler.reset();
                workspace.live.reset();
                workspace.session.reset();

                try
                {
                    workspace = open (dir);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "error: couldn't open " << dir.string() << ": " << e.what() << std::endl;

                    try
                    {
                        workspace = open (previous);
                    }
                    catch (const std::exception& again)
                    {
                        // With no project open there's nothing to serve: stop, rather than let the
                        // exception out of the message loop and end the process without a word.
                        std::cerr << "error: couldn't reopen " << previous.string() << " either: " << again.what() << std::endl;
                        server->detachHandler();
                        stopRequested = true;
                        return;
                    }
                }

                server->setHandler (*workspace.handler);
                registerAs (*workspace.session->getProjectDir());
                gk::recent::add (recents, *workspace.session->getProjectDir());
            });
        };

        workspace = open (projectDir);
        server = std::make_unique<gk::ApiServer> (*workspace.handler, port);
        server->start();

        std::signal (SIGINT, [] (int) { stopRequested = true; });
        std::signal (SIGTERM, [] (int) { stopRequested = true; });

        std::cout << "Serving " << args.positional[1] << " on ws://127.0.0.1:" << port << std::endl;

        registerAs (*workspace.session->getProjectDir());
        gk::recent::add (recents, *workspace.session->getProjectDir());

        // From before engines registered one file each.
        std::error_code ec;
        std::filesystem::remove (gk::EngineContext::getAppDataFolder() / "engine.json", ec);

        // With --parent-pid (the MCP server passes its own), quit when that process is gone, so a
        // client that dies without cleaning up doesn't leave an engine behind.
        const auto parent = args.get ("--parent-pid");
        const auto parentPid = parent ? std::stoi (*parent) : 0;

        // runDispatchLoop() needs an NSApplication on macOS; a console process pumps messages itself.
        while (! stopRequested)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

            if (parentPid > 0 && kill (parentPid, 0) != 0 && errno == ESRCH)
                break;
        }

        server->stop();
        server.reset();

        std::filesystem::remove (registration, ec);
        return 0;
    }

    int render (const Arguments& args)
    {
        if (args.positional.size() != 2 || ! args.get ("-o"))
            throw std::runtime_error ("render needs a song file and -o <out.wav>");

        const std::filesystem::path song = args.positional[1];
        const std::filesystem::path output = *args.get ("-o");

        gk::RenderOptions options;

        if (auto v = args.get ("--sample-rate"))   options.sampleRate = std::stod (*v);
        if (auto v = args.get ("--bit-depth"))     options.bitDepth = std::stoi (*v);
        if (auto v = args.get ("--start"))         options.start = std::stoll (*v);
        if (auto v = args.get ("--end"))           options.end = std::stoll (*v);
        if (auto v = args.get ("--tail"))          options.tailSeconds = std::stod (*v);

        auto project = gk::model::loadProject (song);

        gk::EngineContext context;
        gk::PluginCatalog plugins (context.getEngine(), gk::PluginCatalog::getDefaultListFile());
        auto compiled = gk::compileProject (context.getEngine(), project,
                                            { std::filesystem::absolute (song).parent_path(), tracktion::Edit::forRendering });
        gk::renderToWav (compiled, project, output, options);

        std::cout << output.string() << "\n";
        return 0;
    }
}

int main (int argc, char* argv[])
{
    // A copy of this executable scans plugins for the engine; see EngineContext.
    gk::EngineContext::runPluginScanChildIfRequested (argc, argv);

    StderrLogger logger;
    juce::Logger::setCurrentLogger (&logger);

    int result = 1;

    try
    {
        auto args = parseArguments (argc, argv);
        logger.enabled = args.verbose;
        const auto command = args.positional.empty() ? std::string() : args.positional[0];

        if (command == "render")
        {
            result = render (args);
        }
        else if (command == "serve")
        {
            result = serve (args);
        }
        else if (command == "validate" && args.positional.size() == 2)
        {
            result = validate (args.positional[1]);
        }
        else if (command == "upgrade" && args.positional.size() == 2)
        {
            result = upgrade (args.positional[1]);
        }
        else if (command == "analyze" && args.positional.size() == 2)
        {
            juce::ScopedJuceInitialiser_GUI juce;
            std::cout << gk::analysis::analyse (gk::analysis::loadAudio (args.positional[1]), true).dump (2) << "\n";
            result = 0;
        }
        else if (command == "effects")
        {
            std::cout << gk::effects::catalogueToJson().dump (2) << "\n";
            result = 0;
        }
        else
        {
            std::cerr << usage;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
    }

    juce::Logger::setCurrentLogger (nullptr);
    return result;
}
