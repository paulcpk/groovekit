#include "LiveEngine.h"

#include "EngineContext.h"
#include "live/AudioDevices.h"
#include "compiler/EditGraveyard.h"
#include "live/MidiClockPlugin.h"
#include "plugins/PluginWindows.h"
#include "mixer/EffectMix.h"
#include "mixer/FaderPlugin.h"
#include "analysis/Analysis.h"
#include "library/SampleFeatures.h"
#include "effects/BuiltinEffects.h"
#include "effects/EffectPlugin.h"
#include "sampler/AudioChannelPlugin.h"
#include "sampler/SamplerPlugin.h"

#include <fstream>

namespace gk
{

namespace te = tracktion;
using namespace model;

namespace
{
    /** Takes quieter than this throughout count as silent. */
    constexpr float silentTakeDb = -50.0f;

    /** Tracktion's default for how often it looks for new MIDI devices. */
    constexpr int defaultMidiScanSeconds = 4;

    /** Hosted plugins by CompiledEdit::plugins key, as the model describes them. */
    std::map<std::string, Json> hostedPluginRefs (const Project& project)
    {
        std::map<std::string, Json> refs;

        for (auto& channel : project.channels)
            if (auto plugin = std::get_if<PluginInstrument> (&channel.instrument))
                refs["channel:" + channel.id] = toJson (plugin->plugin);

        for (auto& insert : project.mixer.inserts)
            for (auto& effect : insert.effects)
                if (auto plugin = std::get_if<PluginEffect> (&effect))
                    refs["effect:" + insert.id + "/" + plugin->id] = toJson (plugin->plugin);

        return refs;
    }

    /** Runs the messages already posted to the message thread (it's first in, first out). */
    void runPostedMessages()
    {
        auto done = std::make_shared<bool> (false);
        juce::MessageManager::callAsync ([done] { *done = true; });

        while (! *done)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
}

LiveEngine::LiveEngine (EngineContext& c, Session& s, std::filesystem::path file)
    : context (c), session (s),
      settingsFile (file.empty() ? EngineContext::getAppDataFolder() / "recording.json" : std::move (file))
{
    loadSettings();
    context.getEngine().getDeviceManager().addChangeListener (this);

    // GrooveKit sends the clock itself (MidiClock); Tracktion's would double it, and it may
    // still be switched on in settings from earlier versions.
    auto& devices = context.getEngine().getDeviceManager();

    for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
        if (auto d = devices.getMidiOutDevice (i); d != nullptr && d->isSendingClock())
            d->setSendingClock (false);

    // Paused while recording (see startRecording); back on if a recording never finished.
    if (devices.getMidiDeviceScanIntervalSeconds() == 0)
        devices.setMidiDeviceScanIntervalSeconds (defaultMidiScanSeconds);

    listenerId = session.addListener ([this] (const Change& change)
    {
        // Mixer moves apply to the playing Edit as they are; anything else rebuilds it.
        if (! needsRebuild && applyToLiveEdit (change))
            return;

        needsRebuild = true;
        triggerAsyncUpdate();
    });

    rebuild();
    startTimerHz (30);
}

LiveEngine::~LiveEngine()
{
    outputWatcher.reset();
    stopTimer();
    session.removeListener (listenerId);
    context.getEngine().getDeviceManager().deviceManager.removeChangeListener (this);
    context.getEngine().getDeviceManager().removeChangeListener (this);

    for (auto& input : capturedInputs)
        if (auto instance = input.get())
            instance->removeConsumer (&capture);

    midiClock->setSource (nullptr);
    midiClock->stopNow();
    context.getEngine().getDeviceManager().deviceManager.removeAudioCallback (midiClock.get());
    context.getEngine().getDeviceManager().deviceManager.removeAudioCallback (&preview);

    if (compiled.edit != nullptr)
        transport().stop (false, true);
}

void LiveEngine::openDefaultAudioDevice()
{
    auto& devices = context.getEngine().getDeviceManager();

    // Once per process: a LiveEngine for another project opened later uses the open device.
    if (devices.deviceManager.getCurrentAudioDeviceType().isEmpty())
        devices.initialise();

    // After Tracktion's own callback, so each cycle's clock is complete when it's sent.
    devices.deviceManager.removeAudioCallback (midiClock.get());
    devices.deviceManager.addAudioCallback (midiClock.get());
    devices.deviceManager.removeAudioCallback (&preview);
    devices.deviceManager.addAudioCallback (&preview);

    useSystemOutput();
    releaseBluetoothMicrophone();
    devices.deviceManager.addChangeListener (this);
    pairStereoInputs();

    // macOS's default output changes when AirPods connect or another output is picked.
    outputWatcher = std::make_unique<DefaultOutputWatcher> ([this]
    {
        useSystemOutput();
        releaseBluetoothMicrophone();
    });

    if (hasAudioDevice())
        transport().ensureContextAllocated();
}

void LiveEngine::releaseBluetoothMicrophone()
{
    auto& devices = context.getEngine().getDeviceManager().deviceManager;

    if (! hasBluetoothInput (devices))
        return;

    auto setup = devices.getAudioDeviceSetup();
    setup.inputDeviceName = {};
    devices.setAudioDeviceSetup (setup, false);
    pairStereoInputs();
}

void LiveEngine::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // Tracktion's device list changed (a MIDI device came or went): it recreates every input,
    // dropping where they played to. Routed again once the playback context has the new ones.
    if (source == &context.getEngine().getDeviceManager())
    {
        needsLiveRouting = true;
        return;
    }

    // E.g. AirPods connected, or a device came back and the system defaults were opened.
    releaseBluetoothMicrophone();

    // A device that went away and nothing to replace it.
    if (outputWatcher != nullptr && context.getEngine().getDeviceManager().deviceManager.getCurrentAudioDevice() == nullptr)
        juce::MessageManager::callAsync ([this, alive = std::weak_ptr<bool> (lifetime)]
        {
            if (! alive.expired())
                useSystemOutput();
        });
}

void LiveEngine::useSystemOutput()
{
    auto& devices = context.getEngine().getDeviceManager().deviceManager;
    auto setup = devices.getAudioDeviceSetup();
    const auto systemOutput = defaultAudioDeviceName (false);
    const bool noDevice = devices.getCurrentAudioDevice() == nullptr;

    if (systemOutput.isEmpty() || (! followSystemOutput && ! noDevice) || (setup.outputDeviceName == systemOutput && ! noDevice))
        return;

    // Not saved as the chosen setup: the choice is "whatever the system uses".
    setup.outputDeviceName = systemOutput;
    std::string notice;

    if (auto error = applyAudioSetup (devices, setup, false, notice); error.isNotEmpty())
        juce::Logger::writeToLog ("Couldn't switch to " + systemOutput + ": " + error);

    pairStereoInputs();

    if (hasAudioDevice() && compiled.edit != nullptr)
        transport().ensureContextAllocated();
}

void LiveEngine::setFollowSystemOutput (bool follow)
{
    followSystemOutput = follow;
    saveSettings();

    if (follow)
        useSystemOutput();
}

//==============================================================================
te::TransportControl& LiveEngine::transport() const
{
    return compiled.edit->getTransport();
}

te::TimePosition LiveEngine::toTime (Ticks t) const
{
    return compiled.edit->tempoSequence.toTime (te::BeatPosition::fromBeats (static_cast<double> (t) / ticksPerQuarter));
}

Ticks LiveEngine::toTicks (te::TimePosition t) const
{
    return static_cast<Ticks> (std::llround (compiled.edit->tempoSequence.toBeats (t).inBeats() * ticksPerQuarter));
}

void LiveEngine::rebuild()
{
    needsRebuild = false;

    const bool wasPlaying = compiled.edit != nullptr && transport().isPlaying();
    auto position = compiled.edit != nullptr ? transport().getPosition() : te::TimePosition();

    // Hosted plugins keep what was done in their windows (which isn't in the model until the
    // window closes) unless the model changed them.
    struct Carried
    {
        juce::MemoryBlock state;
        bool windowOpen = false;
    };

    std::map<std::string, Carried> carried;

    for (auto& [key, plugin] : compiled.plugins)
    {
        if (auto external = dynamic_cast<te::ExternalPlugin*> (plugin); external != nullptr && external->getAudioPluginInstance() != nullptr)
        {
            auto& c = carried[key];
            external->getAudioPluginInstance()->getStateInformation (c.state);
            c.windowOpen = external->windowState->isWindowShowing();
            external->windowState->closeWindowExplicitly();
        }
    }

    // The clock carries on into the new Edit if it plays on from the same place.
    midiClock->setSource (nullptr);

    if (compiled.edit != nullptr)
        transport().stop (false, true);

    const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
    auto next = compileProject (context.getEngine(), projectToPlay(), { projectDir, te::Edit::forEditing, {}, liveChannel });

    // Tracktion puts recordings next to the Edit's file, but only if that folder exists
    // (otherwise they land in the working directory).
    std::filesystem::create_directories (projectDir / "recordings");
    const auto editFile = juce::File ((projectDir / "recordings" / "live.tracktionedit").string());
    next.edit->editFileRetriever = [editFile] { return editFile; };

    compiled = std::move (next);
    addClockTrack();
    compiled.edit->clickTrackEnabled = metronome || clickForCountIn;
    compiled.edit->clickTrackEmphasiseBars = true;

    const auto refs = hostedPluginRefs (session.getProject());

    for (auto& [key, c] : carried)
    {
        const auto before = builtPluginRefs.find (key);
        const auto now = refs.find (key);
        const auto plugin = compiled.plugins.find (key);

        if (before == builtPluginRefs.end() || now == refs.end() || before->second != now->second || plugin == compiled.plugins.end())
            continue;

        if (auto external = dynamic_cast<te::ExternalPlugin*> (plugin->second); external != nullptr && external->getAudioPluginInstance() != nullptr)
        {
            external->getAudioPluginInstance()->setStateInformation (c.state.getData(), static_cast<int> (c.state.getSize()));

            if (c.windowOpen)
                external->windowState->showWindowExplicitly();
        }
    }

    builtPluginRefs = refs;

    auto& tc = transport();

    // A playback context needs a running device (Tracktion divides by its sample rate).
    if (hasAudioDevice())
        tc.ensureContextAllocated();

    // A pattern that got shorter starts over.
    if (auto length = playPatternLength(); length && toTicks (position) >= *length)
        position = {};

    tc.setPosition (position);
    applyLoop();

    if (wasPlaying)
        tc.play (false);

    routeLiveInput();
}

void LiveEngine::addClockTrack()
{
    auto& edit = *compiled.edit;
    auto track = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr, false);
    track->setName ("GrooveKit clock");

    auto plugin = edit.getPluginCache().createNewPlugin (MidiClockPlugin::xmlTypeName, {});
    track->pluginList.insertPlugin (plugin, 0, nullptr);

    if (auto p = dynamic_cast<MidiClockPlugin*> (plugin.get()))
    {
        p->setClock (midiClock);
        clockPlugin = p;
        midiClock->setSource (clockPlugin);
    }
}

void LiveEngine::applyMutes()
{
    auto& project = session.getProject();

    for (auto& clip : project.playlist.clips)
        if (auto it = compiled.clips.find (getClipId (clip)); it != compiled.clips.end())
            for (auto c : it->second)
                c->setMuted (! isClipAudible (project, clip));

    for (auto& channel : project.channels)
    {
        if (auto it = compiled.channelTracks.find (channel.id); it != compiled.channelTracks.end())
            it->second->setMute (! isChannelAudible (project, channel));

        if (auto it = compiled.audioChannelTracks.find (channel.id); it != compiled.audioChannelTracks.end())
            for (auto track : it->second)
                track->setMute (! isChannelAudible (project, channel));
    }

    const auto audible = audibleInserts (project);

    for (auto& insert : project.mixer.inserts)
    {
        const bool muted = insert.mute || ! audible.contains (insert.id);

        if (auto it = compiled.insertTracks.find (insert.id); it != compiled.insertTracks.end())
            it->second->setMute (muted);

        // Levels set directly: the send's own mute brings back whatever level it last had.
        for (auto& send : insert.sends)
            if (auto it = compiled.sends.find (insert.id + "/" + send.id); it != compiled.sends.end())
                it->second->setGainDb (muted ? -100.0f : static_cast<float> (send.levelDb));
    }
}

bool LiveEngine::applyToLiveEdit (const Change& change)
{
    if (compiled.edit == nullptr || change.ops.empty())
        return false;

    auto& project = session.getProject();
    std::vector<std::function<void()>> actions;
    bool mutesQueued = false;

    auto index = [] (const std::string& s) -> std::optional<size_t>
    {
        if (s.empty() || ! std::all_of (s.begin(), s.end(), [] (char c) { return std::isdigit (static_cast<unsigned char> (c)); }))
            return {};
        return std::stoul (s);
    };

    for (auto& op : change.ops)
    {
        std::vector<std::string> keys;

        for (size_t start = 1; start <= op.path.size();)
        {
            const auto end = std::min (op.path.find ('/', start), op.path.size());
            keys.push_back (op.path.substr (start, end - start));
            start = end + 1;
        }

        // Mute and solo of playlist rows, clips, channels and inserts: everything is switched
        // on or off where it is (one solo changes who else is heard), without rebuilding.
        const auto isFlag = [] (const std::string& k) { return k == "mute" || k == "solo"; };

        // Automation isn't a clip there but curves on its parameters: a row's mute or solo, or an
        // automation clip's mute, rebuilds them.
        const auto automationSwitched = [&]
        {
            if (keys.size() != 4 || keys[0] != "playlist" || ! isFlag (keys[3]))
                return false;

            auto& clips = project.playlist.clips;
            const auto isAutomation = [] (const Clip& c) { return std::holds_alternative<AutomationClip> (c); };

            if (keys[1] == "rows")
                return std::any_of (clips.begin(), clips.end(), isAutomation);

            const auto i = index (keys[2]);
            return keys[1] == "clips" && i && *i < clips.size() && isAutomation (clips[*i]);
        };

        if (automationSwitched())
            return false;

        if ((keys.size() == 4 && keys[0] == "playlist" && (keys[1] == "rows" || keys[1] == "clips") && isFlag (keys[3]))
            || (keys.size() == 3 && keys[0] == "channels" && isFlag (keys[2]))
            || (keys.size() == 4 && keys[0] == "mixer" && keys[1] == "inserts" && isFlag (keys[3])))
        {
            if (! mutesQueued)
                actions.push_back ([this] { applyMutes(); });

            mutesQueued = true;
            continue;
        }

        // /channels/<i>/volumeDb or /pan
        if (keys.size() == 3 && keys[0] == "channels" && (keys[2] == "volumeDb" || keys[2] == "pan"))
        {
            const auto i = index (keys[1]);
            if (! i || *i >= project.channels.size()) return false;
            const auto& channel = project.channels[*i];
            std::vector<mixer::FaderPlugin*> faders;

            if (auto it = compiled.channelFaders.find (channel.id); it != compiled.channelFaders.end())
                faders.push_back (it->second);

            if (auto it = compiled.audioChannelFaders.find (channel.id); it != compiled.audioChannelFaders.end())
                faders.insert (faders.end(), it->second.begin(), it->second.end());

            for (auto fader : faders)
                actions.push_back ([fader, v = channel.volumeDb, p = channel.pan] { fader->setVolumeDb (static_cast<float> (v)); fader->setPan (static_cast<float> (p)); });

            continue;
        }

        // What only the UI shows: names, colours, which rack list a channel is in, the song's key.
        if ((keys.size() == 1 && (keys[0] == "key" || keys[0] == "name" || keys[0] == "style")) || (keys.size() == 3 && keys[0] == "channels" && (keys[2] == "name" || keys[2] == "color" || keys[2] == "group"))
            || (keys.size() == 4 && keys[0] == "playlist" && keys[1] == "rows" && (keys[3] == "name" || keys[3] == "color"))
            || (keys.size() == 3 && keys[0] == "playlist" && keys[1] == "rows" && op.op == PatchOpOp::add && op.value && op.value->empty()))
            continue;

        // /channels/<i>/instrument/...: a sampler takes its new settings while it plays.
        if (keys.size() >= 4 && keys[0] == "channels" && keys[2] == "instrument")
        {
            const auto i = index (keys[1]);
            if (! i || *i >= project.channels.size()) return false;
            const auto& channel = project.channels[*i];
            auto sampler = std::get_if<SamplerInstrument> (&channel.instrument);
            if (sampler == nullptr) return false;

            // Its audio clips play a prepared copy of the first layer's part of the file: a change
            // to which part, or how it's stretched or tuned, rebuilds; the rest is shaped live.
            if (auto it = compiled.audioChannelPlugins.find (channel.id); it != compiled.audioChannelPlugins.end())
            {
                static const std::set<std::string> prepared { "sample", "start", "end", "reverse", "transpose", "fineTune", "sourceTempo", "resample" };

                if (keys[3] == "layers" && (keys.size() < 6 || (keys[4] == "0" && prepared.contains (keys[5]))))
                    return false;

                for (auto plugin : it->second)
                    actions.push_back ([plugin, instrument = *sampler] { plugin->updateInstrument (instrument); });
            }

            const auto it = compiled.plugins.find ("channel:" + channel.id);

            // A sampler that isn't loaded plays no notes yet: adding some rebuilds.
            if (it == compiled.plugins.end())
                continue;

            auto plugin = dynamic_cast<sampler::SamplerPlugin*> (it->second);
            if (plugin == nullptr) return false;
            const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
            actions.push_back ([plugin, instrument = *sampler, projectDir] { plugin->setInstrument (instrument, projectDir); });
            continue;
        }

        // Notes and pattern clips change the MIDI clips that play them, not the whole Edit. In
        // pattern mode the song's clips don't play (only the pattern-mode clip does), and a
        // pattern that changes length moves that clip's end: a rebuild.
        if (keys.size() >= 3 && keys[0] == "patterns" && (keys[2] == "notes" || keys[2] == "length"))
        {
            const auto i = index (keys[1]);
            if (! i || *i >= project.patterns.size()) return false;
            if (playPattern && keys[2] == "length") return false;
            const auto& patternId = project.patterns[*i].id;

            for (auto& clip : projectToPlay().playlist.clips)
                if (auto pc = std::get_if<PatternClip> (&clip); pc != nullptr && pc->pattern == patternId && ! canFillPatternClip (*pc))
                    return false;

            actions.push_back ([this, patternId]
            {
                for (auto& clip : projectToPlay().playlist.clips)
                    if (auto pc = std::get_if<PatternClip> (&clip); pc != nullptr && pc->pattern == patternId)
                        fillPatternClip (*pc);
            });

            if (! mutesQueued)
                actions.push_back ([this] { applyMutes(); });

            mutesQueued = true;
            continue;
        }

        if (keys.size() == 1 && keys[0] == "swing")
        {
            actions.push_back ([this]
            {
                for (auto& clip : projectToPlay().playlist.clips)
                    if (auto pc = std::get_if<PatternClip> (&clip))
                        fillPatternClip (*pc);
            });
            continue;
        }

        if (keys.size() >= 3 && keys[0] == "playlist" && keys[1] == "clips")
        {
            // In pattern mode the song's clips aren't in the Edit; they're rebuilt when it plays again.
            if (playPattern)
                continue;

            const auto i = index (keys[2]);

            // A pattern clip's position (start, length, offset): moved and filled again; a row
            // change can only change its mute. Audio and automation clips rebuild.
            if (keys.size() == 4 && i && *i < project.playlist.clips.size())
            {
                auto pc = std::get_if<PatternClip> (&project.playlist.clips[*i]);
                if (pc == nullptr) return false;

                if (keys[3] == "row")
                {
                    if (! mutesQueued)
                        actions.push_back ([this] { applyMutes(); });

                    mutesQueued = true;
                    continue;
                }

                if (keys[3] != "start" && keys[3] != "length" && keys[3] != "offset") return false;
                if (! canFillPatternClip (*pc)) return false;
                actions.push_back ([this, clip = *pc] { fillPatternClip (clip); });
                continue;
            }

            // A clip added or removed (whole).
            if (keys.size() == 3 && (keys[2] == "-" || i))
            {
                if (op.op == PatchOpOp::remove)
                {
                    actions.push_back ([this]
                    {
                        std::set<Id> playing;

                        for (auto& clip : session.getProject().playlist.clips)
                            playing.insert (getClipId (clip));

                        std::vector<Id> gone;

                        for (auto& [id, clips] : compiled.clips)
                            if (! playing.contains (id))
                                gone.push_back (id);

                        for (auto& id : gone)
                            removePlaylistClip (id);
                    });
                    continue;
                }

                if (op.op != PatchOpOp::add) return false;
                const auto at = keys[2] == "-" ? project.playlist.clips.size() - 1 : *i;
                if (at >= project.playlist.clips.size()) return false;
                auto pc = std::get_if<PatternClip> (&project.playlist.clips[at]);
                if (pc == nullptr || ! canFillPatternClip (*pc)) return false;
                actions.push_back ([this, clip = *pc] { fillPatternClip (clip); });

                if (! mutesQueued)
                    actions.push_back ([this] { applyMutes(); });

                mutesQueued = true;
                continue;
            }

            return false;
        }

        if (keys.size() < 4 || keys[0] != "mixer" || keys[1] != "inserts")
            return false;

        const auto i = index (keys[2]);
        if (! i || *i >= project.mixer.inserts.size()) return false;
        const auto& insert = project.mixer.inserts[*i];

        // /mixer/inserts/<i>/volumeDb or /pan
        if (keys.size() == 4 && (keys[3] == "volumeDb" || keys[3] == "pan"))
        {
            const auto it = compiled.insertFaders.find (insert.id);
            if (it == compiled.insertFaders.end()) return false;
            actions.push_back ([fader = it->second, v = insert.volumeDb, p = insert.pan] { fader->setVolumeDb (static_cast<float> (v)); fader->setPan (static_cast<float> (p)); });
            continue;
        }

        // /mixer/inserts/<i>/sends/<j>/levelDb
        if (keys.size() == 6 && keys[3] == "sends" && keys[5] == "levelDb")
        {
            const auto j = index (keys[4]);
            if (! j || *j >= insert.sends.size() || insert.mute) return false;
            const auto it = compiled.sends.find (insert.id + "/" + insert.sends[*j].id);
            if (it == compiled.sends.end()) return false;
            actions.push_back ([aux = it->second, v = insert.sends[*j].levelDb] { aux->setGainDb (static_cast<float> (v)); });
            continue;
        }

        // /mixer/inserts/<i>/effects/<j>/mix
        if (keys.size() == 6 && keys[3] == "effects" && keys[5] == "mix")
        {
            const auto j = index (keys[4]);
            if (! j || *j >= insert.effects.size()) return false;
            const auto& effect = insert.effects[*j];
            const auto it = compiled.effectMixes.find (insert.id + "/" + getEffectId (effect));
            if (it == compiled.effectMixes.end()) return false;
            actions.push_back ([blend = it->second, v = std::visit ([] (auto& e) { return e.mix; }, effect)] { blend->setMix (static_cast<float> (v)); });
            continue;
        }

        // /mixer/inserts/<i>/effects/<j>/params[/<p>] or /bypass
        if (keys.size() >= 6 && keys[3] == "effects" && (keys[5] == "params" || (keys[5] == "bypass" && keys.size() == 6)))
        {
            const auto j = index (keys[4]);
            if (! j || *j >= insert.effects.size()) return false;
            const auto& effect = insert.effects[*j];
            const auto id = std::visit ([] (auto& e) { return e.id; }, effect);
            const auto it = compiled.plugins.find ("effect:" + insert.id + "/" + id);
            if (it == compiled.plugins.end()) return false;

            if (auto builtin = std::get_if<BuiltinEffect> (&effect))
                actions.push_back ([plugin = it->second, e = *builtin] { effects::applyParams (*plugin, e); });
            else if (keys[5] == "bypass")
                actions.push_back ([plugin = it->second, b = std::get<PluginEffect> (effect).bypass] { plugin->setEnabled (! b); });
            else
                return false;

            continue;
        }

        return false;
    }

    try
    {
        for (auto& a : actions)
            a();
    }
    catch (const std::exception&)
    {
        return false;
    }

    return true;
}

bool LiveEngine::canFillPatternClip (const PatternClip& pc) const
{
    auto pattern = findPattern (session.getProject(), pc.pattern);

    if (pattern == nullptr)
        return true;

    // A channel that played no notes has no track (and its sampler isn't loaded): a rebuild.
    for (auto& note : pattern->notes)
        if (! compiled.channelTracks.contains (note.channel))
            return false;

    return true;
}

void LiveEngine::fillPatternClip (const PatternClip& pc)
{
    auto& project = session.getProject();
    auto pattern = findPattern (project, pc.pattern);
    auto& clips = compiled.clips[pc.id];
    std::set<Id> channels;

    if (pattern != nullptr)
        for (auto& note : pattern->notes)
            channels.insert (note.channel);

    // The clip for each channel is named "<clip>:<channel>" (ids hold no colons).
    const auto channelOf = [&] (const te::Clip& c) { return c.getName().toStdString().substr (pc.id.size() + 1); };
    const te::ClipPosition position { { toTime (pc.start), toTime (pc.start + pc.length) }, {} };

    for (auto it = clips.begin(); it != clips.end();)
    {
        auto* clip = *it;
        const auto channel = channelOf (*clip);

        if (! channels.contains (channel))
        {
            clip->removeFromParent();
            it = clips.erase (it);
            continue;
        }

        if (auto midiClip = dynamic_cast<te::MidiClip*> (clip))
        {
            midiClip->setPosition (position);
            gk::fillPatternClip (*midiClip, project, *pattern, pc, channel);
        }

        channels.erase (channel);
        ++it;
    }

    for (auto& channel : channels)
    {
        auto track = compiled.channelTracks.find (channel);

        if (track == compiled.channelTracks.end())
            continue;

        auto midiClip = track->second->insertMIDIClip (pc.id + ":" + channel, position.time, nullptr);

        if (midiClip == nullptr)
            continue;

        gk::fillPatternClip (*midiClip, project, *pattern, pc, channel);
        midiClip->setMuted (! isClipAudible (project, Clip { pc }));
        clips.push_back (midiClip.get());
    }
}

void LiveEngine::removePlaylistClip (const Id& id)
{
    auto it = compiled.clips.find (id);

    if (it == compiled.clips.end())
        return;

    for (auto clip : it->second)
        clip->removeFromParent();

    compiled.clips.erase (it);
}

std::vector<float> LiveEngine::readEffectOutput (const std::string& insert, const std::string& effect, int numSamples) const
{
    if (auto it = compiled.effectMixes.find (insert + "/" + effect); it != compiled.effectMixes.end())
        return it->second->readScope (numSamples);

    return {};
}

model::Json LiveEngine::readEffectMeter (const std::string& insert, const std::string& effect, const model::Json& params) const
{
    if (auto plugin = dynamic_cast<const effects::EffectPlugin*> (findPlugin ("effect:" + insert + "/" + effect)))
        return plugin->readMeter (params);

    return model::Json::object();
}

double LiveEngine::getSampleRate() const
{
    return context.getEngine().getDeviceManager().getSampleRate();
}

std::string LiveEngine::findPluginKey (const te::Plugin& plugin) const
{
    for (auto& [key, p] : compiled.plugins)
        if (p == &plugin)
            return key;

    return {};
}

bool LiveEngine::showPluginWindow (const std::string& key, bool show)
{
    updateNow();
    auto external = dynamic_cast<te::ExternalPlugin*> (findPlugin (key));

    if (external == nullptr || external->getAudioPluginInstance() == nullptr)
        return false;

    if (show)
    {
        external->windowState->showWindowExplicitly();

        // A window already open may be behind the UI's, which is another app's.
        if (auto* window = external->windowState->pluginWindow.get())
            PluginWindowBehaviour::bringToFront (*window);
    }
    else
        external->windowState->closeWindowExplicitly();

    return true;
}

void LiveEngine::updateNow()
{
    cancelPendingUpdate();
    handleAsyncUpdate();
}

void LiveEngine::handleAsyncUpdate()
{
    // Rebuilding would throw away a recording in progress; catch up when it stops.
    if (needsRebuild && recordings.empty())
        rebuild();
}

//==============================================================================
bool LiveEngine::hasAudioDevice() const
{
    auto& devices = context.getEngine().getDeviceManager();
    return devices.isHostedAudioDeviceInterfaceInUse() || devices.deviceManager.getCurrentAudioDevice() != nullptr;
}

void LiveEngine::requireAudioDevice()
{
    if (! hasAudioDevice())
        throw std::runtime_error ("No audio device is open");

    transport().ensureContextAllocated();
}

void LiveEngine::play()
{
    requireAudioDevice();
    transport().play (false);
    notifyTransport (true);
}

void LiveEngine::stop()
{
    if (! recordings.empty())
    {
        stopRecording();
        return;
    }

    if (noteRecording)
    {
        finishNoteRecording();
        return;
    }

    stopTransport();
    notifyTransport (true);
}

void LiveEngine::stopTransport()
{
    // Tracktion clears the playback graph when it stops, so the clock plugin never sees a stopped
    // block; the clock is stopped here instead, ignoring any block still on its way.
    midiClock->setSource (nullptr);
    transport().stop (false, true);
    midiClock->stopNow();
    midiClock->setSource (clockPlugin);
}

bool LiveEngine::isPlaying() const      { return transport().isPlaying(); }
bool LiveEngine::isRecording() const    { return transport().isRecording(); }

void LiveEngine::setPosition (Ticks t)
{
    transport().setPosition (toTime (t));
    notifyTransport (true);
}

Ticks LiveEngine::getPosition() const
{
    return toTicks (transport().getPosition());
}

void LiveEngine::setLoop (std::optional<std::pair<Ticks, Ticks>> range)
{
    if (range && range->second <= range->first)
        throw std::runtime_error ("The loop must end after it starts");

    songLoop = range;
    applyLoop();
}

void LiveEngine::applyLoop()
{
    auto& tc = transport();

    if (playPattern)
    {
        const auto length = playPatternLength();

        if (length)
            tc.setLoopRange ({ toTime (0), toTime (*length) });

        tc.looping = length.has_value();
        return;
    }

    if (songLoop)
        tc.setLoopRange ({ toTime (songLoop->first), toTime (songLoop->second) });

    tc.looping = songLoop.has_value();
}

void LiveEngine::setPlayPattern (std::optional<Id> pattern)
{
    if (pattern)
    {
        auto& patterns = session.getProject().patterns;

        if (std::none_of (patterns.begin(), patterns.end(), [&] (auto& p) { return p.id == *pattern; }))
            throw std::runtime_error ("No pattern \"" + *pattern + "\"");
    }

    if (pattern == playPattern)
        return;

    if (! recordings.empty())
        throw std::runtime_error ("Can't switch to pattern mode while recording");

    playPattern = pattern;
    rebuild();
    transport().setPosition (toTime (0));
    notifyTransport (true);
}

std::optional<TickLength> LiveEngine::playPatternLength() const
{
    if (! playPattern)
        return {};

    auto& project = session.getProject();

    for (auto& p : project.patterns)
        if (p.id == *playPattern)
            return getPatternLength (p, project.timeSignature);

    return {};
}

Project LiveEngine::projectToPlay() const
{
    if (! playPattern)
        return session.getProject();

    // The song's channels and mixer, with a playlist holding only the pattern.
    auto project = session.getProject();
    project.playlist = {};

    if (auto length = playPatternLength())
    {
        project.playlist.clips.push_back (PatternClip { .id = "pattern-mode", .row = 0, .start = 0,
                                                        .length = *length, .pattern = *playPattern });
    }

    return project;
}

//==============================================================================
namespace
{
    /** "Track 1 L + Track 1 R" -> "Track 1"; other names stay as they are. */
    std::string friendlyInputName (const juce::String& name)
    {
        const auto left = name.upToFirstOccurrenceOf (" + ", false, false);
        const auto right = name.fromFirstOccurrenceOf (" + ", false, false);

        if (left.endsWith (" L") && right.endsWith (" R") && left.dropLastCharacters (2) == right.dropLastCharacters (2))
            return left.dropLastCharacters (2).toStdString();

        return name.toStdString();
    }
}

std::vector<std::string> LiveEngine::getInputNames() const
{
    std::vector<std::string> names;

    for (auto device : context.getEngine().getDeviceManager().getWaveInputDevices())
        names.push_back (friendlyInputName (device->getName()));

    return names;
}

std::vector<LiveEngine::MidiPort> LiveEngine::getMidiOutputs() const
{
    std::vector<MidiPort> ports;
    auto& devices = context.getEngine().getDeviceManager();

    for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
        if (auto d = devices.getMidiOutDevice (i))
            ports.push_back ({ d->getName().toStdString(), d->isEnabled(), midiClock->isSendingTo (d->getName().toStdString()) });

    return ports;
}

std::vector<std::string> LiveEngine::getMidiInputs() const
{
    std::vector<std::string> names;

    // Not the app's own keyboard, which is always on.
    for (auto& d : context.getEngine().getDeviceManager().getMidiInDevices())
        if (dynamic_cast<te::VirtualMidiInputDevice*> (d.get()) == nullptr)
            names.push_back (d->getName().toStdString());

    return names;
}

bool LiveEngine::isMidiInputEnabled (const std::string& name) const
{
    for (auto& d : context.getEngine().getDeviceManager().getMidiInDevices())
        if (d->getName().toStdString() == name)
            return d->isEnabled();

    return false;
}

void LiveEngine::setMidiInputEnabled (const std::string& name, bool enabled)
{
    for (auto& d : context.getEngine().getDeviceManager().getMidiInDevices())
    {
        if (d->getName().toStdString() == name)
        {
            d->setEnabled (enabled);
            context.getEngine().getDeviceManager().dispatchPendingUpdates();
            needsLiveRouting = true;
            return;
        }
    }

    throw std::runtime_error ("No MIDI input \"" + name + "\"");
}

void LiveEngine::setMidiClock (const std::string& output, bool enabled)
{
    auto& devices = context.getEngine().getDeviceManager();

    for (int i = 0; i < devices.getNumMidiOutDevices(); ++i)
    {
        if (auto d = devices.getMidiOutDevice (i); d != nullptr && d->getName().toStdString() == output)
        {
            if (d->isSendingClock())
                d->setSendingClock (false);

            if (! midiClock->setOutput (output, enabled))
                throw std::runtime_error ("Can't open MIDI output \"" + output + "\"");

            return;
        }
    }

    throw std::runtime_error ("No MIDI output \"" + output + "\"");
}

void LiveEngine::pairStereoInputs()
{
    auto& devices = context.getEngine().getDeviceManager();
    auto device = devices.deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    const auto names = device->getInputChannelNames();

    for (int ch = 0; ch + 1 < names.size(); ch += 2)
    {
        const auto& left = names[ch];
        const auto& right = names[ch + 1];
        const bool pair = left.endsWith (" L") && right.endsWith (" R")
                       && left.dropLastCharacters (2) == right.dropLastCharacters (2);

        devices.setDeviceInChannelStereo (ch, pair);
    }

    devices.dispatchPendingUpdates();

    // Tracktion drops its estimated device latency from the start of every take; GrooveKit keeps
    // that as it is (its recorder can't do without it) and aligns hardware with calibrateAlignment.
    for (auto input : devices.getWaveInputDevices())
        if (input->getRecordAdjustmentMs() != 0)
            input->setRecordAdjustmentMs (0);

    needsRebuild = true;
    updateNow();
}

void LiveEngine::startRecording (const std::vector<RecordTarget>& targets, const RecordOptions& options)
{
    if (targets.empty())
        throw std::runtime_error ("Nothing to record");

    if (! recordings.empty())
        throw std::runtime_error ("Already recording");

    // Takes belong to the song.
    if (playPattern)
        setPlayPattern ({});

    updateNow();
    requireAudioDevice();

    auto& project = session.getProject();
    auto& edit = *compiled.edit;
    auto playbackContext = edit.getCurrentPlaybackContext();

    if (playbackContext == nullptr)
        throw std::runtime_error ("No playback context");

    for (auto& t : targets)
    {
        if (findInsert (project, t.insert) == nullptr)
            throw std::runtime_error ("No mixer insert \"" + t.insert + "\"");

        te::InputDeviceInstance* input = nullptr;

        for (auto instance : playbackContext->getAllInputs())
        {
            const auto name = instance->getInputDevice().getName();

            if (name.toStdString() == t.input || friendlyInputName (name) == t.input)
                input = instance;
        }

        if (input == nullptr)
            throw std::runtime_error ("No input \"" + t.input + "\"");

        auto track = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr, false);
        // Recorded files are named "_<track name>_Take_<n>.wav".
        track->setName (juce::String ("track" + std::to_string (t.row + 1) + "-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S").toStdString()));
        track->getOutput().setOutputToTrack (compiled.insertTracks.at (t.insert));

        auto destination = input->setTarget (track->itemID, true, nullptr);

        if (! destination)
            throw std::runtime_error ("Can't record from \"" + t.input + "\": " + destination.error().toStdString());

        (*destination)->recordEnabled = true;
        recordings.push_back ({ t, track.get() });
    }

    edit.dispatchPendingUpdatesSynchronously();

    // Arming an input queues Tracktion's update of its recording state. Running once the
    // transport records, that update stops the takes of the inputs it sees as changed, so it
    // has to happen first.
    runPostedMessages();

    recordOptions = options;
    recordStart = getPosition();

    // The count-in plays (with the click) from bars before; takes are trimmed to start here.
    if (countInBars > 0)
    {
        const auto from = std::max<Ticks> (0, recordStart - countInBars * ticksPerBar (project.timeSignature));
        transport().setPosition (toTime (from));

        if (from < recordStart)
        {
            clickForCountIn = true;
            edit.clickTrackEnabled = true;
        }
    }

    EditGraveyard::getInstance().hold();

    // When the MIDI device list changes, Tracktion detaches and reattaches every device, which
    // cuts takes short. Its periodic check for changes waits until the recording is done.
    context.getEngine().getDeviceManager().setMidiDeviceScanIntervalSeconds (0);

    midiClock->setRecording (true);
    transport().record (false);
    notifyTransport (true);
}

std::vector<Id> LiveEngine::stopRecording (const ExecuteOptions& options)
{
    stopTransport();
    midiClock->setRecording (false);

    if (! recordings.empty())
    {
        EditGraveyard::getInstance().release();
        context.getEngine().getDeviceManager().setMidiDeviceScanIntervalSeconds (defaultMidiScanSeconds);
    }

    auto& project = session.getProject();
    const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
    std::vector<Command> commands;
    std::vector<Id> ids;
    std::set<Id> used;
    lastTakes.clear();

    for (auto& clip : project.playlist.clips)
        used.insert (getClipId (clip));

    // Each take gets a sampler channel (with the same id as its clip), listed with the arrangement's.
    for (auto& channel : project.channels)
        used.insert (channel.id);

    for (auto& r : recordings)
    {
        for (auto clip : r.track->getClips())
        {
            auto wave = dynamic_cast<te::WaveAudioClip*> (clip);

            if (wave == nullptr)
                continue;

            // Canonical on both sides, so /var/... and /private/var/... still relate.
            auto file = std::filesystem::weakly_canonical (wave->getSourceFileReference().getFile().getFullPathName().toStdString());
            auto relative = file.lexically_relative (std::filesystem::weakly_canonical (projectDir));
            const auto position = wave->getPosition();
            lastTakes.push_back ({ file, position.getStart().inSeconds(), position.getOffset().inSeconds() });

            if (! recordOptions.addClips)
                continue;

            Id id;

            for (int n = 1; id.empty() || used.contains (id); ++n)
                id = "rec-" + std::to_string (r.target.row + 1) + "-" + std::to_string (n);

            used.insert (id);

            std::optional<analysis::Audio> audio;

            if (recordOptions.dropSilent || recordOptions.normalizeLufs)
                audio = analysis::loadAudio (file);

            if (recordOptions.dropSilent && analysis::peakDb (*audio) < silentTakeDb)
                continue;

            // The device's measured round trip moves the take onto the grid. Takes usually arrive
            // late, so they move earlier; one that would start before the song skips into its file.
            auto start = position.getStart().inSeconds() + getAlignmentMs() / 1000.0;
            const auto end = position.getEnd().inSeconds() + getAlignmentMs() / 1000.0;
            auto offset = position.getOffset().inSeconds();

            // What was played during the count-in isn't part of the take.
            if (const auto from = toTime (recordStart).inSeconds(); start < from)
            {
                offset += from - start;
                start = from;
            }

            if (start < 0)
            {
                offset -= start;
                start = 0;
            }

            AudioClip ac;
            ac.id = id;
            ac.row = r.target.row;
            ac.start = toTicks (te::TimePosition::fromSeconds (start));
            ac.length = std::max<Ticks> (1, toTicks (te::TimePosition::fromSeconds (end)) - ac.start);

            if (recordOptions.length)
                ac.length = std::max<Ticks> (1, std::min (ac.length, recordStart + *recordOptions.length - ac.start));

            SamplerLayer layer;
            layer.id = "sample";
            layer.sample = relative.empty() || relative.string().starts_with ("..") ? file.string() : relative.string();

            if (recordOptions.normalizeLufs)
                if (const auto lufs = analysis::integratedLufs (*audio); lufs > analysis::silenceDb)
                    layer.gainDb = std::round (std::clamp (*recordOptions.normalizeLufs - lufs, -24.0, 24.0) * 10.0) / 10.0;

            Channel channel;
            channel.id = id;
            channel.insert = r.target.insert;
            channel.group = ChannelGroup::arrangement;
            channel.instrument = SamplerInstrument { .layers = { layer } };

            if (auto track = findRow (project, r.target.row); track != nullptr && ! track->name.empty())
                channel.name = track->name + " " + id.substr (id.rfind ('-') + 1);
            else
                channel.name = "Take " + id.substr (4);

            commands.push_back (AddChannel { channel, {} });

            ac.channel = id;
            ac.offset = offset;
            commands.push_back (AddClip { ac });
            ids.push_back (id);
        }
    }

    recordings.clear();

    if (! commands.empty())
    {
        auto opts = options;

        if (! opts.label)
            opts.label = "Record " + std::to_string (commands.size()) + (commands.size() == 1 ? " take" : " takes");

        session.execute (commands, opts);
    }

    updateNow();
    notifyTransport (true);
    return ids;
}

LiveEngine::CalibrationResult LiveEngine::calibrateAlignment (const std::string& input, int bars)
{
    if (bars < 1)
        throw std::runtime_error ("Calibrate over at least one bar");

    LiveRecordOptions options;
    options.addClips = false;
    options.length = bars * ticksPerBar (session.getProject().timeSignature);

    setPosition (0);
    startRecording ({ { input, masterInsertId, {} } }, options);

    // Pump messages until the timed recording stops itself.
    for (auto start = juce::Time::getMillisecondCounter(); ! recordings.empty();)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        if (juce::Time::getMillisecondCounter() - start > static_cast<juce::uint32> (bars * 20000 + 5000))
        {
            stopRecording();
            throw std::runtime_error ("Calibration recording didn't finish");
        }
    }

    if (lastTakes.empty())
        throw std::runtime_error ("Nothing was recorded from \"" + input + "\"");

    auto& take = lastTakes.front();
    const auto audio = analysis::loadAudio (take.file);

    // Clean hits (a kick) give exact onsets; fall back to transients for denser material.
    auto hits = analysis::detectOnsets (audio, -40.0f, 0.05);

    if (hits.size() < 4)
        hits = library::detectTransients (audio);

    const auto step = 60.0 / session.getProject().tempo / 4.0; // a 16th note
    std::vector<double> offsets;

    for (auto t : hits)
    {
        const auto when = take.startSeconds + t - take.offsetSeconds;
        offsets.push_back (when - std::round (when / step) * step);
    }

    if (offsets.size() < 4)
        throw std::runtime_error ("Too few hits to calibrate (" + std::to_string (offsets.size()) + "); is \"" + input + "\" playing in time?");

    auto median = [] (std::vector<double> v)
    {
        std::nth_element (v.begin(), v.begin() + static_cast<std::ptrdiff_t> (v.size() / 2), v.end());
        return v[v.size() / 2];
    };

    const auto offset = median (offsets);
    std::vector<double> deviations;

    for (auto o : offsets)
        deviations.push_back (std::abs (o - offset));

    // The measurement is of raw takes, so it replaces the device's previous alignment.
    setAlignmentMs (-offset * 1000.0);

    return { getAlignmentMs(), median (deviations) * 1000.0, static_cast<int> (offsets.size()), currentInputDevice() };
}

std::string LiveEngine::currentInputDevice() const
{
    return context.getEngine().getDeviceManager().deviceManager.getAudioDeviceSetup().inputDeviceName.toStdString();
}

double LiveEngine::getAlignmentMs() const
{
    auto it = alignmentMs.find (currentInputDevice());
    return it != alignmentMs.end() ? it->second : 0.0;
}

void LiveEngine::setAlignmentMs (double ms)
{
    alignmentMs[currentInputDevice()] = ms;
    saveSettings();
}

void LiveEngine::loadSettings()
{
    std::ifstream in (settingsFile);

    if (! in)
        return;

    try
    {
        const auto j = Json::parse (in);
        const auto devices = j.value ("alignmentMs", Json::object()); // a copy: items() mustn't outlive it

        for (auto& [device, ms] : devices.items())
            alignmentMs[device] = ms.get<double>();

        metronome = j.value ("metronome", false);
        followSystemOutput = j.value ("followSystemOutput", true);
        countInBars = std::clamp (j.value ("countInBars", 0), 0, 2);

    }
    catch (const std::exception&)
    {
        // A damaged file just means no alignment until the next calibration.
    }
}

void LiveEngine::saveSettings() const
{
    std::filesystem::create_directories (settingsFile.parent_path());
    std::ofstream (settingsFile) << Json { { "alignmentMs", alignmentMs }, { "metronome", metronome }, { "countInBars", countInBars },
                                    { "followSystemOutput", followSystemOutput } }.dump (2) << "\n";
}

tracktion::Plugin* LiveEngine::findPlugin (const std::string& key) const
{
    auto it = compiled.plugins.find (key);
    return it != compiled.plugins.end() ? it->second : nullptr;
}

//==============================================================================
LiveEngine::TransportState LiveEngine::getTransportState() const
{
    TransportState s;
    s.playing = transport().isPlaying();
    s.recording = transport().isRecording();
    s.position = getPosition();
    s.seconds = transport().getPosition().inSeconds();
    s.pattern = playPattern;
    s.recording = s.recording || noteRecording.has_value();
    s.metronome = metronome;
    s.countInBars = countInBars;
    s.time = static_cast<double> (juce::Time::currentTimeMillis());

    // While playing, the audio thread's own position (updated every block) rather than the
    // transport's copy, which Tracktion refreshes on a timer: clients move on from it by s.time.
    if (s.playing)
        if (auto context = compiled.edit->getCurrentPlaybackContext())
        {
            const auto audible = context->getAudibleTimelineTime();
            s.position = toTicks (audible);
            s.seconds = audible.inSeconds();
        }

    if (auto device = context.getEngine().getDeviceManager().deviceManager.getCurrentAudioDevice(); device != nullptr && device->getCurrentSampleRate() > 0)
        s.outputLatencyMs = (device->getOutputLatencyInSamples() + device->getCurrentBufferSizeSamples()) * 1000.0 / device->getCurrentSampleRate();

    return s;
}

void LiveEngine::timerCallback()
{
    session.flushSave();

    // Timed recordings stop on their own; the takes are trimmed to the exact length.
    if (! recordings.empty() && recordOptions.length && getPosition() >= recordStart + *recordOptions.length)
        stopRecording();

    takeCapturedNotes();

    // The app's keyboard appears a moment after it's first created, and inputs are recreated
    // when devices change.
    if (! needsLiveRouting)
        for (auto& input : capturedInputs)
            if (input.get() == nullptr)
                needsLiveRouting = true;

    if (needsLiveRouting)
        routeLiveInput();

    // The click that counted in goes back to the metronome setting once the count-in is over.
    if (clickForCountIn && compiled.edit != nullptr)
    {
        const bool over = noteRecording ? juce::Time::getMillisecondCounterHiRes() >= noteRecording->fromMs
                                        : recordings.empty() || getPosition() >= recordStart;

        if (over)
        {
            clickForCountIn = false;
            compiled.edit->clickTrackEnabled = metronome;
        }
    }

    stopAfterSongEnd();
    notifyTransport (false);
    reportLevels();
}

void LiveEngine::stopAfterSongEnd()
{
    const auto position = getPosition();
    const auto previous = std::exchange (lastPosition, position);

    if (! isPlaying() || playPattern || songLoop || ! recordings.empty() || noteRecording)
        return;

    const auto& project = session.getProject();
    const auto end = getSongEnd (project);

    if (end <= 0)
        return;

    // Only when playing goes by it: started further on, or moved there, it plays on.
    const auto bar = ticksPerBar (project.timeSignature);
    const auto stopAt = end + barsAfterSongEnd * bar;

    if (position >= stopAt && previous < stopAt && position - previous < bar)
    {
        stopTransport();
        transport().setPosition (toTime (0));
        lastPosition = 0;
        notifyTransport (true);
    }
}

void LiveEngine::reportLevels()
{
    if (! onLevels || compiled.edit == nullptr)
        return;

    Levels levels;
    bool signal = false;

    for (auto& [id, fader] : compiled.insertFaders)
    {
        const auto peaks = fader->takePeaks();
        levels.inserts[id] = peaks;
        signal = signal || peaks.first > 1.0e-5f || peaks.second > 1.0e-5f;
    }

    for (auto& [id, fader] : compiled.channelFaders)
    {
        const auto [l, r] = fader->takePeaks();
        levels.channels[id] = std::max (l, r);
        signal = signal || levels.channels[id] > 1.0e-5f;
    }

    for (auto& [id, faders] : compiled.audioChannelFaders)
    {
        float level = 0;

        for (auto fader : faders)
        {
            const auto [l, r] = fader->takePeaks();
            level = std::max ({ level, l, r });
        }

        levels.channels[id] = level;
        signal = signal || level > 1.0e-5f;
    }

    // Once more after the signal stops, so meters fall to zero.
    if (signal || hadSignal)
        onLevels (levels);

    hadSignal = signal;
}

void LiveEngine::notifyTransport (bool force)
{
    if (! onTransportChange || compiled.edit == nullptr)
        return;

    auto s = getTransportState();

    if (force || s.playing || s.playing != lastNotified.playing || s.recording != lastNotified.recording)
    {
        lastNotified = s;
        onTransportChange (s);
    }
}

//==============================================================================
void LiveEngine::setMetronome (bool enabled)
{
    metronome = enabled;

    if (compiled.edit != nullptr)
        compiled.edit->clickTrackEnabled = metronome || clickForCountIn;

    saveSettings();
    notifyTransport (true);
}

void LiveEngine::setCountInBars (int bars)
{
    countInBars = std::clamp (bars, 0, 2);
    saveSettings();
    notifyTransport (true);
}

//==============================================================================
void LiveEngine::NoteCapture::handleIncomingMidiMessage (const juce::MidiMessage& message, te::MPESourceID)
{
    if (! message.isNoteOnOrOff())
        return;

    const std::lock_guard lock (mutex);
    messages.emplace_back (message, juce::Time::getMillisecondCounterHiRes());
}

std::vector<std::pair<juce::MidiMessage, double>> LiveEngine::NoteCapture::take()
{
    const std::lock_guard lock (mutex);
    return std::exchange (messages, {});
}

te::MidiInputDevice* LiveEngine::keyboardDevice()
{
    static const juce::String name ("GrooveKit keyboard");
    auto& devices = context.getEngine().getDeviceManager();

    for (auto& d : devices.getMidiInDevices())
        if (d->getName() == name)
            return d.get();

    // Kept in the settings, so this happens once; the device shows up after a rescan.
    (void) devices.createVirtualMidiDevice (name);
    return nullptr;
}

void LiveEngine::routeLiveInput()
{
    needsLiveRouting = false;

    for (auto& input : capturedInputs)
        if (auto instance = input.get())
            instance->removeConsumer (&capture);

    capturedInputs.clear();

    auto playbackContext = compiled.edit != nullptr ? compiled.edit->getCurrentPlaybackContext() : nullptr;
    auto keyboard = keyboardDevice();

    if (keyboard != nullptr && ! keyboard->isEnabled())
        keyboard->setEnabled (true);

    if (playbackContext == nullptr)
        return;

    te::AudioTrack* target = nullptr;

    if (liveChannel)
        if (auto it = compiled.channelTracks.find (*liveChannel); it != compiled.channelTracks.end())
            target = it->second;

    bool keyboardRouted = false;

    for (auto instance : playbackContext->getAllInputs())
    {
        auto& device = instance->getInputDevice();

        if (! device.isMidi())
            continue;

        for (auto id : instance->getTargets())
            (void) instance->removeTarget (id, nullptr);

        // Physical inputs play only once they're switched on in the settings.
        if (target == nullptr || ! device.isEnabled())
            continue;

        if (auto destination = instance->setTarget (target->itemID, false, nullptr))
            (*destination)->recordEnabled = false;

        device.setMonitorMode (te::InputDevice::MonitorMode::on);
        instance->addConsumer (&capture);
        capturedInputs.push_back (instance);
        keyboardRouted = keyboardRouted || &device == keyboard;
    }

    if (target != nullptr && ! keyboardRouted)
        needsLiveRouting = true;
}

void LiveEngine::setLiveChannel (std::optional<Id> channel)
{
    if (channel == liveChannel)
        return;

    liveChannel = std::move (channel);

    // A sampler that only plays audio clips isn't loaded to play notes: load it.
    if (liveChannel && ! compiled.channelTracks.contains (*liveChannel))
    {
        const auto& channels = session.getProject().channels;

        if (std::any_of (channels.begin(), channels.end(), [&] (const Channel& c) { return c.id == *liveChannel; }))
        {
            needsRebuild = true;
            triggerAsyncUpdate();
            return;
        }
    }

    routeLiveInput();
}

void LiveEngine::noteOn (int key, int velocity)
{
    if (auto keyboard = keyboardDevice())
    {
        auto message = juce::MidiMessage::noteOn (1, std::clamp (key, 0, 127), static_cast<juce::uint8> (std::clamp (velocity, 1, 127)));
        message.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        keyboard->handleIncomingMidiMessage (message, keyboard->getMPESourceID());
    }
}

void LiveEngine::noteOff (int key)
{
    if (auto keyboard = keyboardDevice())
    {
        auto message = juce::MidiMessage::noteOff (1, std::clamp (key, 0, 127));
        message.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        keyboard->handleIncomingMidiMessage (message, keyboard->getMPESourceID());
    }
}

//==============================================================================
void LiveEngine::startNoteRecording (const NoteRecordOptions& options)
{
    if (! recordings.empty() || noteRecording)
        throw std::runtime_error ("Already recording");

    auto& project = session.getProject();

    if (findChannel (project, options.channel) == nullptr)
        throw std::runtime_error ("No channel \"" + options.channel + "\"");

    setPlayPattern (options.pattern);   // throws if there's no such pattern
    setLiveChannel (options.channel);
    requireAudioDevice();

    const auto barMs = static_cast<double> (ticksPerBar (project.timeSignature)) / ticksPerQuarter * 60000.0 / project.tempo;

    NoteRecording r;
    r.options = options;
    r.loopLength = *playPatternLength();
    r.fromMs = juce::Time::getMillisecondCounterHiRes() + countInBars * barMs;
    noteRecording = std::move (r);
    capture.take();

    if (countInBars > 0)
    {
        clickForCountIn = true;
        compiled.edit->clickTrackEnabled = true;
    }

    transport().play (false);
    notifyTransport (true);
}

void LiveEngine::takeCapturedNotes()
{
    auto messages = capture.take();

    if (onInputNotes && ! messages.empty())
    {
        std::vector<std::pair<int, int>> played;

        for (auto& [message, ms] : messages)
            played.emplace_back (message.getNoteNumber(), message.isNoteOn() ? message.getVelocity() : 0);

        onInputNotes (played);
    }

    if (! noteRecording || messages.empty() || compiled.edit == nullptr)
        return;

    auto& r = *noteRecording;
    const auto& project = session.getProject();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto ticksPerMs = project.tempo * ticksPerQuarter / 60000.0;

    // Where each note was played: back from where the transport is now by how long ago it
    // arrived, and by the output latency, since the player answers what they hear.
    const auto latencyMs = context.getEngine().getDeviceManager().getOutputLatencySeconds() * 1000.0;
    const auto loop = r.loopLength;
    const auto nowTicks = getPosition();

    for (auto& [message, at] : messages)
    {
        if (at < r.fromMs)
            continue;

        auto t = nowTicks - static_cast<Ticks> (std::llround ((now - at + latencyMs) * ticksPerMs));
        t = ((t % loop) + loop) % loop;
        const auto key = message.getNoteNumber();

        if (message.isNoteOn())
        {
            r.held[key] = { t, message.getVelocity() };
        }
        else if (auto it = r.held.find (key); it != r.held.end())
        {
            auto [start, velocity] = it->second;
            auto length = t - start;

            if (length <= 0)
                length += loop;

            r.notes.push_back ({ r.options.channel, start, std::max<TickLength> (1, length), key, velocity });
            r.held.erase (it);
        }
    }
}

bool LiveEngine::isRecordingNotes() const
{
    return noteRecording.has_value();
}

std::vector<Id> LiveEngine::finishNoteRecording()
{
    takeCapturedNotes();
    stopTransport();

    auto r = std::move (*noteRecording);
    noteRecording.reset();

    if (clickForCountIn)
    {
        clickForCountIn = false;
        compiled.edit->clickTrackEnabled = metronome;
    }

    // Notes still held end where the recording stopped.
    const auto end = getPosition() % r.loopLength;

    for (auto& [key, held] : r.held)
    {
        auto length = end - held.first;
        r.notes.push_back ({ r.options.channel, held.first, std::max<TickLength> (1, length <= 0 ? length + r.loopLength : length), key, held.second });
    }

    if (r.options.quantize > 0)
        for (auto& n : r.notes)
            n.start = (std::llround (static_cast<double> (n.start) / r.options.quantize) * r.options.quantize) % r.loopLength;

    if (! r.notes.empty())
        session.execute ({ AddNotes { r.options.pattern, r.notes } }, { "recording", {}, "Record notes" });

    notifyTransport (true);
    return {};
}

} // namespace gk
