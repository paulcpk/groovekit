#pragma once

#include "compiler/EditCompiler.h"
#include "live/MidiClock.h"
#include "live/SamplePreview.h"
#include "session/Session.h"

#include <juce_events/juce_events.h>

namespace gk
{

class EngineContext;

struct LiveRecordOptions
{
    /** Stop by itself after this many ticks, and trim the takes to it. */
    std::optional<model::Ticks> length;

    /** Don't add clips for takes that stayed silent (MIDI or unused tracks). The files are kept. */
    bool dropSilent = false;

    /** False to record without touching the project (e.g. for calibration). */
    bool addClips = true;

    /** Set each take's clip gain so it plays at this integrated loudness (within +-24 dB).
        Hardware outputs like the Digitakt's per-track ones arrive very quiet. */
    std::optional<double> normalizeLufs;
};

/** Real-time playback and recording of a session. Lives on the message thread.

    Keeps a playable Edit in step with the session: after a change it recompiles and, if it
    was playing, carries on from the same position. Recording sends inputs to hidden tracks
    that feed the chosen inserts; when it stops, the takes become audio clips in the
    project (one undoable change) and the files stay in <project>/recordings.
*/
class LiveEngine : private juce::AsyncUpdater,
                   private juce::Timer,
                   private juce::ChangeListener
{
public:
    /** settingsFile keeps per-device recording alignment; empty for <app data>/recording.json. */
    LiveEngine (EngineContext&, Session&, std::filesystem::path settingsFile = {});
    ~LiveEngine() override;

    /** Opens the system's default audio device (or the one chosen in settings), unless an earlier
        LiveEngine did, and plays through it. Without this, the engine plays through whatever
        device the device manager has (e.g. a hosted device in tests). */
    void openDefaultAudioDevice();

    /** Closes the input if it's a Bluetooth headset's microphone that wasn't chosen as the input
        (see isBluetoothAudioDevice): macOS makes it the default input along with the output. Also
        done whenever the device changes. */
    void releaseBluetoothMicrophone();

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

    struct MidiPort
    {
        std::string name;
        bool enabled = false, sendsClock = false;
    };

    std::vector<MidiPort> getMidiOutputs() const;
    std::vector<std::string> getMidiInputs() const;

    /** Sends MIDI clock, start and stop to an output, so hardware like the Digitakt follows
        GrooveKit's tempo and starts with its transport. Throws if there's no such output. */
    void setMidiClock (const std::string& output, bool enabled);
    MidiClock& getMidiClock()           { return *midiClock; }

    /** Pairs input channels named "... L" and "... R" (as the Digitakt names its per-track outputs)
        into stereo inputs and leaves the rest mono. Call after the input device changes. */
    void pairStereoInputs();

    using RecordOptions = LiveRecordOptions;

    /** Arms the targets and starts the transport recording from the current position.
        Throws if an input or id is unknown. */
    void startRecording (const std::vector<RecordTarget>&, const RecordOptions& = {});

    /** Stops, and adds the takes to the project. Returns the new clip ids. */
    std::vector<model::Id> stopRecording (const ExecuteOptions& = { "recording", {}, {} });

    struct CalibrationResult
    {
        double alignmentMs;         // how far takes from this device are moved from now on (negative: earlier)
        double spreadMs;            // median deviation of the hits around the measured offset
        int hits;                   // hits the measurement used
        std::string device;
    };

    /** Records an input that plays clean hits in time with GrooveKit (e.g. a kick on a Digitakt
        track following GrooveKit's MIDI clock) for a few bars from bar 1, measures how far the hits
        sit from the 16th-note grid, and stores that as the input device's alignment. Blocks while
        recording. Round trips through hardware (MIDI out, the instrument, USB audio in) aren't
        known to the engine, so this is how takes end up on the grid. */
    CalibrationResult calibrateAlignment (const std::string& input, int bars);

    /** Milliseconds takes from the current input device are moved when they become clips
        (negative: earlier, the usual case for a hardware round trip). */
    double getAlignmentMs() const;
    void setAlignmentMs (double);

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

    /** Peak output levels (linear, since the last report) of every insert (left, right) and channel. */
    struct Levels
    {
        std::map<model::Id, std::pair<float, float>> inserts;
        std::map<model::Id, float> channels;
    };

    /** Called on the message thread about 30 times a second while there's signal, and once when it stops. */
    std::function<void (const Levels&)> onLevels;

    /** Auditions a sample file on the audio device, independent of the song. */
    SamplePreview& getPreview()         { return preview; }

    /** Recompiles now if the project changed since the last build (normally done asynchronously). */
    void updateNow();

    tracktion::Edit* getEdit() const    { return compiled.edit.get(); }

    /** A plugin in the live Edit, by the keys of CompiledEdit::plugins. */
    tracktion::Plugin* findPlugin (const std::string& key) const;

    /** The key of a plugin in the live Edit, or empty if it isn't one of its instruments or effects. */
    std::string findPluginKey (const tracktion::Plugin&) const;

    /** Opens or closes a hosted plugin's own window. False if the key isn't a hosted plugin. */
    bool showPluginWindow (const std::string& key, bool show);

private:
    EngineContext& context;
    Session& session;
    CompiledEdit compiled;
    std::shared_ptr<MidiClock> midiClock = std::make_shared<MidiClock>();
    std::map<std::string, model::Json> builtPluginRefs; // the hosted plugins the live Edit was built with
    const void* clockPlugin = nullptr;  // the live Edit's, the clock's source
    SamplePreview preview;
    bool hadSignal = false;
    int listenerId = 0;
    bool needsRebuild = false;

    struct ActiveRecording
    {
        RecordTarget target;
        tracktion::AudioTrack* track;
    };

    std::vector<ActiveRecording> recordings;

    struct Take
    {
        std::filesystem::path file;
        double startSeconds, offsetSeconds;
    };

    std::vector<Take> lastTakes;

    std::filesystem::path settingsFile;
    std::map<std::string, double> alignmentMs; // by input device name

    std::string currentInputDevice() const;
    void loadSettings();
    void saveSettings() const;
    RecordOptions recordOptions;
    model::Ticks recordStart = 0;
    TransportState lastNotified;

    void rebuild();
    void addClockTrack();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** Applies a change that only moves mixer values (volume, pan, send levels, effect
        parameters) to the playing Edit, without rebuilding it. False if it can't. */
    bool applyToLiveEdit (const Change&);
    void stopTransport();
    bool hasAudioDevice() const;
    void requireAudioDevice();
    tracktion::TransportControl& transport() const;
    tracktion::TimePosition toTime (model::Ticks) const;
    model::Ticks toTicks (tracktion::TimePosition) const;

    void handleAsyncUpdate() override;
    void timerCallback() override;
    void notifyTransport (bool force);
    void reportLevels();
};

} // namespace gk
