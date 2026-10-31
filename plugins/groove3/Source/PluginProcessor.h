#pragma once

#include "Bassline.h"
#include "Line.h"
#include "LineLink.h"
#include "Presets.h"

namespace groove3
{

const std::vector<gkp::Preset>& getPresets();

/** Groove-3: the voice played by MIDI notes and by the step section's line, which plays with the
    host's transport. The line is saved with the parameters; presets change the sound only.

    Inside GrooveKit the line never plays on its own: GrooveKit connects (see LineLink) and the
    step section edits the notes of the GrooveKit pattern the channel plays instead. */
class Processor : public gkp::PresetProcessor, private juce::AsyncUpdater, private juce::AudioProcessorValueTreeState::Listener
{
public:
    Processor();
    ~Processor() override;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override        { return 4.0; }

    void setCurrentProgram (int) override;
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::VST3ClientExtensions* getVST3ClientExtensions() override     { return &extensions; }

    Settings readSettings() const;
    float value (const juce::String& paramId) const     { return get (paramId); }

    /** The line the step section plays outside GrooveKit, all its steps (the Length knob says how
        many play). Message thread. */
    const Line& getOwnLine() const                      { return ownLine; }
    void setOwnLine (Line);

    LineLink link;

    /** What the panel shows: the own line's playing step (or -1), the host's tempo and whether
        it plays, the filter's cutoff, the main envelope, the accent sweep, and the key sounding
        (or -1). */
    std::atomic<int> ownStep { -1 };
    std::atomic<double> hostTempo { 0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<float> cutoffHz { 0 }, envelope { 0 }, sweep { 0 };
    std::atomic<int> soundingKey { -1 };

private:
    struct Event
    {
        int sample, key, velocity;
        bool on, fromLine;
    };

    struct Held
    {
        int key;
        bool accent;
    };

    Bassline voice;
    LineLink::Extensions extensions { link };
    double rate = 48000;
    std::vector<float> mono;
    std::vector<Event> events;
    std::vector<Held> held;

    // The own line: its notes as the message thread last made them, and the audio thread's copy.
    Line ownLine;
    juce::SpinLock notesLock;
    std::vector<gkmachine::Note> ownNotes, playingNotes;
    int ownTicks = 16 * stepTicks, playingTicks = 16 * stepTicks;
    bool notesChanged = true;
    double nextTick = -1;
    std::vector<int> lineKeys;      // keys the line has on

    void runLine (int numSamples);
    void play (const Event&);
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void updateNotes();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace groove3
