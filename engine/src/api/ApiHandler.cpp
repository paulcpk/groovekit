#include "ApiHandler.h"

#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "analysis/MixAnalysis.h"
#include "compiler/EditCompiler.h"
#include "compiler/EditGraveyard.h"
#include "effects/BuiltinEffects.h"
#include "library/SampleLibrary.h"
#include "live/LiveEngine.h"
#include "plugins/PluginCatalog.h"
#include "render/Render.h"
#include "session/Mastering.h"
#include "session/Summary.h"

#include <juce_events/juce_events.h>

namespace gk
{

using model::Json;

namespace
{
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
                broadcast (notification ("transport", { { "playing", t.playing }, { "recording", t.recording },
                                                        { "position", t.position }, { "seconds", t.seconds } }).dump());
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
    }

    session.removeListener (listenerId);
}

void ApiHandler::setBroadcast (Send b)
{
    broadcast = std::move (b);
}

std::string ApiHandler::getSnapshotMessage() const
{
    return notification ("snapshot", { { "document", session.getDocument() }, { "head", headJson (session.getHead()) } }).dump();
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

    if (method.starts_with ("transport.") || method.starts_with ("record.") || method.starts_with ("audio.") || method.starts_with ("midi.")
        || method.starts_with ("preview."))
        return callLive (method, params, client);

    if (method.starts_with ("plugins."))
        return callPlugins (method.substr (8), params);

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

    return { { "path", std::filesystem::absolute (output).string() },
             { "analysis", analysis::analyse (analysis::loadAudio (output), true) } };
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

    if (method == "folders" || method == "status")
        return { { "folders", folders() }, { "progress", progressJson() } };

    if (method == "addFolder")
    {
        library->addFolder (readParam<std::string> (params, "path"));

        if (params.value ("scan", true))
            startLibraryScan();

        return { { "folders", folders() }, { "progress", progressJson() } };
    }

    if (method == "removeFolder")
    {
        library->removeFolder (readParam<std::string> (params, "path"));
        return { { "folders", folders() }, { "progress", progressJson() } };
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

    auto state = [this]
    {
        auto t = live->getTransportState();
        return Json { { "playing", t.playing }, { "recording", t.recording }, { "position", t.position }, { "seconds", t.seconds } };
    };

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

        return { { "outputs", outputs }, { "inputs", live->getMidiInputs() } };
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
                                { "sampleRate", setup.sampleRate }, { "bufferSize", setup.bufferSize } } } };
    }

    if (method == "audio.setDevice")
    {
        if (params.contains ("type"))
            devices.setCurrentAudioDeviceType (readParam<std::string> (params, "type"), true);

        auto setup = devices.getAudioDeviceSetup();

        if (params.contains ("output"))                             setup.outputDeviceName = readParam<std::string> (params, "output");
        if (params.contains ("input"))                              setup.inputDeviceName = readParam<std::string> (params, "input");
        if (auto v = optionalNumber (params, "sampleRate"))         setup.sampleRate = *v;
        if (auto v = optionalNumber (params, "bufferSize"))         setup.bufferSize = static_cast<int> (*v);

        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;

        if (auto error = devices.setAudioDeviceSetup (setup, true); error.isNotEmpty())
            throw RpcError (operationFailed, error.toStdString());

        live->pairStereoInputs();

        return call ("audio.devices", Json::object(), client);
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::callPlugins (const std::string& method, const Json& params)
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

    if (method == "captureState")
    {
        if (live == nullptr)
            throw RpcError (operationFailed, "capturing plugin state needs live playback");

        const bool forChannel = params.contains ("channel");
        const auto key = forChannel ? "channel:" + readParam<std::string> (params, "channel")
                                    : "effect:" + readParam<std::string> (params, "insert") + "/" + readParam<std::string> (params, "effect");

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
        std::string path;

        auto indexOf = [] (const Json& array, const std::string& id)
        {
            for (size_t i = 0; i < array.size(); ++i)
                if (array[i].value ("id", "") == id)
                    return std::to_string (i);

            throw RpcError (invalidParams, "no \"" + id + "\"");
        };

        if (forChannel)
        {
            path = "/channels/" + indexOf (doc["channels"], params["channel"]) + "/instrument/plugin/state";
        }
        else
        {
            const auto insertIndex = indexOf (doc["mixer"]["inserts"], params["insert"]);
            path = "/mixer/inserts/" + insertIndex + "/effects/"
                 + indexOf (doc["mixer"]["inserts"][std::stoul (insertIndex)]["effects"], params["effect"]) + "/plugin/state";
        }

        model::Patch patch;
        patch.ops.push_back ({ model::PatchOpOp::add, path, Json (name) });

        ExecuteOptions options;
        options.label = "Save plugin state (" + external->getName().toStdString() + ")";
        session.execute ({ patch }, options);

        return { { "state", name }, { "bytes", data.getSize() } };
    }

    throw RpcError (methodNotFound, "unknown method \"plugins." + method + "\"");
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
