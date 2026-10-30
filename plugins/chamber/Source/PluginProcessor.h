#pragma once

#include "Parameters.h"
#include "Reverb.h"
#include "Presets.h"

namespace chamber
{

const std::vector<gkp::Preset>& getPresets();

class Processor : public gkp::PresetProcessor
{
public:
    Processor();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override        { return 30.0; }

    std::atomic<float> inputLevel { 0 };

private:
    Reverb reverb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace chamber
