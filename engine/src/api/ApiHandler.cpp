#include "ApiHandler.h"

#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "analysis/MixAnalysis.h"
#include "compiler/EditCompiler.h"
#include "compiler/EditGraveyard.h"
#include "effects/BuiltinEffects.h"
#include "library/SampleLibrary.h"
#include "listening/Listening.h"
#include "listening/Maps.h"
#include "live/AudioDevices.h"
#include "live/LiveEngine.h"
#include "music/Notes.h"
#include "plugins/PluginCatalog.h"
#include "plugins/PluginWindows.h"
#include "render/Render.h"
#include "session/Mastering.h"
#include "sampler/SamplerPlugin.h"
#include "guides/Guides.h"
#include "session/AgentNotes.h"
#include "session/Chats.h"
#include "session/Presets.h"
#include "session/RecentProjects.h"
#include "session/Summary.h"
#include "voice/Transcriber.h"

#include <juce_events/juce_events.h>

namespace gk
{

using model::Json;

namespace
{
    /** Transport state as clients get it; mode "pattern" plays one pattern on a loop. */
    model::Json transportJson (const LiveEngine::TransportState& t)
    {
        model::Json j { { "playing", t.playing }, { "recording", t.recording }, { "position", t.position }, { "seconds", t.seconds },
                        { "mode", t.pattern ? "pattern" : "song" }, { "metronome", t.metronome }, { "countInBars", t.countInBars },
                        { "time", t.time }, { "outputLatencyMs", std::round (t.outputLatencyMs * 10) / 10 } };

        if (t.pattern)
            j["pattern"] = *t.pattern;

        return j;
    }

    /** JSON-RPC error codes. */
    enum ErrorCode
    {
        parseError = -32700,
        invalidRequest = -32600,
        methodNotFound = -32601,
        invalidParams = -32602,
        commandFailed = 1,
        operationFailed = 2,
    };

    struct RpcError : std::runtime_error
    {
        RpcError (int c, const std::string& m) : std::runtime_error (m), code (c) {}
        int code;
    };

    Json notification (const char* method, Json params)
    {
        return { { "jsonrpc", "2.0" }, { "method", method }, { "params", std::move (params) } };
    }

    const char* toString (Change::Kind k)
    {
        switch (k)
        {
            case Change::Kind::execute: return "execute";
            case Change::Kind::undo:    return "undo";
            case Change::Kind::redo:    return "redo";
        }

        return "";
    }

    Json headJson (const std::optional<int64_t>& head)
    {
        return head ? Json (*head) : Json();
    }

    template <typename T>
    T readParam (const Json& params, const char* key)
    {
        model::Reader reader;
        T value;

        try
        {
            reader.required (params, key, [&] (const Json& v) { readValue (reader, v, value); });
        }
        catch (const model::ReadError& e)
        {
            throw RpcError (invalidParams, std::string ("params") + e.what());
        }

        return value;
    }

    std::optional<double> optionalNumber (const Json& params, const char* key)
    {
        if (! params.contains (key) || params[key].is_null())
            return {};

        if (! params[key].is_number())
            throw RpcError (invalidParams, std::string ("params/") + key + ": expected number");

        return params[key].get<double>();
    }
}

ApiHandler::ApiHandler (Session& s, EngineContext& c, library::SampleLibrary* l, LiveEngine* le, PluginCatalog* p, listening::Listening* li)
    : session (s), context (c), library (l), live (le), plugins (p), listening (li)
{
    // Progress comes from download, indexing and analysis threads: broadcast from the message
    // thread. (A handler for another project replaces this observer.)
    if (listening != nullptr)
        listening->setObserver ([this, alive = alive] (const Json& status)
        {
            juce::MessageManager::callAsync ([this, alive, status]
            {
                if (*alive && broadcast)
                    broadcast (notification ("listening", status).dump());
            });
        });

    if (live != nullptr)
        live->onTransportChange = [this] (const LiveEngine::TransportState& t)
        {
            if (broadcast)
                broadcast (notification ("transport", transportJson (t)).dump());
        };

    if (live != nullptr)
        live->onLevels = [this] (const LiveEngine::Levels& levels)
        {
            if (! broadcast)
                return;

            auto round = [] (float v) { return std::round (v * 1000.0f) / 1000.0f; };
            Json inserts = Json::object(), channels = Json::object();

            for (auto& [id, peak] : levels.inserts)
                inserts[id] = Json::array ({ round (peak.first), round (peak.second) });

            for (auto& [id, peak] : levels.channels)
                channels[id] = round (peak);

            broadcast (notification ("levels", { { "inserts", inserts }, { "channels", channels } }).dump());
        };

    // What was done in a plugin's window goes into the project when the window closes.
    if (live != nullptr)
        PluginWindowBehaviour::onWindowClosed = [this, alive = alive] (tracktion::Plugin& plugin)
        {
            if (! *alive)
                return;

            if (const auto key = live->findPluginKey (plugin); ! key.empty())
            {
                try
                {
                    captureState (key, "ui");
                }
                catch (const std::exception& e)
                {
                    juce::Logger::writeToLog ("Couldn't save the plugin's state: " + juce::String (e.what()));
                }
            }
        };

    // The bar above plugin windows edits the project like the UI, and asks the UI for automation clips.
    if (live != nullptr)
        PluginWindowBehaviour::host = {
            .findKey = [this] (tracktion::Plugin& plugin) { return live->findPluginKey (plugin); },
            .getProject = [this] { return &session.getProject(); },
            .execute = [this] (const std::vector<model::Command>& commands, const std::string& label, const std::string& group)
            {
                try
                {
                    lastOrigin = "ui";
                    session.execute (commands, { "ui", group, label });
                }
                catch (const std::exception& e)
                {
                    juce::Logger::writeToLog ("Plugin window: " + juce::String (e.what()));
                }
            },
            .createAutomation = [this] (const model::AutomationTarget& target, double value)
            {
                if (broadcast)
                    broadcast (notification ("createAutomation", { { "target", model::toJson (target) }, { "value", value } }).dump());
            },
        };

    listenerId = session.addListener ([this] (const Change& change)
    {
        if (! broadcast)
            return;

        Json params {
            { "kind", toString (change.kind) },
            { "entry", change.entry },
            { "head", headJson (change.head) },
            { "ops", model::toJson (change.ops) },
        };

        auto& entry = session.getHistory().at (static_cast<size_t> (change.entry - 1));
        params["origin"] = change.kind == Change::Kind::execute ? entry.origin : lastOrigin;
        params["label"] = entry.label;

        broadcast (notification ("patch", std::move (params)).dump());
    });
}

ApiHandler::~ApiHandler()
{
    *alive = false;

    if (live != nullptr)
    {
        live->onTransportChange = {};
        live->onLevels = {};
        PluginWindowBehaviour::onWindowClosed = {};
        PluginWindowBehaviour::host = {};
    }

    session.removeListener (listenerId);
}

void ApiHandler::setBroadcast (Send b)
{
    broadcast = std::move (b);
}

std::string ApiHandler::getSnapshotMessage() const
{
    Json params { { "document", session.getDocument() }, { "head", headJson (session.getHead()) }, { "saved", headJson (session.getSavedHead()) } };

    if (auto dir = session.getProjectDir())
        params["project"] = dir->string();

    return notification ("snapshot", std::move (params)).dump();
}

void ApiHandler::handleMessage (const std::string& text, Client& client)
{
    Json id;
    Json response;

    try
    {
        Json request;

        try
        {
            request = Json::parse (text);
        }
        catch (const Json::parse_error& e)
        {
            throw RpcError (parseError, e.what());
        }

        if (! request.is_object() || ! request.contains ("method") || ! request["method"].is_string())
            throw RpcError (invalidRequest, "expected a JSON-RPC request");

        id = request.value ("id", Json());
        auto params = request.value ("params", Json::object());

        if (! params.is_object())
            throw RpcError (invalidParams, "params must be an object");

        auto result = call (request["method"].get<std::string>(), params, client);

        if (id.is_null())
            return; // a notification: no response

        response = { { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move (result) } };
    }
    catch (const RpcError& e)
    {
        response = { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", e.code }, { "message", e.what() } } } };
    }
    catch (const CommandError& e)
    {
        response = { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", commandFailed }, { "message", e.what() } } } };
    }
    catch (const std::exception& e)
    {
        response = { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", operationFailed }, { "message", e.what() } } } };
    }

    if (client.send)
        client.send (response.dump());
}

Json ApiHandler::call (const std::string& method, const Json& params, Client& client)
{
    if (method.starts_with ("project."))
        return callProject (method, params);

    if (method == "hello")
    {
        if (params.contains ("origin"))
            client.origin = readParam<std::string> (params, "origin");

        Json result { { "server", "groovekit-engine" }, { "protocol", 1 } };

        if (auto dir = session.getProjectDir())
            result["project"] = dir->string();

        return result;
    }

    if (method == "execute")
    {
        ExecuteOptions options;
        options.origin = client.origin;

        if (params.contains ("group"))  options.group = readParam<std::string> (params, "group");
        if (params.contains ("label"))  options.label = readParam<std::string> (params, "label");

        auto& entry = session.execute (readParam<std::vector<model::Command>> (params, "commands"), options);
        return { { "entry", entry.id }, { "label", entry.label } };
    }

    if (method == "undo" || method == "redo")
    {
        const bool wholeGroup = params.value ("wholeGroup", true);
        lastOrigin = client.origin;
        const bool changed = method == "undo" ? session.undo (wholeGroup) : session.redo (wholeGroup);
        return { { "changed", changed }, { "head", headJson (session.getHead()) } };
    }

    if (method == "get")
    {
        auto path = params.value ("path", std::string());

        try
        {
            return session.getDocument().at (Json::json_pointer (path));
        }
        catch (const Json::exception&)
        {
            throw RpcError (invalidParams, "nothing at \"" + path + "\"");
        }
    }

    if (method == "getSummary")
        return summarizeProject (session.getProject());

    if (method == "getHistory")
    {
        auto entries = Json::array();

        for (auto& e : session.getHistory())
        {
            Json j { { "id", e.id }, { "time", e.time }, { "origin", e.origin }, { "label", e.label } };

            if (e.parent)   j["parent"] = *e.parent;
            if (e.group)    j["group"] = *e.group;

            entries.push_back (std::move (j));
        }

        return { { "entries", entries }, { "head", headJson (session.getHead()) } };
    }

    if (method == "render")
        return render (params);

    if (method == "master")
    {
        MasterOptions options;
        options.execute.origin = client.origin;

        if (auto v = optionalNumber (params, "targetLufs"))     options.targetLufs = *v;
        if (auto v = optionalNumber (params, "ceilingDb"))      options.ceilingDb = *v;
        if (auto v = optionalNumber (params, "releaseMs"))      options.releaseMs = *v;
        if (auto v = optionalNumber (params, "start"))          options.render.start = static_cast<model::Ticks> (*v);
        if (auto v = optionalNumber (params, "end"))            options.render.end = static_cast<model::Ticks> (*v);
        if (params.contains ("group"))                          options.execute.group = readParam<std::string> (params, "group");

        return masterToLoudness (context.getEngine(), session, options);
    }

    if (method == "analyzeMix")
    {
        RenderOptions options;

        if (auto v = optionalNumber (params, "start"))          options.start = static_cast<model::Ticks> (*v);
        if (auto v = optionalNumber (params, "end"))            options.end = static_cast<model::Ticks> (*v);
        if (auto v = optionalNumber (params, "tailSeconds"))    options.tailSeconds = *v;

        const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
        return analysis::analyseMix (context.getEngine(), session.getProject(), { projectDir, tracktion::Edit::forRendering },
                                     options, projectDir / "renders" / "analysis");
    }

    if (method == "analyze")
    {
        auto audio = analysis::loadAudio (readParam<std::string> (params, "path"));
        return analysis::analyse (analysis::slice (audio, { optionalNumber (params, "start").value_or (0),
                                                            optionalNumber (params, "end"), {} }), true);
    }

    // The effect catalogue, with each effect's factory presets.
    if (method == "listEffects")
    {
        auto catalogue = effects::catalogueToJson();

        for (auto& [kind, info] : catalogue.items())
            if (presets::factory().contains (kind))
                info["presets"] = presets::factory()[kind];

        return catalogue;
    }

    if (method.starts_with ("voice."))
        return callVoice (method, params);

    if (method.starts_with ("melody."))
        return callMelody (method, params);

    if (method.starts_with ("listening.") || method.starts_with ("styles."))
        return callListening (method, params);

    if (method.starts_with ("maps."))
        return callMaps (method, params);

    if (method.starts_with ("chats."))
        return callChats (method, params);

    if (method.starts_with ("notes."))
        return callNotes (method, params);

    if (method.starts_with ("guides."))
        return callGuides (method, params);

    if (method.starts_with ("presets."))
    {
        const auto folder = presetsFolder.empty() ? EngineContext::getAppDataFolder() / "presets" : presetsFolder;
        const auto kind = readParam<std::string> (params, "kind");

        try
        {
            if (method == "presets.list")
                return presets::list (kind, folder);

            if (method == "presets.save")
            {
                auto preset = params.contains ("preset") ? params["preset"] : Json();
                presets::save (kind, preset, folder);
                return presets::list (kind, folder);
            }

            if (method == "presets.delete")
            {
                presets::remove (kind, readParam<std::string> (params, "name"), folder);
                return presets::list (kind, folder);
            }
        }
        catch (const std::runtime_error& e)
        {
            throw RpcError (invalidParams, e.what());
        }

        throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
    }

    if (method == "audio.peaks")
        return peaks (params);

    if (method.starts_with ("library."))
        return callLibrary (method.substr (8), params);

    if (method.starts_with ("transport.") || method.starts_with ("record.") || method.starts_with ("audio.") || method.starts_with ("midi.") || method.starts_with ("live.")
        || method.starts_with ("preview.") || method.starts_with ("mixer."))
        return callLive (method, params, client);

    if (method.starts_with ("plugins."))
        return callPlugins (method.substr (8), params, client);

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::peaks (const Json& params)
{
    auto path = std::filesystem::path (readParam<std::string> (params, "path"));

    if (path.is_relative())
        if (auto dir = session.getProjectDir())
            path = *dir / path;

    // Sampler windows ask for the wave itself: its lowest and highest value per slice, finer.
    const bool withSigns = params.value ("signed", false);
    const auto resolution = std::clamp (optionalNumber (params, "resolution").value_or (100.0), 1.0, withSigns ? 48000.0 : 1000.0);

    // A sampler layer's processing (process, punch, tail): the wave as it plays.
    model::SamplerLayer layer;
    bool processed = false;

    if (params.contains ("process") || params.contains ("punch") || params.contains ("tail"))
    {
        if (params.contains ("process"))
        {
            try
            {
                model::Reader reader;
                readValue (reader, params["process"], layer.process.emplace());
            }
            catch (const model::ReadError& e)
            {
                throw RpcError (invalidParams, std::string ("params.process") + e.what());
            }
        }

        layer.punch = optionalNumber (params, "punch").value_or (0.0);
        layer.tail = optionalNumber (params, "tail").value_or (0.0);

        processed = ! sampler::toProcessing (layer).isNeutral();
    }

    const auto key = path.string() + "@" + std::to_string (resolution) + (withSigns ? "+" : "")
                   + (processed ? sampler::toProcessing (layer).key() : "");
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time (path, ec);

    if (ec)
        throw RpcError (operationFailed, "no file \"" + path.string() + "\"");

    if (auto it = peaksCache.find (key); it != peaksCache.end() && it->second.first == modified)
        return it->second.second;

    analysis::Audio audio;
    double timeScale = 1;

    if (processed)
    {
        try
        {
            auto& cache = sampler::SampleCache::getInstance();
            const auto sample = cache.processed (cache.load (path), sampler::toProcessing (layer));
            audio.buffer = sample->audio;
            audio.sampleRate = sample->sampleRate;
            timeScale = sample->timeScale;
        }
        catch (const std::runtime_error& e)
        {
            throw RpcError (operationFailed, e.what());
        }
    }
    else
    {
        audio = analysis::loadAudio (path);
    }

    const auto& buffer = audio.buffer;
    const auto perPeak = std::max (1, static_cast<int> (std::round (audio.sampleRate / resolution)));
    auto values = Json::array(), lows = Json::array(), highs = Json::array();
    const auto rounded = [] (float v) { return std::round (std::clamp (v, -1.0f, 1.0f) * 1000.0f) / 1000.0f; };

    for (int start = 0; start < buffer.getNumSamples(); start += perPeak)
    {
        const auto n = std::min (perPeak, buffer.getNumSamples() - start);
        float peak = 0, low = 0, high = 0;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto range = buffer.findMinMax (ch, start, n);
            low = std::min (low, range.getStart());
            high = std::max (high, range.getEnd());
            peak = std::max (peak, std::max (-range.getStart(), range.getEnd()));
        }

        values.push_back (rounded (peak));

        if (withSigns)
        {
            lows.push_back (rounded (low));
            highs.push_back (rounded (high));
        }
    }

    Json result { { "durationSeconds", buffer.getNumSamples() / audio.sampleRate },
                  { "sampleRate", audio.sampleRate },
                  { "channels", buffer.getNumChannels() },
                  { "resolution", audio.sampleRate / perPeak },
                  { "timeScale", timeScale },
                  { "peaks", std::move (values) } };

    if (withSigns)
    {
        result["min"] = std::move (lows);
        result["max"] = std::move (highs);
    }

    // Waveforms are asked for again on every redraw of a song; keep a few dozen.
    if (peaksCache.size() > 64)
        peaksCache.clear();

    peaksCache[key] = { modified, result };
    return result;
}

Json ApiHandler::render (const Json& params)
{
    RenderOptions options;

    if (auto v = optionalNumber (params, "start"))          options.start = static_cast<model::Ticks> (*v);
    if (auto v = optionalNumber (params, "end"))            options.end = static_cast<model::Ticks> (*v);
    if (auto v = optionalNumber (params, "tailSeconds"))    options.tailSeconds = *v;
    if (auto v = optionalNumber (params, "sampleRate"))     options.sampleRate = *v;
    if (auto v = optionalNumber (params, "bitDepth"))       options.bitDepth = static_cast<int> (*v);

    if (options.bitDepth != 16 && options.bitDepth != 24 && options.bitDepth != 32)
        throw RpcError (invalidParams, "bitDepth must be 16, 24 or 32");

    if (options.sampleRate < 8000 || options.sampleRate > 192000)
        throw RpcError (invalidParams, "sampleRate must be between 8000 and 192000");

    const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");

    std::filesystem::path output;

    if (params.contains ("path"))
    {
        output = readParam<std::string> (params, "path");
    }
    else
    {
        std::filesystem::create_directories (projectDir / "renders");
        output = projectDir / "renders" / ("render-" + std::to_string (++renderCount) + ".wav");
    }

    auto compiled = compileProject (context.getEngine(), session.getProject(), { projectDir, tracktion::Edit::forRendering });
    renderToWav (compiled, session.getProject(), output, options);

    Json result { { "path", std::filesystem::absolute (output).string() },
                  { "analysis", analysis::analyse (analysis::loadAudio (output), true) } };

    // Stems: each insert but master on its own (soloed, so it's heard through what it feeds),
    // over the same range, next to the mix as <name>-<insert>.wav.
    if (params.value ("stems", false))
    {
        auto stems = Json::array();

        for (auto& insert : session.getProject().mixer.inserts)
        {
            if (insert.id == model::masterInsertId)
                continue;

            auto project = session.getProject();

            for (auto& i : project.mixer.inserts)
                i.solo = i.id == insert.id;

            for (auto& c : project.channels)
                c.solo = false;

            for (auto& t : project.playlist.rows)
                t.solo = false;

            auto stem = output.parent_path() / (output.stem().string() + "-" + insert.id + ".wav");
            auto stemEdit = compileProject (context.getEngine(), project, { projectDir, tracktion::Edit::forRendering });
            renderToWav (stemEdit, project, stem, options);
            stems.push_back ({ { "insert", insert.id }, { "path", std::filesystem::absolute (stem).string() } });
        }

        result["stems"] = stems;
    }

    return result;
}

Json ApiHandler::callLibrary (const std::string& method, const Json& params)
{
    if (library == nullptr)
        throw RpcError (operationFailed, "no sample library is configured");

    auto progressJson = [this]
    {
        auto p = library->getProgress();
        return Json { { "running", p.running }, { "filesFound", p.filesFound }, { "analysed", p.analysed },
                      { "unchanged", p.unchanged }, { "failed", p.failed }, { "samples", library->size() } };
    };

    auto folders = [this]
    {
        auto list = Json::array();

        for (auto& f : library->getFolders())
            list.push_back (f.string());

        return list;
    };

    auto status = [&] { return Json { { "folders", folders() }, { "progress", progressJson() }, { "autoScan", library->getAutoScan() } }; };

    if (method == "folders" || method == "status")
        return status();

    if (method == "setAutoScan")
    {
        library->setAutoScan (readParam<bool> (params, "enabled"));
        return status();
    }

    if (method == "addFolder")
    {
        library->addFolder (readParam<std::string> (params, "path"));

        if (params.value ("scan", true))
            startLibraryScan();

        return status();
    }

    if (method == "removeFolder")
    {
        library->removeFolder (readParam<std::string> (params, "path"));
        return status();
    }

    if (method == "scan")
    {
        startLibraryScan();
        return progressJson();
    }

    if (method == "search")
    {
        library::SampleQuery q;
        q.text = params.value ("query", "");
        q.limit = std::clamp (params.value ("limit", 20), 1, 200);

        if (params.contains ("category"))       q.category = readParam<std::string> (params, "category");
        if (params.contains ("loop"))           q.loop = readParam<bool> (params, "loop");
        if (params.contains ("key"))            q.key = readParam<std::string> (params, "key");

        // "song" is the song's key.
        if (auto fits = params.value ("fitsKey", std::string()); ! fits.empty())
        {
            const auto name = fits == "song" ? session.getProject().key.value_or ("") : fits;
            q.fitsKey = music::parseKey (name);

            if (! q.fitsKey)
                throw RpcError (invalidParams, fits == "song" ? "The song has no key yet (setKey)" : "\"" + fits + "\" isn't a key; name one like \"A minor\"");
        }

        if (auto v = optionalNumber (params, "tempo"))          q.tempo = v;
        if (auto v = optionalNumber (params, "minDuration"))    q.minDuration = v;
        if (auto v = optionalNumber (params, "maxDuration"))    q.maxDuration = v;

        if (listening != nullptr)
        {
            listening::Listening::SoundQuery sound;
            sound.sound = params.value ("sound", "");
            sound.like = params.value ("like", "");
            sound.style = params.value ("style", "");

            if (params.contains ("role"))
            {
                sound.role = listening::roleFromName (readParam<std::string> (params, "role"));

                if (! sound.role)
                    throw RpcError (invalidParams, "role must be kick, hats, drums, bass, music, kickHit, hatHit or percHit");
            }

            try
            {
                return listening->search (q, sound);
            }
            catch (const std::runtime_error& e)
            {
                throw RpcError (operationFailed, e.what());
            }
        }

        auto results = Json::array();

        for (auto& e : library->search (q))
            results.push_back (library::SampleLibrary::toJson (e, false, q.fitsKey));

        return { { "results", results }, { "librarySize", library->size() } };
    }

    if (method == "list")
    {
        const auto listing = library->list (params.contains ("path") ? readParam<std::string> (params, "path") : std::string());
        auto folders = Json::array(), samples = Json::array();

        for (auto& f : listing.folders)
            folders.push_back ({ { "path", f.path.string() }, { "name", f.path.filename().string() }, { "samples", f.samples } });

        for (auto& e : listing.samples)
            samples.push_back (library::SampleLibrary::toJson (e, false));

        return { { "folders", folders }, { "samples", samples } };
    }

    if (method == "describe")
    {
        // A path relative to the project folder works, like in the song; so does a bare file
        // name that one library sample has.
        const std::filesystem::path requested = readParam<std::string> (params, "path");
        auto path = requested;
        std::error_code ec;

        if (path.is_relative())
            if (auto dir = session.getProjectDir())
                path = *dir / path;

        if (! std::filesystem::is_regular_file (path, ec))
        {
            library::SampleQuery query;
            query.text = requested.stem().string();
            query.limit = 200;
            std::vector<std::filesystem::path> named;

            for (auto& e : library->search (query))
                if (std::filesystem::path (e.path).filename() == requested.filename())
                    named.push_back (e.path);

            if (named.size() != 1)
                throw RpcError (invalidParams, "No file at " + path.string()
                                + (named.empty() ? "; pass the sample's full path, as get_summary or search_samples give it"
                                                 : "; several library samples are named " + requested.filename().string() + ", pass the full path"));

            path = named.front();
        }

        auto entry = library->find (path);
        auto result = library::SampleLibrary::toJson (entry ? *entry : library::SampleLibrary::analyseFile (path), true);

        // What it sounds like in words, and how it fits a style, once GrooveKit can listen.
        if (listening != nullptr && listening->getModels().getStatus().ready && listening->getIndex().size() > 0)
        {
            const auto embedding = listening->getIndex().embedFile (path);
            result["sounds"] = listening->describe (embedding, 5);

            if (params.contains ("style"))
            {
                auto profile = listening->getStyles().get (readParam<std::string> (params, "style"));

                if (profile == nullptr)
                    throw RpcError (invalidParams, "No style named \"" + params["style"].get<std::string>() + "\" (styles.list)");

                const auto role = listening::roleOf (result.value ("category", "other"), result.value ("loop", false));
                result["fit"] = std::lround (listening->fitPercent (*profile, role, embedding));
            }
        }

        return result;
    }

    if (method == "rate")
    {
        if (listening == nullptr)
            throw RpcError (operationFailed, "listening isn't available");

        const auto path = readParam<std::string> (params, "path");
        const auto key = library->find (path) ? library->find (path)->path : path;
        std::optional<bool> fits;

        if (params.contains ("fits") && ! params["fits"].is_null())
            fits = readParam<bool> (params, "fits");

        listening->getTaste().rate (key, params.value ("style", ""), fits);
        return { { "path", key }, { "style", params.value ("style", "") }, { "fits", fits ? Json (*fits) : Json() } };
    }

    if (method == "ratings")
    {
        auto list = Json::array();

        if (listening != nullptr)
            for (auto& r : listening->getTaste().all())
                list.push_back ({ { "path", r.path }, { "style", r.style }, { "fits", r.fits } });

        return { { "ratings", list } };
    }

    throw RpcError (methodNotFound, "unknown method \"library." + method + "\"");
}

namespace
{
    /** Magnitudes in dB (full scale sine = 0) on 160 bins spaced evenly in log frequency from
        20 Hz to 20 kHz, each the loudest FFT bin within it, Hann windowed. */
    Json spectrumBins (const std::vector<float>& samples, double sampleRate)
    {
        constexpr int order = 12, size = 1 << order, numBins = 160;
        auto bins = Json::array();

        if (static_cast<int> (samples.size()) < size || sampleRate <= 0)
            return bins;

        std::vector<float> data (2 * size, 0.0f);

        for (int i = 0; i < size; ++i)
            data[static_cast<size_t> (i)] = samples[static_cast<size_t> (i)] * 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * i / (size - 1)));

        juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (data.data(), true);

        // A Hann window halves a sine's amplitude; a full-scale sine reads 0 dB.
        const auto scale = 4.0f / size;

        for (int b = 0; b < numBins; ++b)
        {
            const auto lo = 20.0 * std::pow (1000.0, static_cast<double> (b) / numBins);
            const auto hi = 20.0 * std::pow (1000.0, static_cast<double> (b + 1) / numBins);
            auto from = static_cast<int> (std::floor (lo * size / sampleRate));
            auto to = std::max (from + 1, static_cast<int> (std::ceil (hi * size / sampleRate)));
            float peak = 0;

            for (int k = std::max (1, from); k < std::min (to, size / 2); ++k)
                peak = std::max (peak, data[static_cast<size_t> (k)]);

            bins.push_back (std::round (juce::Decibels::gainToDecibels (peak * scale, -120.0f) * 10.0f) / 10.0f);
        }

        return bins;
    }
}

Json ApiHandler::callLive (const std::string& method, const Json& params, Client& client)
{
    if (live == nullptr)
        throw RpcError (operationFailed, "this engine has no live playback");

    auto state = [this] { return transportJson (live->getTransportState()); };

    if (method == "preview.play")
    {
        auto path = std::filesystem::path (readParam<std::string> (params, "path"));

        if (path.is_relative())
            if (auto dir = session.getProjectDir())
                path = *dir / path;

        try
        {
            live->getPreview().play (path, static_cast<float> (optionalNumber (params, "gainDb").value_or (-6.0)));
        }
        catch (const std::exception& e)
        {
            throw RpcError (operationFailed, e.what());
        }

        return { { "playing", true } };
    }

    // What comes out of an effect slot: its spectrum, or the waveform itself.
    if (method == "mixer.spectrum" || method == "mixer.waveform")
    {
        const auto insert = readParam<std::string> (params, "insert");
        const auto effect = readParam<std::string> (params, "effect");

        if (method == "mixer.waveform")
        {
            const auto n = static_cast<int> (optionalNumber (params, "samples").value_or (1024));
            return { { "samples", live->readEffectOutput (insert, effect, n) } };
        }

        return { { "bins", spectrumBins (live->readEffectOutput (insert, effect, 4096), live->getSampleRate()) },
                 { "fromHz", 20 }, { "toHz", 20000 } };
    }

    if (method == "mixer.meter")
        return live->readEffectMeter (readParam<std::string> (params, "insert"), readParam<std::string> (params, "effect"), params);

    if (method == "preview.stop")
    {
        live->getPreview().stop();
        return { { "playing", false } };
    }

    if (method == "transport.state")    return state();
    if (method == "transport.play")     { live->play(); return state(); }
    if (method == "transport.stop")     { live->stop(); return state(); }

    if (method == "transport.setMode")
    {
        const auto mode = readParam<std::string> (params, "mode");

        if (mode == "pattern")
            live->setPlayPattern (readParam<std::string> (params, "pattern"));
        else if (mode == "song")
            live->setPlayPattern ({});
        else
            throw RpcError (invalidParams, "mode must be \"song\" or \"pattern\"");

        return state();
    }

        if (method == "transport.setPosition")
    {
        live->setPosition (static_cast<model::Ticks> (optionalNumber (params, "position").value_or (0)));
        return state();
    }

    if (method == "transport.setLoop")
    {
        auto start = optionalNumber (params, "start");
        auto end = optionalNumber (params, "end");

        if (start && end)
            live->setLoop (std::make_pair (static_cast<model::Ticks> (*start), static_cast<model::Ticks> (*end)));
        else
            live->setLoop ({});

        return state();
    }

    if (method == "midi.devices")
    {
        auto outputs = Json::array();

        for (auto& port : live->getMidiOutputs())
            outputs.push_back ({ { "name", port.name }, { "enabled", port.enabled }, { "clock", port.sendsClock } });

        auto inputs = Json::array(), enabledInputs = Json::array();

        for (auto& name : live->getMidiInputs())
        {
            inputs.push_back (name);

            if (live->isMidiInputEnabled (name))
                enabledInputs.push_back (name);
        }

        return { { "outputs", outputs }, { "inputs", inputs }, { "enabledInputs", enabledInputs } };
    }

    if (method == "midi.setInput")
    {
        live->setMidiInputEnabled (readParam<std::string> (params, "input"), params.value ("enabled", true));
        return call ("midi.devices", Json::object(), client);
    }

    if (method == "transport.setMetronome")
    {
        if (params.contains ("enabled"))       live->setMetronome (readParam<bool> (params, "enabled"));
        if (auto bars = optionalNumber (params, "countInBars")) live->setCountInBars (static_cast<int> (*bars));
        return state();
    }

    if (method == "live.setChannel")
    {
        if (params.contains ("channel") && ! params["channel"].is_null())
            live->setLiveChannel (readParam<std::string> (params, "channel"));
        else
            live->setLiveChannel ({});

        return { { "channel", live->getLiveChannel() ? Json (*live->getLiveChannel()) : Json() } };
    }

    if (method == "live.noteOn" || method == "live.noteOff")
    {
        if (params.contains ("channel"))
            live->setLiveChannel (readParam<std::string> (params, "channel"));

        const auto key = static_cast<int> (optionalNumber (params, "key").value_or (60));

        if (method == "live.noteOn")
            live->noteOn (key, static_cast<int> (optionalNumber (params, "velocity").value_or (100)));
        else
            live->noteOff (key);

        return Json::object();
    }

    if (method == "record.notes")
    {
        LiveEngine::NoteRecordOptions options;
        options.pattern = readParam<std::string> (params, "pattern");
        options.channel = readParam<std::string> (params, "channel");
        options.quantize = static_cast<model::Ticks> (optionalNumber (params, "quantize").value_or (0));
        live->startNoteRecording (options);
        return state();
    }

    if (method == "midi.setClock")
    {
        live->setMidiClock (readParam<std::string> (params, "output"), params.value ("enabled", true));
        return call ("midi.devices", Json::object(), client);
    }

    if (method == "record.calibrate")
    {
        auto r = live->calibrateAlignment (params.value ("input", std::string ("Track 1")), params.value ("bars", 2));
        return { { "alignmentMs", std::round (r.alignmentMs * 10) / 10 }, { "spreadMs", std::round (r.spreadMs * 10) / 10 },
                 { "hits", r.hits }, { "device", r.device } };
    }

    if (method == "record.alignment")
    {
        if (auto ms = optionalNumber (params, "ms"))
            live->setAlignmentMs (*ms);

        return { { "alignmentMs", live->getAlignmentMs() } };
    }

    if (method == "record.inputs")
        return { { "inputs", live->getInputNames() } };

    if (method == "record.start")
    {
        std::vector<LiveEngine::RecordTarget> targets;

        for (auto& t : params.value ("targets", Json::array()))
        {
            if (! t.is_object() || ! t.contains ("input") || ! t.contains ("row") || ! t["row"].is_number_integer()
                 || t["row"].get<int>() < 0 || t["row"].get<int>() > 999)
                throw RpcError (invalidParams, "each target needs input and row (an arrangement row number, 0-999), and optionally insert");

            targets.push_back ({ t["input"].get<std::string>(), t.value ("insert", std::string (model::masterInsertId)),
                                 t["row"].get<int>() });
        }

        LiveEngine::RecordOptions options;
        options.dropSilent = params.value ("dropSilent", false);
        options.normalizeLufs = optionalNumber (params, "normalizeLufs");

        if (auto bars = optionalNumber (params, "bars"))
            options.length = static_cast<model::Ticks> (*bars * static_cast<double> (model::ticksPerBar (session.getProject().timeSignature)));

        if (auto from = optionalNumber (params, "fromBar"))
            live->setPosition (static_cast<model::Ticks> ((*from - 1) * static_cast<double> (model::ticksPerBar (session.getProject().timeSignature))));

        live->startRecording (targets, options);
        return state();
    }

    if (method == "record.stop" && live->isRecordingNotes())
    {
        live->stop();
        return { { "clips", Json::array() }, { "transport", state() } };
    }

    if (method == "record.stop")
    {
        ExecuteOptions options;
        options.origin = client.origin;

        if (params.contains ("group"))
            options.group = readParam<std::string> (params, "group");

        return { { "clips", live->stopRecording (options) }, { "transport", state() } };
    }

    auto& devices = context.getEngine().getDeviceManager().deviceManager;

    if (method == "audio.devices")
    {
        auto types = Json::array();

        for (auto type : devices.getAvailableDeviceTypes())
        {
            type->scanForDevices();
            auto names = [] (const juce::StringArray& list)
            {
                auto j = Json::array();

                for (auto& n : list)
                    j.push_back (n.toStdString());

                return j;
            };

            // Headsets' microphones aren't offered: they'd turn playback into call quality.
            juce::StringArray inputs;

            for (auto& name : type->getDeviceNames (true))
                if (! isBluetoothAudioDevice (name))
                    inputs.add (name);

            types.push_back ({ { "type", type->getTypeName().toStdString() },
                               { "outputs", names (type->getDeviceNames (false)) },
                               { "inputs", names (inputs) } });
        }

        auto setup = devices.getAudioDeviceSetup();
        return { { "types", types },
                 { "current", { { "type", devices.getCurrentAudioDeviceType().toStdString() },
                                { "output", setup.outputDeviceName.toStdString() }, { "input", setup.inputDeviceName.toStdString() },
                                { "bluetoothInput", isBluetoothAudioDevice (setup.inputDeviceName) },
                                { "followSystemOutput", live->getFollowSystemOutput() }, { "systemOutput", defaultAudioDeviceName (false).toStdString() },
                                { "sampleRate", setup.sampleRate }, { "bufferSize", setup.bufferSize } } } };
    }

    if (method == "audio.setDevice")
    {
        if (params.contains ("type"))
            devices.setCurrentAudioDeviceType (readParam<std::string> (params, "type"), true);

        // This setup becomes the chosen one (never with a headset's microphone; see applyAudioSetup).
        auto setup = devices.getAudioDeviceSetup();

        // An output by name is the one to use from now on; "" goes back to following the system's.
        const auto output = params.contains ("output") ? std::optional (readParam<std::string> (params, "output")) : std::nullopt;

        if (output && ! output->empty())                            setup.outputDeviceName = *output;
        if (params.contains ("input"))                              setup.inputDeviceName = readParam<std::string> (params, "input");
        if (auto v = optionalNumber (params, "sampleRate"))         setup.sampleRate = *v;
        if (auto v = optionalNumber (params, "bufferSize"))         setup.bufferSize = static_cast<int> (*v);

        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;

        std::string notice;

        if (auto error = applyAudioSetup (devices, setup, true, notice); error.isNotEmpty())
            throw RpcError (operationFailed, error.toStdString());

        live->pairStereoInputs();

        if (output)
            live->setFollowSystemOutput (output->empty());

        auto result = call ("audio.devices", Json::object(), client);

        if (! notice.empty())
            result["notice"] = notice;

        return result;
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

namespace
{
    /** One for the whole engine: the model stays loaded when another project opens. */
    voice::Transcriber& getTranscriber()
    {
        static voice::Transcriber transcriber (EngineContext::getAppDataFolder() / "models");
        return transcriber;
    }

    Json voiceStatusJson (const voice::Transcriber::Status& s)
    {
        Json j { { "ready", s.ready }, { "downloading", s.downloading }, { "progress", std::round (s.progress * 1000.0) / 1000.0 },
                 { "modelBytes", s.modelBytes } };

        if (! s.error.empty())
            j["error"] = s.error;

        return j;
    }
}

Json ApiHandler::callVoice (const std::string& method, const Json& params)
{
    auto& transcriber = getTranscriber();

    if (method == "voice.status")
    {
        // The UI asks when voice may be used soon: the model loads meanwhile.
        if (params.value ("prepare", false))
            transcriber.prepare();

        return voiceStatusJson (transcriber.getStatus());
    }

    if (method == "voice.download")
    {
        // Progress goes to every client from the message thread.
        transcriber.download ([alive = alive, this] (const voice::Transcriber::Status& s)
        {
            juce::MessageManager::callAsync ([alive, this, s]
            {
                if (*alive && broadcast)
                    broadcast (notification ("voiceModel", voiceStatusJson (s)).dump());
            });
        });

        return voiceStatusJson (transcriber.getStatus());
    }

    if (method == "voice.transcribe")
    {
        // 16-bit little-endian PCM at 16 kHz, mono, as base64.
        juce::MemoryOutputStream decoded;

        if (! juce::Base64::convertFromBase64 (decoded, juce::String (readParam<std::string> (params, "audio"))))
            throw RpcError (invalidParams, "audio must be base64");

        const auto* pcm = static_cast<const int16_t*> (decoded.getData());
        std::vector<float> samples (decoded.getDataSize() / 2);

        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float> (pcm[i]) / 32768.0f;

        if (! transcriber.getStatus().ready)
            throw RpcError (operationFailed, "The speech model isn't downloaded yet (voice.download)");

        // It takes a moment: the text arrives as a voiceTranscript notification with this job id.
        const auto job = juce::Uuid().toString().toStdString();
        const auto language = params.value ("language", std::string());

        std::thread ([alive = alive, this, job, language, samples = std::move (samples), &transcriber]
        {
            Json result { { "job", job }, { "recording", "voice-" + job.substr (0, 6) } };

            try
            {
                result["text"] = transcriber.transcribe (samples, language);

                // A melody hummed or sung in it ("for the lead: da da da") is noted, to be placed
                // with melody.voice.
                const auto frames = music::trackPitch (samples.data(), static_cast<int> (samples.size()), voice::Transcriber::sampleRate);

                if (auto sung = music::findSungPart (frames))
                {
                    int count = 0;

                    for (auto& n : music::segmentNotes (frames))
                        if (n.start >= sung->start - 0.01 && n.end <= sung->end + 0.01)
                            ++count;

                    result["melody"] = { { "from", std::round (sung->start * 100) / 100 }, { "to", std::round (sung->end * 100) / 100 }, { "notes", count } };
                }
            }
            catch (const std::exception& e)
            {
                result["error"] = e.what();
            }

            juce::MessageManager::callAsync ([alive, this, result, samples = std::move (samples)] () mutable
            {
                if (! *alive)
                    return;

                voiceRecordings.emplace_back (result["recording"].get<std::string>(), std::move (samples));

                if (voiceRecordings.size() > 8)
                    voiceRecordings.erase (voiceRecordings.begin());

                if (broadcast)
                    broadcast (notification ("voiceTranscript", result).dump());
            });
        }).detach();

        return { { "job", job } };
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

namespace
{
    /** Where a melody is placed: the song's tempo and key (or the one asked for) and a register. */
    music::FitOptions fitOptions (const Json& params, const model::Project& project, bool snapByDefault)
    {
        music::FitOptions options;
        options.songTempo = project.tempo;

        const auto keyName = params.contains ("key") ? params["key"].get<std::string>() : project.key.value_or ("");

        if (! keyName.empty())
        {
            options.key = music::parseKey (keyName);

            if (! options.key)
                throw RpcError (invalidParams, "\"" + keyName + "\" isn't a key; name one like \"A minor\"");
        }

        options.snapToScale = params.value ("snapToScale", snapByDefault);

        if (auto r = params.value ("register", std::string()); ! r.empty())
        {
            static const std::map<std::string, std::pair<int, int>> registers {
                { "bass", { 28, 52 } }, { "chords", { 48, 72 } }, { "lead", { 60, 84 } }, { "high", { 72, 96 } },
            };

            auto it = registers.find (r);

            if (it == registers.end())
                throw RpcError (invalidParams, "register is bass, chords, lead or high");

            options.lowest = it->second.first;
            options.highest = it->second.second;
        }

        if (params.contains ("lowest"))     options.lowest = params["lowest"].get<int>();
        if (params.contains ("highest"))    options.highest = params["highest"].get<int>();

        return options;
    }

    Json melodyJson (const music::Melody& m, const music::FitOptions& options)
    {
        auto notes = Json::array();

        for (auto& n : m.notes)
            notes.push_back ({ { "start", n.start }, { "length", n.length }, { "key", n.key }, { "velocity", n.velocity } });

        Json j { { "notes", notes }, { "length", m.length }, { "heardTempo", std::round (m.sourceTempo * 10) / 10 },
                 { "transposed", m.transposed } };

        if (m.heardKey)
            j["heardKey"] = { { "key", music::keyName (m.heardKey->key) }, { "confidence", std::round (m.heardKey->confidence * 100) / 100 } };

        if (options.key)
            j["key"] = music::keyName (*options.key);

        return j;
    }
}

Json ApiHandler::callMelody (const std::string& method, const Json& params)
{
    if (method == "melody.voice")
    {
        if (voiceRecordings.empty())
            throw RpcError (operationFailed, "There's no voice message yet: record one with a melody hummed in it");

        auto it = voiceRecordings.end() - 1;

        if (auto id = params.value ("recording", std::string()); ! id.empty())
        {
            it = std::find_if (voiceRecordings.begin(), voiceRecordings.end(), [&] (auto& r) { return r.first == id; });

            if (it == voiceRecordings.end())
                throw RpcError (invalidParams, "no voice message \"" + id + "\" (only the last 8 are kept)");
        }

        const auto& samples = it->second;
        const auto frames = music::trackPitch (samples.data(), static_cast<int> (samples.size()), voice::Transcriber::sampleRate);
        auto sung = music::findSungPart (frames);

        if (params.contains ("from") || params.contains ("to"))
            sung = music::Span { params.value ("from", 0.0), params.value ("to", 1.0e9) };

        if (! sung)
            throw RpcError (operationFailed, "No melody was heard in that voice message: hum or sing it on \"da\", a note at a time");

        std::vector<music::HeardNote> notes;

        for (auto& n : music::segmentNotes (frames))
            if (n.start >= sung->start - 0.01 && n.end <= sung->end + 0.01)
                notes.push_back (n);

        auto options = fitOptions (params, session.getProject(), true);

        if (params.value ("tempo", std::string()) == "song")
            options.sourceTempo = session.getProject().tempo;

        auto j = melodyJson (music::fitMelody (notes, options), options);
        j["recording"] = it->first;
        j["from"] = sung->start;
        j["to"] = sung->end;
        return j;
    }

    if (method == "melody.reference")
    {
        if (listening == nullptr)
            throw RpcError (operationFailed, "this engine has no listening");

        const auto part = params.value ("part", std::string ("lead"));

        if (part != "lead" && part != "chords" && part != "bass")
            throw RpcError (invalidParams, "part is lead, chords or bass");

        listening::Listening::ReferenceNotes heard;

        try
        {
            heard = listening->hearNotes (params.value ("style", std::string()), readParam<std::string> (params, "track"),
                                          params.contains ("from") ? std::optional (params["from"].get<double>()) : std::nullopt,
                                          params.value ("length", 24.0), part == "bass" ? listening::Role::bass : listening::Role::music);
        }
        catch (const std::runtime_error& e)
        {
            throw RpcError (operationFailed, e.what());
        }

        // A lead is the top line of what plays, a bassline the bottom one; chords keep it all.
        const auto notes = part == "lead" ? music::topLine (heard.notes) : part == "bass" ? music::bottomLine (heard.notes) : heard.notes;

        auto options = fitOptions (params, session.getProject(), false);

        if (heard.tempo > 0)
            options.sourceTempo = heard.tempo;

        if (! params.contains ("register"))
            if (! params.contains ("lowest") && ! params.contains ("highest"))
                options.lowest = options.highest = std::nullopt;

        auto j = melodyJson (music::fitMelody (notes, options), options);
        j["track"] = heard.track;
        j["from"] = std::round (heard.from * 100) / 100;
        j["seconds"] = heard.length;
        j["part"] = part;
        return j;
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::callProject (const std::string& method, const Json& params)
{
    const auto recents = recentFile.empty() ? EngineContext::getAppDataFolder() / "recent.json" : recentFile;

    if (method == "project.recent")
    {
        auto list = Json::array();

        for (auto& dir : recent::load (recents))
        {
            const juce::File song ((dir / "song.json").string());
            list.push_back ({ { "path", dir.string() }, { "name", dir.filename().string() },
                              { "modified", song.getLastModificationTime().toMilliseconds() } });
        }

        Json result { { "projects", list } };

        if (auto dir = session.getProjectDir())
            result["current"] = dir->string();

        return result;
    }

    if (method == "project.importFile" || method == "project.importData" || method == "project.collectSamples")
        return importIntoProject (method, params);

    // Every change is written as it's made; saving marks where the song was saved, which
    // closing without saving goes back to.
    if (method == "project.save" || method == "project.revert")
    {
        if (method == "project.save")
            session.markSaved();
        else
            session.revertToSaved();

        Json state { { "saved", headJson (session.getSavedHead()) }, { "head", headJson (session.getHead()) } };

        if (broadcast)
            broadcast (notification ("saved", state).dump());

        return state;
    }

    if (method != "project.open" && method != "project.new" && method != "project.saveAs")
        throw RpcError (methodNotFound, "unknown method \"" + method + "\"");

    if (! onOpenProject)
        throw RpcError (operationFailed, "this engine can't switch projects");

    const auto dir = std::filesystem::absolute (readParam<std::string> (params, "path")).lexically_normal();
    std::error_code ec;

    if (method == "project.open")
    {
        if (! recent::isProject (dir))
            throw RpcError (invalidParams, dir.string() + " isn't a GrooveKit project (it has no song.json)");
    }
    else
    {
        if (recent::isProject (dir))
            throw RpcError (invalidParams, dir.string() + (method == "project.new" ? " is already a project; open it instead"
                                                                                   : " is already a project; choose a new folder"));

        if (std::filesystem::exists (dir, ec) && ! std::filesystem::is_empty (dir, ec))
            throw RpcError (invalidParams, dir.string() + " isn't empty; choose a new folder for the project");

        // A file system that's out of reach shows up here rather than after the switch.
        if (! std::filesystem::create_directories (dir, ec) && ec)
            throw RpcError (operationFailed, "Can't create " + dir.string() + ": " + ec.message());

        // Save as: the whole folder (song, history, recordings, plugin states) goes along, so
        // paths relative to the project still work; then the copy is opened.
        if (method == "project.saveAs")
        {
            const auto current = session.getProjectDir();

            if (! current)
                throw RpcError (operationFailed, "this session has no project folder to copy");

            std::filesystem::copy (*current, dir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing, ec);

            if (ec)
                throw RpcError (operationFailed, "Can't copy the project to " + dir.string() + ": " + ec.message());

            // The copy is the song as it is now, saved.
            session.markCopySaved (dir);
        }
    }

    if (auto current = session.getProjectDir(); current && std::filesystem::weakly_canonical (*current) == std::filesystem::weakly_canonical (dir))
        return { { "project", dir.string() } };

    onOpenProject (dir);
    return { { "project", dir.string() } };
}

Json ApiHandler::importIntoProject (const std::string& method, const Json& params)
{
    const auto projectDir = session.getProjectDir();

    if (! projectDir)
        throw RpcError (operationFailed, "this session has no project folder");

    const auto samplesDir = *projectDir / "samples";
    std::filesystem::create_directories (samplesDir);

    // A free name in samples/ ("kick.wav", "kick-2.wav", ...), or the same file if it's already there.
    auto destinationFor = [&] (const std::string& fileName, std::uintmax_t size) -> std::pair<std::filesystem::path, bool>
    {
        const auto name = std::filesystem::path (fileName);
        auto target = samplesDir / name.filename();
        std::error_code ec;

        for (int n = 2; std::filesystem::exists (target); ++n)
        {
            if (std::filesystem::file_size (target, ec) == size)
                return { target, true };

            target = samplesDir / (name.stem().string() + "-" + std::to_string (n) + name.extension().string());
        }

        return { target, false };
    };

    auto relative = [&] (const std::filesystem::path& p) { return p.lexically_relative (*projectDir).generic_string(); };

    auto importFile = [&] (const std::filesystem::path& source)
    {
        std::error_code ec;

        if (! std::filesystem::is_regular_file (source, ec))
            throw RpcError (invalidParams, "No file at " + source.string());

        auto [target, exists] = destinationFor (source.filename().string(), std::filesystem::file_size (source, ec));

        if (! exists)
            std::filesystem::copy_file (source, target, ec);

        if (ec)
            throw RpcError (operationFailed, "Can't copy " + source.string() + ": " + ec.message());

        return relative (target);
    };

    if (method == "project.importFile")
        return { { "path", importFile (readParam<std::string> (params, "path")) } };

    if (method == "project.importData")
    {
        juce::MemoryOutputStream decoded;

        if (! juce::Base64::convertFromBase64 (decoded, readParam<std::string> (params, "data")))
            throw RpcError (invalidParams, "data isn't base64");

        auto [target, exists] = destinationFor (readParam<std::string> (params, "name"), decoded.getDataSize());

        if (! exists && ! juce::File (target.string()).replaceWithData (decoded.getData(), decoded.getDataSize()))
            throw RpcError (operationFailed, "Can't write " + target.string());

        return { { "path", relative (target) } };
    }

    // collectSamples: every sample outside the project folder is copied in, and the song uses the copies.
    std::vector<model::Command> commands;
    std::map<std::string, std::string> copied;
    const auto& project = session.getProject();

    auto collect = [&] (const std::string& sample) -> std::optional<std::string>
    {
        std::filesystem::path p (sample);

        if (p.is_relative())
            return {};

        if (auto it = copied.find (sample); it != copied.end())
            return it->second;

        if (! std::filesystem::exists (p))
            return {};

        return copied[sample] = importFile (p);
    };

    for (auto& channel : project.channels)
        if (auto sampler = std::get_if<model::SamplerInstrument> (&channel.instrument))
            for (auto& layer : sampler->layers)
                if (auto path = collect (layer.sample))
                    commands.push_back (model::UpdateSamplerLayer { channel.id, layer.id, Json { { "sample", *path } } });

    if (! commands.empty())
        session.execute (commands, { "ui", {}, "Collect samples into the project" });

    return { { "copied", copied.size() }, { "updated", commands.size() } };
}

Json ApiHandler::callPlugins (const std::string& method, const Json& params, Client& client)
{
    if (plugins == nullptr)
        throw RpcError (operationFailed, "this engine has no plugin catalog");

    auto progressJson = [this]
    {
        auto p = plugins->getProgress();
        return Json { { "running", p.running }, { "scanned", p.scanned }, { "total", p.total }, { "current", p.current },
                      { "plugins", plugins->getPlugins().size() } };
    };

    if (method == "status")
        return progressJson();

    if (method == "scan")
    {
        plugins->startScan ([this, alive = alive] (const PluginCatalog::Progress& p)
        {
            if (*alive && broadcast)
                broadcast (notification ("pluginScanProgress", { { "running", p.running }, { "scanned", p.scanned },
                                                                 { "total", p.total }, { "current", p.current } }).dump());
        });

        return progressJson();
    }

    if (method == "list")
    {
        const auto words = juce::StringArray::fromTokens (juce::String (params.value ("query", "")), true);
        const auto instruments = params.contains ("instrument") ? std::optional (readParam<bool> (params, "instrument")) : std::nullopt;
        auto list = Json::array();

        for (auto& d : plugins->getPlugins())
        {
            if (instruments && d.isInstrument != *instruments)
                continue;

            const auto text = d.name + " " + d.manufacturerName + " " + d.category;
            bool all = true;

            for (auto& w : words)
                all = all && text.containsIgnoreCase (w);

            if (all)
                list.push_back (PluginCatalog::toJson (d));
        }

        return { { "plugins", list } };
    }

    if (method == "describe")
    {
        auto uid = readParam<std::string> (params, "uid");
        auto desc = plugins->find (uid);

        if (! desc)
            throw RpcError (invalidParams, "no plugin \"" + uid + "\"; run plugins.scan first");

        // Load it in a scratch Edit to read its parameters.
        auto edit = tracktion::Edit::createSingleTrackEdit (context.getEngine(), tracktion::Edit::forRendering);
        auto plugin = edit->getPluginCache().createNewPlugin (tracktion::ExternalPlugin::xmlTypeName, *desc);
        auto external = dynamic_cast<tracktion::ExternalPlugin*> (plugin.get());

        if (external == nullptr || external->getAudioPluginInstance() == nullptr)
            throw RpcError (operationFailed, "couldn't load \"" + desc->name.toStdString() + "\""
                                             + (external != nullptr ? ": " + external->getLoadError().toStdString() : std::string()));

        // Instruments like the Digitakt have thousands of parameters: filter by words, and cap.
        const auto words = juce::StringArray::fromTokens (juce::String (params.value ("query", "")), true);
        const auto limit = std::clamp (params.value ("limit", 200), 1, 5000);
        auto parameters = Json::array();
        int matching = 0;

        for (int i = 0; i < external->getNumAutomatableParameters(); ++i)
        {
            auto p = external->getAutomatableParameter (i);

            if (p == nullptr)
                continue;

            bool all = true;

            for (auto& w : words)
                all = all && p->getParameterName().containsIgnoreCase (w);

            if (! all || matching++ >= limit)
                continue;

            parameters.push_back ({ { "id", p->paramID.toStdString() }, { "name", p->getParameterName().toStdString() },
                                    { "value", std::round (p->getCurrentNormalisedValue() * 1000.0) / 1000.0 },
                                    { "text", p->getCurrentValueAsStringWithLabel().toStdString() } });
        }

        auto result = PluginCatalog::toJson (*desc);
        result["parameterCount"] = external->getNumAutomatableParameters();
        plugin = nullptr;
        EditGraveyard::getInstance().bury (std::move (edit));
        result["matchingParameters"] = matching;
        result["parameters"] = parameters;
        result["pluginRef"] = { { "format", PluginCatalog::formatName (*desc) }, { "uid", uid }, { "name", desc->name.toStdString() },
                                { "manufacturer", desc->manufacturerName.toStdString() } };
        return result;
    }

    if (method == "captureState" || method == "openEditor" || method == "closeEditor")
    {
        if (live == nullptr)
            throw RpcError (operationFailed, "plugin windows and state need live playback");

        const auto key = params.contains ("channel") ? "channel:" + readParam<std::string> (params, "channel")
                                                     : "effect:" + readParam<std::string> (params, "insert") + "/" + readParam<std::string> (params, "effect");

        if (method == "captureState")
            return captureState (key, client.origin);

        if (! live->showPluginWindow (key, method == "openEditor"))
            throw RpcError (invalidParams, "no hosted plugin at " + key);

        return { { "open", method == "openEditor" } };
    }

    throw RpcError (methodNotFound, "unknown method \"plugins." + method + "\"");
}

Json ApiHandler::captureState (const std::string& key, const std::string& origin)
{
    live->updateNow();
    auto external = dynamic_cast<tracktion::ExternalPlugin*> (live->findPlugin (key));

    if (external == nullptr || external->getAudioPluginInstance() == nullptr)
        throw RpcError (invalidParams, "no hosted plugin at " + key);

    juce::MemoryBlock data;
    external->getAudioPluginInstance()->getStateInformation (data);

    // Content-addressed, so identical states share one file.
    uint64_t hash = 14695981039346656037ull;

    for (size_t i = 0; i < data.getSize(); ++i)
        hash = (hash ^ static_cast<unsigned char> (data[i])) * 1099511628211ull;

    char name[40];
    std::snprintf (name, sizeof (name), "plugins/%016llx.bin", static_cast<unsigned long long> (hash));

    const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
    std::filesystem::create_directories (projectDir / "plugins");
    juce::File ((projectDir / name).string()).replaceWithData (data.getData(), data.getSize());

    // Point the model at it.
    auto& doc = session.getDocument();

    auto indexOf = [] (const Json& array, const std::string& id)
    {
        for (size_t i = 0; i < array.size(); ++i)
            if (array[i].value ("id", "") == id)
                return std::to_string (i);

        throw RpcError (invalidParams, "no \"" + id + "\"");
    };

    std::string path;

    if (key.starts_with ("channel:"))
    {
        path = "/channels/" + indexOf (doc["channels"], key.substr (8)) + "/instrument/plugin/state";
    }
    else
    {
        const auto slash = key.find ('/');
        const auto insertIndex = indexOf (doc["mixer"]["inserts"], key.substr (7, slash - 7));
        path = "/mixer/inserts/" + insertIndex + "/effects/"
             + indexOf (doc["mixer"]["inserts"][std::stoul (insertIndex)]["effects"], key.substr (slash + 1)) + "/plugin/state";
    }

    if (doc.contains (Json::json_pointer (path)) && doc[Json::json_pointer (path)] == name)
        return { { "state", name }, { "bytes", data.getSize() }, { "changed", false } };

    model::Patch patch;
    patch.ops.push_back ({ model::PatchOpOp::add, path, Json (name) });

    ExecuteOptions options;
    options.origin = origin;
    options.label = "Save plugin state (" + external->getName().toStdString() + ")";
    session.execute ({ patch }, options);

    return { { "state", name }, { "bytes", data.getSize() }, { "changed", true } };
}

Json ApiHandler::callListening (const std::string& method, const Json& params)
{
    if (listening == nullptr)
        throw RpcError (operationFailed, "listening isn't available in this engine");

    try
    {
        if (method == "listening.status")
            return listening->getStatus();

        if (method == "listening.download")
        {
            listening->download();
            return listening->getStatus();
        }

        if (method == "listening.index")
        {
            listening->startIndexing();
            return listening->getStatus();
        }

        if (method == "styles.list")
        {
            auto list = Json::array();

            for (auto& name : listening->getStyles().names())
                if (auto profile = listening->getStyles().get (name))
                {
                    auto m = profile->measurements();
                    list.push_back ({ { "name", profile->name }, { "folder", profile->folder.string() }, { "tracks", profile->tracks.size() },
                                      { "tempo", m.value ("tempo", Json()) }, { "guide", guideFor (profile->name) } });
                }

            return { { "styles", list }, { "progress", listening->getStatus()["styles"] } };
        }

        // The references folder (one folder of tracks per style) and how each folder stands.
        if (method == "styles.references" || method == "styles.createReferences")
        {
            const auto root = referencesFolder.empty() ? listening::Styles::getReferencesFolder() : referencesFolder;

            if (method == "styles.createReferences")
                std::filesystem::create_directories (root);

            auto folders = Json::array();
            std::error_code ec;

            if (std::filesystem::is_directory (root, ec))
                for (auto& f : listening->getStyles().referenceFolders (root))
                    folders.push_back ({ { "name", f.name }, { "path", f.path.string() }, { "files", f.files },
                                         { "style", f.style ? Json (*f.style) : Json() }, { "unheard", f.unheard }, { "gone", f.gone },
                                         { "guide", guideFor (f.name) } });

            return { { "folder", root.string() }, { "exists", std::filesystem::is_directory (root, ec) }, { "folders", folders } };
        }

        if (method == "styles.add")
        {
            const auto name = listening->addStyle (readParam<std::string> (params, "path"), params.value ("name", ""));
            return { { "name", name }, { "progress", listening->getStatus()["styles"] } };
        }

        auto style = [&]
        {
            auto profile = listening->getStyles().get (readParam<std::string> (params, "name"));

            if (profile == nullptr)
                throw RpcError (invalidParams, "No style named \"" + params["name"].get<std::string>() + "\" (styles.list)");

            return profile;
        };

        if (method == "styles.describe")
            return listening->describeStyle (*style());

        if (method == "styles.compare")
            return compareToStyle (params);
    }
    catch (const RpcError&)
    {
        throw;
    }
    catch (const std::runtime_error& e)
    {
        throw RpcError (operationFailed, e.what());
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::callChats (const std::string& method, const Json& params)
{
    ChatStore store (chatsFolder.empty() ? ChatStore::getDefaultFolder() : chatsFolder);

    auto summaryJson = [] (const ChatStore::Summary& c)
    {
        return Json { { "id", c.id }, { "title", c.title }, { "project", c.project }, { "projectName", c.projectName },
                      { "created", c.created }, { "updated", c.updated }, { "messages", c.messages } };
    };

    if (method == "chats.list")
    {
        auto list = Json::array();

        for (auto& c : store.list())
            list.push_back (summaryJson (c));

        return { { "chats", list } };
    }

    if (method == "chats.get")
    {
        if (auto chat = store.get (readParam<std::string> (params, "id")))
            return *chat;

        throw RpcError (invalidParams, "No chat " + params["id"].dump());
    }

    if (method == "chats.save")
    {
        if (! params.contains ("chat") || ! params["chat"].is_object())
            throw RpcError (invalidParams, "chat must be an object with an id");

        try
        {
            return summaryJson (store.save (params["chat"]));
        }
        catch (const std::invalid_argument& e)
        {
            throw RpcError (invalidParams, e.what());
        }
    }

    if (method == "chats.delete")
        return { { "deleted", store.remove (readParam<std::string> (params, "id")) } };

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::callNotes (const std::string& method, const Json& params)
{
    AgentNotes notes (notesFile.empty() ? AgentNotes::getDefaultFile() : notesFile);
    const auto here = session.getProjectDir() ? session.getProjectDir()->string() : std::string();

    // A note's song: this one ("song"), or every song ("all").
    auto scope = [&] () -> std::optional<std::pair<std::string, std::string>>
    {
        if (! params.contains ("scope"))
            return {};

        const auto value = readParam<std::string> (params, "scope");

        if (value == "all")
            return std::pair<std::string, std::string>();

        if (value != "song")
            throw RpcError (invalidParams, "scope must be \"all\" or \"song\"");

        if (here.empty())
            throw RpcError (operationFailed, "No project is open to keep a note for");

        auto name = session.getProject().name;
        return std::pair (here, name.empty() ? session.getProjectDir()->filename().string() : name);
    };

    // here: whether the note applies to the open song (and so to the agent working on it).
    auto noteJson = [&] (const AgentNotes::Note& n)
    {
        auto j = toJson (n);
        j["here"] = n.project.empty() || n.project == here;
        return j;
    };

    auto listJson = [&]
    {
        auto list = Json::array();

        for (auto& n : notes.list())
            list.push_back (noteJson (n));

        return Json { { "notes", list } };
    };

    auto changed = [&] (Json result)
    {
        if (broadcast)
            broadcast (notification ("notes", listJson()).dump());

        return result;
    };

    try
    {
        if (method == "notes.list")
            return listJson();

        if (method == "notes.add")
        {
            const auto where = scope();
            return changed (noteJson (notes.add (readParam<std::string> (params, "text"), where ? where->first : "", where ? where->second : "")));
        }

        if (method == "notes.update")
        {
            AgentNotes::Change change;

            if (params.contains ("text"))       change.text = readParam<std::string> (params, "text");
            if (params.contains ("enabled"))    change.enabled = readParam<bool> (params, "enabled");
            change.project = scope();

            if (auto note = notes.update (readParam<std::string> (params, "id"), change))
                return changed (noteJson (*note));

            throw RpcError (invalidParams, "No note " + params["id"].dump() + " (notes.list)");
        }

        if (method == "notes.delete")
        {
            if (notes.remove (readParam<std::string> (params, "id")))
                return changed ({ { "deleted", true } });

            return { { "deleted", false } };
        }
    }
    catch (const std::invalid_argument& e)
    {
        throw RpcError (invalidParams, e.what());
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::guideFor (const std::string& styleName)
{
    if (auto guide = guides::GuideStore (guides::GuideStore::getDefaultFolder()).find (styleName))
        return guide->id;

    return {};
}

Json ApiHandler::callGuides (const std::string& method, const Json& params)
{
    guides::GuideStore store (guides::GuideStore::getDefaultFolder());
    const auto style = session.getProject().style;

    auto listJson = [&]
    {
        auto list = Json::array();

        for (auto& g : store.list())
        {
            auto j = guides::toJson (g, false);
            j["song"] = style && *style == g.id;
            list.push_back (j);
        }

        return Json { { "guides", list }, { "style", style ? Json (*style) : Json() } };
    };

    auto changed = [&] (Json result)
    {
        if (broadcast)
            broadcast (notification ("guides", listJson()).dump());

        return result;
    };

    try
    {
        if (method == "guides.list")
            return listJson();

        // One guide in full: by id or name, or the song's.
        if (method == "guides.get")
        {
            const auto wanted = params.contains ("id") ? readParam<std::string> (params, "id") : style.value_or ("");

            if (wanted.empty())
                throw RpcError (invalidParams, "The song has no style yet; name a guide (guides.list) or set one (setStyle)");

            if (auto guide = store.find (wanted))
                return guides::toJson (*guide, true);

            throw RpcError (invalidParams, "No style guide \"" + wanted + "\" (guides.list)");
        }

        if (method == "guides.save")
            return changed (guides::toJson (store.save (params.value ("id", std::string()), readParam<std::string> (params, "text")), true));

        if (method == "guides.remove")
        {
            const auto removed = store.remove (readParam<std::string> (params, "id"));

            if (removed == guides::GuideStore::Removed::nothing)
                return { { "removed", "nothing" } };

            return changed ({ { "removed", removed == guides::GuideStore::Removed::restored ? "restored" : "deleted" } });
        }
    }
    catch (const std::invalid_argument& e)
    {
        throw RpcError (invalidParams, e.what());
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

// The song maps: every reference track heard once, curated in the References tab.
Json ApiHandler::callMaps (const std::string& method, const Json& params)
{
    if (listening == nullptr)
        throw RpcError (operationFailed, "listening isn't available in this engine");

    auto& styles = listening->getStyles();
    auto& maps = styles.getMaps();
    const auto root = referencesFolder.empty() ? listening::Styles::getReferencesFolder() : referencesFolder;

    auto ids = [&]
    {
        return readParam<std::vector<std::string>> (params, "ids");
    };

    // The songs asked for, as they stand.
    auto songsFor = [&] (const std::vector<std::string>& wanted)
    {
        std::vector<listening::Styles::Song> found;

        for (auto& song : styles.songs (root))
            if (std::find (wanted.begin(), wanted.end(), song.id) != wanted.end())
                found.push_back (song);

        return found;
    };

    auto songJson = [] (const listening::Styles::Song& s)
    {
        Json j { { "id", s.id }, { "style", s.style }, { "title", s.title }, { "status", s.status }, { "inUse", s.inUse } };

        if (s.file)
            j["file"] = s.file->string();

        if (! s.error.empty())
            j["error"] = s.error;

        if (s.map != nullptr)
            j["map"] = { { "source", s.map->source }, { "heardAt", s.map->heardAt }, { "analysis", s.map->track.analysis },
                         { "tempo", std::round (s.map->track.tempo * 10) / 10 }, { "lufs", std::round (s.map->track.lufs * 10) / 10 } };

        return j;
    };

    try
    {
        if (method == "maps.list")
        {
            auto list = Json::array();

            for (auto& song : styles.songs (root))
                list.push_back (songJson (song));

            return { { "folder", maps.getFolder().string() }, { "references", root.string() }, { "songs", list } };
        }

        if (method == "maps.status")
        {
            const auto p = styles.getProgress();
            auto item = [] (const listening::Styles::Progress::Item& i)
            {
                return Json { { "id", i.id }, { "style", i.style }, { "title", i.title } };
            };

            auto queued = Json::array(), recent = Json::array();

            for (auto& i : p.queued)
                queued.push_back (item (i));

            for (auto& d : p.recent)
            {
                auto j = item (d.item);
                j["ok"] = d.ok;
                j["seconds"] = std::round (d.seconds * 10) / 10;
                j["at"] = d.at;

                if (! d.error.empty())
                    j["error"] = d.error;

                recent.push_back (j);
            }

            Json result { { "running", p.running }, { "paused", p.paused }, { "queued", queued }, { "recent", recent },
                          { "secondsPerSong", std::round (p.secondsPerSong * 10) / 10 }, { "ready", listening->getModels().getStatus().ready } };

            if (p.current)
            {
                result["current"] = item (*p.current);
                result["current"]["progress"] = p.currentProgress;
                result["current"]["started"] = p.currentStarted;
            }

            return result;
        }

        if (method == "maps.get")
        {
            const auto id = readParam<std::string> (params, "id");
            auto map = maps.get (id);

            if (map == nullptr)
                throw RpcError (invalidParams, "No map for \"" + id + "\" (maps.list)");

            // What was heard, without the sound fingerprints (only how many there are).
            auto j = map->toJson();
            auto& sounds = j["track"]["sounds"];

            for (auto& [role, list] : sounds.items())
                list = list.size();

            j["id"] = id;
            j["inUse"] = maps.isInUse (id);
            j["current"] = map->track.analysis >= listening::currentAnalysisVersion();
            return j;
        }

        if (method == "maps.setInUse")
        {
            maps.setInUse (ids(), readParam<bool> (params, "inUse"));
            listening->notifyChanged();
            return { { "changed", params["ids"].size() } };
        }

        if (method == "maps.hear")
        {
            std::vector<std::pair<std::string, std::filesystem::path>> todo;

            for (auto& song : songsFor (ids()))
                if (song.file)
                    todo.emplace_back (song.style, *song.file);

            styles.hear (todo);
            return { { "queued", todo.size() } };
        }

        if (method == "maps.hearNew")
        {
            const auto style = params.value ("style", std::string());
            std::vector<std::pair<std::string, std::filesystem::path>> todo;

            for (auto& song : styles.songs (root))
                if (song.file && (style.empty() || song.style == style) && (song.status == "new" || song.status == "changed" || song.status == "failed"))
                    todo.emplace_back (song.style, *song.file);

            styles.hear (todo);
            return { { "queued", todo.size() } };
        }

        if (method == "maps.pause")     { styles.pause();       return { { "paused", true } }; }
        if (method == "maps.resume")    { styles.resume();      return { { "paused", false } }; }
        if (method == "maps.clearQueue") { styles.clearQueue(); return { { "cleared", true } }; }

        if (method == "maps.unqueue")
        {
            styles.unqueue (ids());
            return { { "unqueued", params["ids"].size() } };
        }

        if (method == "maps.forget")
        {
            styles.forget (ids());
            return { { "forgotten", params["ids"].size() } };
        }

        if (method == "maps.unskip")
        {
            maps.setSkipped (ids(), false);
            listening->notifyChanged();
            return { { "unskipped", params["ids"].size() } };
        }

        // Out of the references: the audio goes to "Not used" beside the style folders (nothing is
        // deleted), the map with it.
        if (method == "maps.removeReferences")
        {
            int moved = 0;
            std::error_code ec;

            for (auto& song : songsFor (ids()))
            {
                if (song.file)
                {
                    const auto styleFolder = song.file->parent_path();
                    const auto base = song.file->lexically_relative (root).string().starts_with ("..") ? styleFolder.parent_path() : root;
                    const auto folder = base / "Not used" / song.style;
                    std::filesystem::create_directories (folder);

                    auto target = juce::File ((folder / song.file->filename()).string()).getNonexistentSibling (false);

                    if (! juce::File (song.file->string()).moveFileTo (target))
                        throw RpcError (operationFailed, "Couldn't move " + song.file->string() + " to " + folder.string());

                    ++moved;
                }

                maps.remove (song.id);
                maps.setSkipped ({ song.id }, false);
            }

            styles.unqueue (ids());
            return { { "moved", moved } };
        }

        if (method == "maps.export")
        {
            const std::filesystem::path zip = readParam<std::string> (params, "path");
            const auto count = maps.exportZip (ids(), zip);
            return { { "path", zip.string() }, { "count", count } };
        }

        if (method == "maps.import")
        {
            const auto result = maps.importZip (readParam<std::string> (params, "path"));
            listening->notifyChanged();
            return { { "added", result.added }, { "kept", result.kept } };
        }
    }
    catch (const RpcError&)
    {
        throw;
    }
    catch (const std::runtime_error& e)
    {
        throw RpcError (operationFailed, e.what());
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::compareToStyle (const Json& params)
{
    auto profile = listening->getStyles().get (readParam<std::string> (params, "name"));

    if (profile == nullptr)
        throw RpcError (invalidParams, "No style named \"" + params["name"].get<std::string>() + "\" (styles.list)");

    if (! listening->getModels().getStatus().ready)
        throw RpcError (operationFailed, "The listening models aren't downloaded yet (listening.download)");

    RenderOptions options;

    if (auto v = optionalNumber (params, "start"))  options.start = static_cast<model::Ticks> (*v);
    if (auto v = optionalNumber (params, "end"))    options.end = static_cast<model::Ticks> (*v);

    const auto& project = session.getProject();
    const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
    const auto workDir = projectDir / "renders" / "listening";
    std::filesystem::create_directories (workDir);

    // Which part each channel mostly plays, from its first sample's category: a hint for acting
    // on the comparison, which hears the mix itself split into parts.
    auto partOf = [&] (const model::Channel& c)
    {
        if (auto* sampler = std::get_if<model::SamplerInstrument> (&c.instrument); sampler != nullptr && ! sampler->layers.empty())
        {
            std::filesystem::path sample = sampler->layers.front().sample;

            if (sample.is_relative())
                sample = projectDir / sample;

            std::string category = "other";

            if (auto entry = library != nullptr ? library->find (sample) : std::nullopt)
                category = entry->classification.category;
            else
                try { category = library::SampleLibrary::analyseFile (sample).classification.category; } catch (const std::exception&) {}

            const auto role = listening::roleOf (category, true);
            return role == listening::Role::kick || role == listening::Role::hats ? listening::Role::drums : role;
        }

        return juce::String (c.name).containsIgnoreCase ("bass") ? listening::Role::bass : listening::Role::music;
    };

    std::map<listening::Role, std::vector<std::string>> parts;

    for (auto& c : project.channels)
        if (! c.mute)
            parts[partOf (c)].push_back (c.id);

    const auto file = workDir / "mix.wav";
    auto compiled = compileProject (context.getEngine(), project, { projectDir, tracktion::Edit::forRendering });
    renderToWav (compiled, project, file, options);

    Json result;

    try
    {
        result = listening->compare (*profile, file, project.tempo, project.swing);
    }
    catch (...)
    {
        std::filesystem::remove (file);
        throw;
    }

    std::filesystem::remove (file);

    auto partsJson = Json::object();

    for (auto& [role, channels] : parts)
        partsJson[listening::roleName (role)] = channels;

    result["channels"] = partsJson;
    return result;
}

void ApiHandler::startLibraryScan()
{
    library->startScan ([this, alive = alive] (const library::SampleLibrary::Progress& p)
    {
        Json params { { "running", p.running }, { "filesFound", p.filesFound }, { "analysed", p.analysed },
                      { "unchanged", p.unchanged }, { "failed", p.failed } };

        // Called on the scan thread: broadcast from the message thread. New samples are then heard.
        juce::MessageManager::callAsync ([this, alive, params]
        {
            if (! *alive)
                return;

            if (broadcast)
                broadcast (notification ("libraryProgress", params).dump());

            if (listening != nullptr && ! params.value ("running", true))
                listening->startIndexing();
        });
    });
}

} // namespace gk
