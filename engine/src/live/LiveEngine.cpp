#include "LiveEngine.h"

#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "library/SampleFeatures.h"

#include <fstream>

namespace gk
{

namespace te = tracktion;
using namespace model;

namespace
{
    /** Takes quieter than this throughout count as silent. */
    constexpr float silentTakeDb = -50.0f;
}

LiveEngine::LiveEngine (EngineContext& c, Session& s, std::filesystem::path file)
    : context (c), session (s),
      settingsFile (file.empty() ? EngineContext::getAppDataFolder() / "recording.json" : std::move (file))
{
    loadSettings();

    listenerId = session.addListener ([this] (const Change&)
    {
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

    if (compiled.edit != nullptr)
        transport().stop (false, true);
}

void LiveEngine::openDefaultAudioDevice()
{
    context.getEngine().getDeviceManager().initialise();
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

    transport().stop (false, true);
    notifyTransport (true);
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
            ports.push_back ({ d->getName().toStdString(), d->isEnabled(), d->isSendingClock() });

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
            if (enabled && ! d->isEnabled())
                d->setEnabled (true);

            d->setSendingClock (enabled);
            devices.dispatchPendingUpdates();

            // The playback context picks up output devices when it's created.
            if (hasAudioDevice())
                transport().ensureContextAllocated (true);

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

    // GrooveKit aligns takes itself; Tracktion's adjustment can only drop samples, never pad.
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
    recordOptions = options;
    recordStart = getPosition();
    transport().record (false);
    notifyTransport (true);
}

std::vector<Id> LiveEngine::stopRecording (const ExecuteOptions& options)
{
    transport().stop (false, true);

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

            // The device's measured round trip moves the take onto the grid.
            const auto shift = te::TimeDuration::fromSeconds (getAlignmentMs() / 1000.0);

            AudioClip ac;
            ac.id = id;
            ac.track = r.target.track;
            ac.start = toTicks (position.getStart() + shift);
            ac.length = std::max<Ticks> (1, toTicks (position.getEnd() + shift) - ac.start);

            if (recordOptions.length)
                ac.length = std::max<Ticks> (1, std::min (ac.length, recordStart + *recordOptions.length - ac.start));

            ac.sample = relative.empty() || relative.string().starts_with ("..") ? file.string() : relative.string();
            ac.insert = r.target.insert;
            ac.offset = position.getOffset().inSeconds();

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

    // Raw takes arrive `offset` early (negative) or late; place them later by the opposite.
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
