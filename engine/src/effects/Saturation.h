#pragma once

#include "Dynamics.h"
#include "Effects.h"

namespace gk::effects
{

/** Exciter: a gentle analog-style stage (a tube, tape, a transformer or a console channel)
    driven softly, level-matched so it adds warmth rather than loudness, plus generated
    harmonics above a frequency (presence and air) and below about 150 Hz (body). The stage runs
    at twice the sample rate. Measures input and output for the UI's VU meter. */
class ExciterPlugin : public EffectPlugin
{
public:
    ExciterPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;
    static const std::vector<const char*>& modelNames();

    /** The stage: what one sample becomes for a model, drive (as gain) and warmth (0-1, the share
        of even harmonics), without the level matching. The UI draws it. */
    static float stage (int model, float x, float drive, float warmth);

    /** The gain the level matching divides by: the stage's gain at -10 dBFS. */
    static float stageGain (int model, float drive, float warmth);

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Exciter"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    double getLatencySeconds() override;

    /** { position, frames: [[inputDb, outputDb], ...] }: RMS over each 5 ms. */
    model::Json readMeter (const model::Json& params) const override;

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    int maxBlock = 0;
    std::vector<float> inputSquares;                       // per sample of a block, for the meter
    Biquad presence, presenceAfter, bodyLow, bodyBand;   // at the oversampled rate
    float presenceLevel[2] {}, levelAttack = 0, levelRelease = 0;
    OnePole transformerLow[2], tapeLoss[2], dcBlock[2];
    float lastHarmonicsHz = -1;

    MeterHistory meter { 2 };
    int hop = 240, hopCount = 0;
    float hopIn = 0, hopOut = 0;
};

//==============================================================================
/** Bitcrusher: drive, then the sample rate lowered by holding samples (unfiltered, so it
    aliases, with optional jitter in the timing) and the levels quantised to a number of bits,
    then a low-pass tone and output level. */
class BitcrusherPlugin : public EffectPlugin
{
public:
    BitcrusherPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Crush"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    double phase = 1;
    float held[2] {};
    Biquad tone;
    float lastTone = -1;
    juce::Random random;
};

} // namespace gk::effects
