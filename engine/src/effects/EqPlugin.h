#pragma once

#include "EffectPlugin.h"

namespace gk::effects
{

/** Seven-band parametric EQ. Each band is off, a low or high cut (24 dB/octave), a low or high
    shelf, a peak or a notch, with RBJ-cookbook biquads; the UI draws the response with the
    same formulas. */
class EqPlugin : public EffectPlugin
{
public:
    EqPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;
    static constexpr int numBands = 7;

    enum class BandType { off, lowCut, lowShelf, peak, notch, highShelf, highCut };
    static const std::vector<const char*>& bandTypeNames();

    /** Default band layout: shelves at the ends, peaks between, all flat. */
    struct BandDefaults { BandType type; double freq, q; };
    static const BandDefaults bandDefaults[numBands];

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "EQ"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] {}, z2[2] {};

        float process (float x, int ch)
        {
            const auto y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
    };

    struct Band
    {
        BandType type = BandType::off;
        float freq = 1000, gainDb = 0, q = 0.71f;
        Biquad stages[2];   // cuts run twice, for 24 dB/octave
    };

    Band bands[numBands];

    void updateBand (int index);
};

} // namespace gk::effects
