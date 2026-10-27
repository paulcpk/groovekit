#pragma once

#include "Effects.h"
#include "Meter.h"

namespace gk::effects
{

/** Stereo field: a matrix of how much of each input side goes to each output side (negative
    inverts it), so it can swap, fold, invert or narrow the sides; then a short delay of one
    side (the Haas effect), mid/side width, mono below a frequency (crossover-split, so the bass
    stays solid) and balance. Keeps the latest output for the UI's vectorscope. */
class StereoPlugin : public EffectPlugin
{
public:
    StereoPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Stereo"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    /** { left: [...], right: [...] }: the latest output samples (`samples`, default 512). */
    model::Json readMeter (const model::Json& params) const override;

private:
    static constexpr int numSmoothed = 7;   // the matrix, width, balance, delay

    juce::SmoothedValue<float> smoothed[numSmoothed];
    DelayLine lines[2];
    juce::dsp::LinkwitzRileyFilter<float> bassSplit;
    float lastMonoHz = -1;
    StereoScope scope;
};

} // namespace gk::effects
