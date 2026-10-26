#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <functional>

namespace gk
{

/** One audio block as the clock sees it. */
struct MidiClockBlock
{
    double startPpq = 0, endPpq = 0;    // quarter notes at the start and end of the block
    double seconds = 0;                 // how long the block is
    double offsetSeconds = 0;           // where it starts in the audio device's cycle
    bool playing = false;
    bool recording = false;
};

struct MidiClockEvent
{
    double seconds;                     // from the start of the audio device's cycle
    juce::uint8 bytes[3];
    int size;

    juce::MidiMessage toMessage() const { return juce::MidiMessage (bytes, size); }
};

/** Turns blocks of the playhead into MIDI clock (24 a quarter note), start, continue, song
    position and stop. Starting from the top or recording sends Start (hardware like the
    Digitakt then plays its pattern from step 1); starting anywhere else waits for the next
    16th and sends its song position and Continue. A jump stops and starts again.
*/
class MidiClockGenerator
{
public:
    void process (const MidiClockBlock&, const std::function<void (const MidiClockEvent&)>&);

    bool isRunning() const      { return running; }
    void reset()                { running = false; }

private:
    bool running = false, syncing = false;
    juce::int64 nextTick = 0;
    double lastEndPpq = 0;
};

/** Sends GrooveKit's MIDI clock to hardware, timed by the audio device.

    The live Edit's clock plugin hands it each block of the playhead on the audio thread; at the
    end of every audio device cycle it gives the cycle's messages to CoreMIDI timestamped from the
    device's host time, so they go out evenly spaced no matter when the audio thread got to them.
    (Tracktion's own clock leaves its messages to a timer, which sends them in bursts per audio
    block, and restarts hardware when recording begins.)

    Register it with the audio device manager after Tracktion's own callback, so a cycle's blocks
    are in by the time it sends them.
*/
class MidiClock : public juce::AudioIODeviceCallback
{
public:
    MidiClock();
    ~MidiClock() override;

    /** Starts or stops sending to a MIDI output, by name. Returns false if there's no such output. */
    bool setOutput (const std::string& name, bool enabled);
    bool isSendingTo (const std::string& name) const;

    /** Only blocks from this source (the current live Edit's clock plugin) are used, so a
        discarded Edit stopping doesn't stop the hardware while its replacement carries on. */
    void setSource (const void*);

    /** While recording, starting sends Start wherever the playhead is. */
    void setRecording (bool);

    /** Sends Stop now if the clock is running. */
    void stopNow();

    /** From the clock plugin, on the audio thread. */
    void addBlock (const void* source, double startPpq, double endPpq, double seconds, bool playing);

    /** Called with every message as it's sent, with the host time it's due (for tests). */
    std::function<void (const juce::MidiMessage&, juce::uint64 hostTimeNs)> onSend;

    //==============================================================================
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override;

private:
    struct Destination;

    mutable juce::SpinLock lock;
    std::vector<std::unique_ptr<Destination>> destinations;
    const void* source = nullptr;
    bool recording = false;

    MidiClockGenerator generator;
    std::array<MidiClockEvent, 256> pending;
    size_t numPending = 0;
    double cycleSeconds = 0;

    void send (const MidiClockEvent*, size_t, juce::uint64 cycleHostTimeNs);
};

} // namespace gk
