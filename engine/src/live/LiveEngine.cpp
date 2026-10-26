#include "LiveEngine.h"

#include "EngineContext.h"
#include "compiler/EditGraveyard.h"
#include "live/MidiClockPlugin.h"
#include "mixer/FaderPlugin.h"
#include "analysis/Analysis.h"
#include "library/SampleFeatures.h"
#include "effects/BuiltinEffects.h"

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
    stopTimer();
    session.removeListener (listenerId);

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
    devices.initialise();

    // After Tracktion's own callback, so each cycle's clock is complete when it's sent.
    devices.deviceManager.removeAudioCallback (midiClock.get());
    devices.deviceManager.addAudioCallback (midiClock.get());
    devices.deviceManager.removeAudioCallback (&preview);
    devices.deviceManager.addAudioCallback (&preview);

    pairStereoInputs();

    if (hasAudioDevice())
        transport().ensureContextAllocated();
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
    const auto position = compiled.edit != nullptr ? transport().getPosition() : te::TimePosition();
    std::optional<te::TimeRange> loop;

    if (compiled.edit != nullptr && transport().looping.get())
        loop = transport().getLoopRange();

    // The clock carries on into the new Edit if it plays on from the same place.
    midiClock->setSource (nullptr);

    if (compiled.edit != nullptr)
        transport().stop (false, true);

    const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
    auto next = compileProject (context.getEngine(), session.getProject(), { projectDir, te::Edit::forEditing });

    // Tracktion puts recordings next to the Edit's file, but only if that folder exists
    // (otherwise they land in the working directory).
    std::filesystem::create_directories (projectDir / "recordings");
    const auto editFile = juce::File ((projectDir / "recordings" / "live.tracktionedit").string());
    next.edit->editFileRetriever = [editFile] { return editFile; };

    compiled = std::move (next);
    addClockTrack();

    auto& tc = transport();

    // A playback context needs a running device (Tracktion divides by its sample rate).
    if (hasAudioDevice())
        tc.ensureContextAllocated();

    tc.setPosition (position);

    if (loop)
    {
        tc.setLoopRange (*loop);
        tc.looping = true;
    }

    if (wasPlaying)
        tc.play (false);
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

bool LiveEngine::applyToLiveEdit (const Change& change)
{
    if (compiled.edit == nullptr || change.ops.empty())
        return false;

    auto& project = session.getProject();
    std::vector<std::function<void()>> actions;

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

        // /channels/<i>/volumeDb or /pan
        if (keys.size() == 3 && keys[0] == "channels" && (keys[2] == "volumeDb" || keys[2] == "pan"))
        {
            const auto i = index (keys[1]);
            if (! i || *i >= project.channels.size()) return false;
            const auto& channel = project.channels[*i];
            const auto it = compiled.channelFaders.find (channel.id);
            if (it == compiled.channelFaders.end()) return false;
            actions.push_back ([fader = it->second, v = channel.volumeDb, p = channel.pan] { fader->setVolumeDb (static_cast<float> (v)); fader->setPan (static_cast<float> (p)); });
            continue;
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
    if (range)
    {
        if (range->second <= range->first)
            throw std::runtime_error ("The loop must end after it starts");

        transport().setLoopRange ({ toTime (range->first), toTime (range->second) });
    }

    transport().looping = range.has_value();
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

    for (auto& d : context.getEngine().getDeviceManager().getMidiInDevices())
        names.push_back (d->getName().toStdString());

    return names;
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

        if (options.addClips && findTrack (project, t.track) == nullptr)
            throw std::runtime_error ("No playlist track \"" + t.track + "\"");

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
        track->setName (juce::String (t.track + "-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S").toStdString()));
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
                id = "rec-" + r.target.track + "-" + std::to_string (n);

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

            if (start < 0)
            {
                offset -= start;
                start = 0;
            }

            AudioClip ac;
            ac.id = id;
            ac.track = r.target.track;
            ac.start = toTicks (te::TimePosition::fromSeconds (start));
            ac.length = std::max<Ticks> (1, toTicks (te::TimePosition::fromSeconds (end)) - ac.start);

            if (recordOptions.length)
                ac.length = std::max<Ticks> (1, std::min (ac.length, recordStart + *recordOptions.length - ac.start));

            ac.sample = relative.empty() || relative.string().starts_with ("..") ? file.string() : relative.string();
            ac.insert = r.target.insert;
            ac.offset = offset;

            if (recordOptions.normalizeLufs)
                if (const auto lufs = analysis::integratedLufs (*audio); lufs > analysis::silenceDb)
                    ac.gainDb = std::round (std::clamp (*recordOptions.normalizeLufs - lufs, -24.0, 24.0) * 10.0) / 10.0;

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

    }
    catch (const std::exception&)
    {
        // A damaged file just means no alignment until the next calibration.
    }
}

void LiveEngine::saveSettings() const
{
    std::filesystem::create_directories (settingsFile.parent_path());
    std::ofstream (settingsFile) << Json { { "alignmentMs", alignmentMs } }.dump (2) << "\n";
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
    return s;
}

void LiveEngine::timerCallback()
{

    // Timed recordings stop on their own; the takes are trimmed to the exact length.
    if (! recordings.empty() && recordOptions.length && getPosition() >= recordStart + *recordOptions.length)
        stopRecording();

    notifyTransport (false);
    reportLevels();
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

} // namespace gk
