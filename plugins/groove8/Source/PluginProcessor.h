#pragma once

#include "Synth.h"
#include "MachineProcessor.h"

namespace groove8
{

const std::vector<gkp::Preset>& getPresets();

class Processor : public rhythm::MachineProcessor
{
public:
    Processor();

    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override        { return 4.0; }

    int noteForRow (int row) const override;
    int rowForNote (int note) const override;

private:
    Synth synth;

    void prepareVoices (double sampleRate) override;
    void renderVoices (juce::AudioBuffer<float>&, const juce::MidiBuffer&) override;
    Settings readSettings() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace groove8
