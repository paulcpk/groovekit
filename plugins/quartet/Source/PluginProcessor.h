#pragma once

#include "Synth.h"
#include "Presets.h"

namespace quartet
{

const std::vector<gkp::Preset>& getPresets();

class Processor : public gkp::PresetProcessor
{
public:
    Processor();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override        { return 10.0; }

private:
    Synth synth;

    Settings readSettings() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace quartet
