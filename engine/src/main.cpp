#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "api/ApiServer.h"
#include "compiler/EditCompiler.h"
#include "effects/BuiltinEffects.h"
#include "library/SampleLibrary.h"
#include "render/Render.h"

#include <tracktion_engine/tracktion_engine.h>

#include <csignal>
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
  groovekit-engine serve <project-folder> [--port <port>] [--library <index.json>]
      Opens (or creates) the project and serves the JSON-RPC API on ws://127.0.0.1:<port>
      (default 7878) until interrupted. The sample library index defaults to
      ~/Library/Application Support/GrooveKit/library.json.
  groovekit-engine validate <song.json>
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

    std::atomic<bool> stopRequested { false };

    int serve (const Arguments& args)
    {
        if (args.positional.size() != 2)
            throw std::runtime_error ("serve needs a project folder");

        const int port = std::stoi (args.get ("--port").value_or ("7878"));

        gk::EngineContext context;
        auto session = gk::Session::open (std::filesystem::absolute (args.positional[1]));
        gk::library::SampleLibrary library (args.get ("--library").value_or (gk::library::SampleLibrary::getDefaultIndexFile().string()));
        gk::ApiHandler handler (*session, context, &library);
        gk::ApiServer server (handler, port);
        server.start();

        std::signal (SIGINT, [] (int) { stopRequested = true; });
        std::signal (SIGTERM, [] (int) { stopRequested = true; });

        std::cout << "Serving " << args.positional[1] << " on ws://127.0.0.1:" << port << std::endl;

        // runDispatchLoop() needs an NSApplication on macOS; a console process pumps messages itself.
        while (! stopRequested)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        server.stop();
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
        auto compiled = gk::compileProject (context.getEngine(), project,
                                            { std::filesystem::absolute (song).parent_path(), tracktion::Edit::forRendering });
        gk::renderToWav (*compiled.edit, project, output, options);

        std::cout << output.string() << "\n";
        return 0;
    }
}

int main (int argc, char* argv[])
{
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
