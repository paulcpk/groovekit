#pragma once

#include "Synth.h"

namespace triad
{

/** A factory sound: parameter ids to values, in their own units (the rest take defaults). */
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<Preset>& getPresets();

class Processor : public juce::AudioProcessor
{
public:
    Processor();

    juce::AudioProcessorValueTreeState state;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return "Triad"; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isSynth() const                                { return true; }
    double getTailLengthSeconds() const override        { return 10.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override                    { return program; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** The latest output, mono, oldest first, for the editor's scope. */
    void readScope (float* destination, int numSamples) const;
    static constexpr int scopeSize = 2048;

    std::atomic<int> activeVoices { 0 };

private:
    Synth synth;
    int program = 0;
    std::array<float, scopeSize> scope {};
    std::atomic<int> scopePosition { 0 };

    Settings readSettings() const;
    float get (const juce::String& id) const       { return state.getRawParameterValue (id)->load(); }
    int getIndex (const juce::String& id) const    { return juce::roundToInt (get (id)); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace triad
