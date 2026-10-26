#pragma once

#include "compiler/EditCompiler.h"
#include "session/Session.h"

#include <juce_events/juce_events.h>

namespace gk
{

class EngineContext;

/** Real-time playback and recording of a session. Lives on the message thread.

    Keeps a playable Edit in step with the session: after a change it recompiles and, if it
    was playing, carries on from the same position. Recording sends inputs to hidden tracks
    that feed the chosen inserts; when it stops, the takes become audio clips in the
    project (one undoable change) and the files stay in <project>/recordings.
*/
class LiveEngine : private juce::AsyncUpdater,
                   private juce::Timer
{
public:
    LiveEngine (EngineContext&, Session&);
    ~LiveEngine() override;

    /** Opens the system's default audio device. Without this, the engine plays through
        whatever device the device manager has (e.g. a hosted device in tests). */
    void openDefaultAudioDevice();

    //==============================================================================
    void play();
    void stop();
    bool isPlaying() const;
    bool isRecording() const;

    void setPosition (model::Ticks);
    model::Ticks getPosition() const;

    /** Loops between two positions, or turns looping off. */
    void setLoop (std::optional<std::pair<model::Ticks, model::Ticks>>);

    //==============================================================================
    struct RecordTarget
    {
        std::string input;      // input device name, from getInputNames()
        model::Id insert;       // where the input is heard and the clip will play
        model::Id track;        // playlist track for the new clip
    };

    std::vector<std::string> getInputNames() const;

    /** Arms the targets and starts the transport recording. Throws if an input or id is unknown. */
    void startRecording (const std::vector<RecordTarget>&);

    /** Stops, and adds the takes to the project. Returns the new clip ids. */
    std::vector<model::Id> stopRecording (const ExecuteOptions& = { "recording", {}, {} });

    //==============================================================================
    struct TransportState
    {
        bool playing = false, recording = false;
        model::Ticks position = 0;
        double seconds = 0;
    };

    TransportState getTransportState() const;

    /** Called on the message thread about 30 times a second while playing, and on every start and stop. */
    std::function<void (const TransportState&)> onTransportChange;

    /** Recompiles now if the project changed since the last build (normally done asynchronously). */
    void updateNow();

    tracktion::Edit* getEdit() const    { return compiled.edit.get(); }

private:
    EngineContext& context;
    Session& session;
    CompiledEdit compiled;
    int listenerId = 0;
    bool needsRebuild = false;

    struct ActiveRecording
    {
        RecordTarget target;
        tracktion::AudioTrack* track;
    };

    std::vector<ActiveRecording> recordings;
    TransportState lastNotified;

    void rebuild();
    tracktion::TransportControl& transport() const;
    tracktion::TimePosition toTime (model::Ticks) const;
    model::Ticks toTicks (tracktion::TimePosition) const;

    void handleAsyncUpdate() override;
    void timerCallback() override;
    void notifyTransport (bool force);
};

} // namespace gk
