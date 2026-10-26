#include "LiveEngine.h"

#include "EngineContext.h"

namespace gk
{

namespace te = tracktion;
using namespace model;

LiveEngine::LiveEngine (EngineContext& c, Session& s)
    : context (c), session (s)
{
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
void LiveEngine::play()
{
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
std::vector<std::string> LiveEngine::getInputNames() const
{
    std::vector<std::string> names;

    for (auto device : context.getEngine().getDeviceManager().getWaveInputDevices())
        names.push_back (device->getName().toStdString());

    return names;
}

void LiveEngine::startRecording (const std::vector<RecordTarget>& targets)
{
    if (targets.empty())
        throw std::runtime_error ("Nothing to record");

    if (! recordings.empty())
        throw std::runtime_error ("Already recording");

    updateNow();

    auto& project = session.getProject();
    auto& edit = *compiled.edit;
    auto playbackContext = edit.getCurrentPlaybackContext();

    if (playbackContext == nullptr)
        throw std::runtime_error ("No playback context");

    for (auto& t : targets)
    {
        if (findInsert (project, t.insert) == nullptr)
            throw std::runtime_error ("No mixer insert \"" + t.insert + "\"");

        if (findTrack (project, t.track) == nullptr)
            throw std::runtime_error ("No playlist track \"" + t.track + "\"");

        te::InputDeviceInstance* input = nullptr;

        for (auto instance : playbackContext->getAllInputs())
            if (instance->getInputDevice().getName().toStdString() == t.input)
                input = instance;

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

            Id id;

            for (int n = 1; id.empty() || used.contains (id); ++n)
                id = "rec-" + r.target.track + "-" + std::to_string (n);

            used.insert (id);

            AudioClip ac;
            ac.id = id;
            ac.track = r.target.track;
            ac.start = toTicks (position.getStart());
            ac.length = std::max<Ticks> (1, toTicks (position.getEnd()) - ac.start);
            ac.sample = relative.empty() || relative.string().starts_with ("..") ? file.string() : relative.string();
            ac.insert = r.target.insert;
            ac.offset = position.getOffset().inSeconds();

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
