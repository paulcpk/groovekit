#include "ApiHandler.h"
#include "Rpc.h"

#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "analysis/MixAnalysis.h"
#include "compiler/EditCompiler.h"
#include "compiler/EditGraveyard.h"
#include "effects/BuiltinEffects.h"
#include "library/SampleLibrary.h"
#include "library/StarterPack.h"
#include "listening/Listening.h"
#include "listening/KickSound.h"
#include "listening/Maps.h"
#include "mixer/Slots.h"
#include "live/AudioDevices.h"
#include "live/LiveEngine.h"
#include "music/Notes.h"
#include "plugins/PluginCatalog.h"
#include "plugins/PluginWindows.h"
#include "render/Encode.h"
#include "render/Render.h"
#include "render/Stretch.h"
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

namespace
{
    /** The names a plugin gives its keys (a drum plugin's "Bass Drum" on 36), by key: VST3's
        pitch names, which most drum instruments declare. Empty when it names none. */
    Json keyNamesOf (juce::AudioPluginInstance& instance)
    {
        auto names = Json::object();

        for (int key = 0; key < 128; ++key)
            if (auto name = instance.getNameForMidiNoteNumber (key, 1); name && name->isNotEmpty())
                names[std::to_string (key)] = name->toStdString();

        return names;
    }
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
        auto resolve = [&] (std::filesystem::path path)
        {
            if (path.is_relative())
                if (auto dir = session.getProjectDir())
                    path = *dir / path;

            return path;
        };

        // under: a second file in sync beneath it (the rest of a song under a candidate), at underLevel.
        std::optional<std::filesystem::path> under;

        if (params.contains ("under"))
            under = resolve (readParam<std::string> (params, "under"));

        live->getPreview().play (resolve (readParam<std::string> (params, "path")), static_cast<float> (optionalNumber (params, "gainDb").value_or (-6.0)),
                                 optionalNumber (params, "from").value_or (0.0), under, static_cast<float> (optionalNumber (params, "underLevel").value_or (1.0)));

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

    if (method == "preview.setUnderLevel")
    {
        live->getPreview().setUnderLevel (static_cast<float> (readParam<double> (params, "level")));
        return { { "level", readParam<double> (params, "level") } };
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

        // Its factory presets, chosen by name with "preset" in a pluginRef.
        auto presets = Json::array();

        if (auto* instance = external->getAudioPluginInstance())
            for (int i = 0; i < instance->getNumPrograms() && i < 500; ++i)
                presets.push_back (instance->getProgramName (i).toStdString());

        result["presets"] = presets;

        if (auto* instance = external->getAudioPluginInstance())
            if (auto names = keyNamesOf (*instance); ! names.empty())
                result["keyNames"] = names;

        plugin = nullptr;
        EditGraveyard::getInstance().bury (std::move (edit));
        result["matchingParameters"] = matching;
        result["parameters"] = parameters;
        result["pluginRef"] = { { "format", PluginCatalog::formatName (*desc) }, { "uid", uid }, { "name", desc->name.toStdString() },
                                { "manufacturer", desc->manufacturerName.toStdString() } };
        return result;
    }

    if (method == "keyNames")
    {
        // The names a channel's plugin gives its keys: from the instance playing, or one loaded
        // for the purpose when nothing plays it.
        const auto channelId = readParam<std::string> (params, "channel");

        if (live != nullptr)
            if (auto external = dynamic_cast<tracktion::ExternalPlugin*> (live->findPlugin ("channel:" + channelId)))
                if (auto* instance = external->getAudioPluginInstance())
                    return { { "keyNames", keyNamesOf (*instance) } };

        const auto& project = session.getProject();
        auto channel = std::find_if (project.channels.begin(), project.channels.end(), [&] (auto& c) { return c.id == channelId; });

        if (channel == project.channels.end())
            throw RpcError (invalidParams, "no channel \"" + channelId + "\"");

        const auto* plugin = std::get_if<model::PluginInstrument> (&channel->instrument);

        if (plugin == nullptr)
            return { { "keyNames", Json::object() } };

        auto desc = plugins->find (plugin->plugin.uid);

        if (! desc)
            return { { "keyNames", Json::object() } };

        auto edit = tracktion::Edit::createSingleTrackEdit (context.getEngine(), tracktion::Edit::forRendering);
        auto loaded = edit->getPluginCache().createNewPlugin (tracktion::ExternalPlugin::xmlTypeName, *desc);
        auto names = Json::object();

        if (auto external = dynamic_cast<tracktion::ExternalPlugin*> (loaded.get()))
            if (auto* instance = external->getAudioPluginInstance())
                names = keyNamesOf (*instance);

        loaded = nullptr;
        EditGraveyard::getInstance().bury (std::move (edit));
        return { { "keyNames", names } };
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

    const auto projectDir = projectDirOf (session);
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

} // namespace gk
