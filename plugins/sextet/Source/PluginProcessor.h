#pragma once

#include "Synth.h"
#include "Presets.h"

namespace sextet
{

const std::vector<gkp::Preset>& getPresets();

class Processor : public gkp::PresetProcessor
{
public:
    Processor();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override        { return 12.0; }

    /** The LFOs' latest values, for the editor. */
    std::array<float, numLfos> lfoValues {};

private:
    Synth synth;
    float modWheel = 0, aftertouch = 0;

    Settings readSettings() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace sextet
