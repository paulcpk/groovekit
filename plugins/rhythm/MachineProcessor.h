#pragma once

#include "Presets.h"
#include "Sequencer.h"

namespace rhythm
{

/** A rhythm machine: voices played by MIDI notes and by the built-in step sequencer, which
    follows the host's transport. The patterns are saved with the parameters; presets change the
    sound only, never the sequencer's settings. */
class MachineProcessor : public gkp::PresetProcessor
{
public:
    /** rows: the voices' rows in panel order, then the accent row (and the flam row). */
    MachineProcessor (const juce::String& name, juce::AudioProcessorValueTreeState::ParameterLayout, const std::vector<gkp::Preset>&,
                      std::vector<Row> rows, int numVoiceRows, bool flam, const std::vector<FactoryPattern>&);

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void setCurrentProgram (int) override;
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    const std::vector<Row> rows;
    const int numVoiceRows, accentRow, flamRow;
    PatternStore patterns;
    Sequencer sequencer;

    /** What the editor shows: the host's tempo, whether it plays, and a count of hits per row
        that goes up whenever the row's voice is struck. */
    std::atomic<double> hostTempo { 0 };
    std::atomic<bool> hostPlaying { false };
    std::array<std::atomic<uint32_t>, maxRows> hits {};

    /** The note that plays a row's voice right now, for the sequencer and the editor. */
    virtual int noteForRow (int row) const = 0;

    /** The row a note plays, or -1. */
    virtual int rowForNote (int note) const = 0;

    float value (const juce::String& paramId) const    { return get (paramId); }

protected:
    virtual void prepareVoices (double sampleRate) = 0;

    /** Plays the block's notes into the (cleared) buffer. */
    virtual void renderVoices (juce::AudioBuffer<float>&, const juce::MidiBuffer&) = 0;

private:
    struct Pending
    {
        int samples, note;
        juce::uint8 velocity;
    };

    double sampleRate = 48000;
    std::vector<Sequencer::Step> steps;
    std::vector<Pending> pending;
    juce::MidiBuffer sequenced;

    void runSequencer (juce::MidiBuffer&, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MachineProcessor)
};

} // namespace rhythm
