#include "ApiHandler.h"

#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "analysis/MixAnalysis.h"
#include "compiler/EditCompiler.h"
#include "compiler/EditGraveyard.h"
#include "effects/BuiltinEffects.h"
#include "library/SampleLibrary.h"
#include "live/AudioDevices.h"
#include "live/LiveEngine.h"
#include "plugins/PluginCatalog.h"
#include "plugins/PluginWindows.h"
#include "render/Render.h"
#include "session/Mastering.h"
#include "session/RecentProjects.h"
#include "session/Summary.h"

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
                        { "mode", t.pattern ? "pattern" : "song" }, { "metronome", t.metronome }, { "countInBars", t.countInBars } };

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

ApiHandler::ApiHandler (Session& s, EngineContext& c, library::SampleLibrary* l, LiveEngine* le, PluginCatalog* p)
    : session (s), context (c), library (l), live (le), plugins (p)
{
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
    }

    session.removeListener (listenerId);
}

void ApiHandler::setBroadcast (Send b)
{
    broadcast = std::move (b);
}

std::string ApiHandler::getSnapshotMessage() const
{
    Json params { { "document", session.getDocument() }, { "head", headJson (session.getHead()) } };

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

    if (method == "listEffects")
        return effects::catalogueToJson();

    if (method == "audio.peaks")
        return peaks (params);

    if (method.starts_with ("library."))
        return callLibrary (method.substr (8), params);

    if (method.starts_with ("transport.") || method.starts_with ("record.") || method.starts_with ("audio.") || method.starts_with ("midi.") || method.starts_with ("live.")
        || method.starts_with ("preview."))
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

    const auto resolution = std::clamp (optionalNumber (params, "resolution").value_or (100.0), 1.0, 1000.0);
    const auto key = path.string() + "@" + std::to_string (resolution);
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time (path, ec);

    if (ec)
        throw RpcError (operationFailed, "no file \"" + path.string() + "\"");

    if (auto it = peaksCache.find (key); it != peaksCache.end() && it->second.first == modified)
        return it->second.second;

    const auto audio = analysis::loadAudio (path);
    const auto& buffer = audio.buffer;
    const auto perPeak = std::max (1, static_cast<int> (std::round (audio.sampleRate / resolution)));
    auto values = Json::array();

    for (int start = 0; start < buffer.getNumSamples(); start += perPeak)
    {
        const auto n = std::min (perPeak, buffer.getNumSamples() - start);
        float peak = 0;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = std::max (peak, buffer.getMagnitude (ch, start, n));

        values.push_back (std::round (std::min (peak, 1.0f) * 1000.0f) / 1000.0f);
    }

    Json result { { "durationSeconds", buffer.getNumSamples() / audio.sampleRate },
                  { "sampleRate", audio.sampleRate },
                  { "channels", buffer.getNumChannels() },
                  { "resolution", audio.sampleRate / perPeak },
                  { "peaks", std::move (values) } };

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

            for (auto& t : project.playlist.tracks)
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
        if (auto v = optionalNumber (params, "tempo"))          q.tempo = v;
        if (auto v = optionalNumber (params, "minDuration"))    q.minDuration = v;
        if (auto v = optionalNumber (params, "maxDuration"))    q.maxDuration = v;

        auto results = Json::array();

        for (auto& e : library->search (q))
            results.push_back (library::SampleLibrary::toJson (e, false));

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
        auto path = readParam<std::string> (params, "path");
        auto entry = library->find (path);
        return library::SampleLibrary::toJson (entry ? *entry : library::SampleLibrary::analyseFile (path), true);
    }

    throw RpcError (methodNotFound, "unknown method \"library." + method + "\"");
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
            if (! t.is_object() || ! t.contains ("input") || ! t.contains ("track"))
                throw RpcError (invalidParams, "each target needs input and track (and optionally insert)");

            targets.push_back ({ t["input"].get<std::string>(), t.value ("insert", std::string (model::masterInsertId)),
                                 t["track"].get<std::string>() });
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

            types.push_back ({ { "type", type->getTypeName().toStdString() },
                               { "outputs", names (type->getDeviceNames (false)) },
                               { "inputs", names (type->getDeviceNames (true)) } });
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

        // Keep a Bluetooth headset's microphone closed unless it's the chosen input; this setup becomes the chosen one.
        const bool releaseInput = ! params.contains ("input") && hasUnchosenBluetoothInput (devices);
        auto setup = devices.getAudioDeviceSetup();

        if (releaseInput)
            setup.inputDeviceName = {};

        // An output by name is the one to use from now on; "" goes back to following the system's.
        const auto output = params.contains ("output") ? std::optional (readParam<std::string> (params, "output")) : std::nullopt;

        if (output && ! output->empty())                            setup.outputDeviceName = *output;
        if (params.contains ("input"))                              setup.inputDeviceName = readParam<std::string> (params, "input");
        if (auto v = optionalNumber (params, "sampleRate"))         setup.sampleRate = *v;
        if (auto v = optionalNumber (params, "bufferSize"))         setup.bufferSize = static_cast<int> (*v);

        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;

        if (auto error = devices.setAudioDeviceSetup (setup, true); error.isNotEmpty())
            throw RpcError (operationFailed, error.toStdString());

        live->pairStereoInputs();

        if (output)
            live->setFollowSystemOutput (output->empty());

        return call ("audio.devices", Json::object(), client);
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

    for (auto& clip : project.playlist.clips)
        if (auto audio = std::get_if<model::AudioClip> (&clip))
            if (auto path = collect (audio->sample))
                commands.push_back (model::UpdateClip { audio->id, Json { { "sample", *path } } });

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

void ApiHandler::startLibraryScan()
{
    library->startScan ([this, alive = alive] (const library::SampleLibrary::Progress& p)
    {
        Json params { { "running", p.running }, { "filesFound", p.filesFound }, { "analysed", p.analysed },
                      { "unchanged", p.unchanged }, { "failed", p.failed } };

        // Called on the scan thread: broadcast from the message thread.
        juce::MessageManager::callAsync ([this, alive, params]
        {
            if (*alive && broadcast)
                broadcast (notification ("libraryProgress", params).dump());
        });
    });
}

} // namespace gk
