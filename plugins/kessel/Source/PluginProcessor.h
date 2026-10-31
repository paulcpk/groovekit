#pragma once

#include "Kick.h"
#include "Presets.h"

namespace kessel
{

const std::vector<gkp::Preset>& getPresets();

class Processor : public gkp::PresetProcessor
{
public:
    Processor();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override        { return 8.0; }

    Settings readSettings() const;

    /** For the editor: the output's peak (falling back slowly), the compressor's gain reduction,
        and how long ago the last kick started and on which key. */
    std::atomic<float> peak { 0 }, reduction { 0 };
    std::atomic<double> sinceHit { 1e9 };
    std::atomic<int> lastKey { rootKey };

private:
    Kick kick;
    std::vector<float> mono;
    double rate = 48000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace kessel
