#pragma once

#include "EffectPlugin.h"
#include "Meter.h"

namespace gk::effects
{

/** The static curve of a compressor: the gain change in dB for a level in dB. Above the
    threshold the level rises 1 dB per `ratio` dB (with a soft knee `knee` dB wide); below
    `belowThreshold` quiet sounds are lifted towards it by `belowRatio` (upward compression), at
    most `maxBoost` dB, and not at all near silence so noise isn't brought up. The UI draws the
    same curve. */
struct GainComputer
{
    float threshold = 0, ratio = 1, knee = 0;
    float belowThreshold = -100, belowRatio = 1, maxBoost = 24;

    float gainDb (float levelDb) const
    {
        float gain = 0;
        const auto over = levelDb - threshold;
        const auto slope = 1.0f / ratio - 1.0f;

        if (knee > 0 && std::abs (over) < knee / 2)
            gain = slope * (over + knee / 2) * (over + knee / 2) / (2 * knee);
        else if (over > 0)
            gain = slope * over;

        if (belowRatio > 1 && levelDb < belowThreshold)
        {
            // Fades out between -80 and -100 dB.
            const auto lift = std::min (maxBoost, (belowThreshold - levelDb) * (1.0f - 1.0f / belowRatio));
            gain += lift * juce::jlimit (0.0f, 1.0f, (levelDb + 100.0f) / 20.0f);
        }

        return gain;
    }
};

/** Smooths a gain change in dB: `attack` when the gain falls (the sound got louder), `release`
    when it rises. Works for upward boosts too, which fall as the sound gets louder. */
struct GainSmoother
{
    float gainDb = 0, attack = 0, release = 0;

    void setTimes (float attackMs, float releaseMs, double sampleRate)
    {
        attack = coefficient (attackMs, sampleRate);
        release = coefficient (releaseMs, sampleRate);
    }

    float process (float target)
    {
        const auto c = target < gainDb ? attack : release;
        return gainDb = target + c * (gainDb - target);
    }

    static float coefficient (float ms, double sampleRate)
    {
        return static_cast<float> (std::exp (-1.0 / (std::max (0.01, static_cast<double> (ms)) * 0.001 * sampleRate)));
    }
};

/** A second-order section (RBJ cookbook), stereo. */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] {}, z2[2] {};

    void setHighpass (double hz, double q, double sampleRate);
    void setLowpass (double hz, double q, double sampleRate);
    void setBandpass (double hz, double q, double sampleRate);
    void reset()    { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    float process (float x, int ch)
    {
        const auto y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
};

//==============================================================================
/** Feed-forward compressor, channels linked: a peak detector (high-passed if asked, so the lows
    don't pump it) into the gain curve, smoothed by attack and release, then make-up gain.
    Measures input, output and gain reduction for the UI. */
class CompressorPlugin : public EffectPlugin
{
public:
    CompressorPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Comp"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    /** { position, frames: [[inputDb (the key's, when keyed by a sidechain), outputDb, gainDb], ...] },
        a frame per 5 ms. */
    model::Json readMeter (const model::Json& params) const override;

private:
    GainSmoother smoother;
    Biquad detectorFilter;
    float lastLowCut = -1;
    MeterHistory meter { 3 };
    int hop = 240, hopCount = 0;
    float hopIn = 0, hopOut = 0, hopGain = 0;
};

//==============================================================================
/** Multiband compressor: three or four bands split by Linkwitz-Riley crossovers (the bands sum
    back flat), each with its own compressor that can also lift quiet sounds (upward
    compression), its own gain and stereo width, and bypass, solo or mute; then a master
    compressor on their sum and a ceiling no peak goes past. Measures each band for the UI. */
class MultibandPlugin : public EffectPlugin
{
public:
    MultibandPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;
    static constexpr int maxBands = 4;
    static constexpr int globalParams = 8, paramsPerBand = 8;

    static const std::vector<const char*>& modeNames();

    /** A band's parameter: its index in the catalogue. */
    static size_t bandParam (int band, int which)  { return static_cast<size_t> (globalParams + band * paramsPerBand + which); }
    enum BandParam { threshold, ratio, belowThreshold, belowRatio, attack, release, gain, mode };

    /** The master section's parameters come after the bands' (so older songs' indices hold),
        then each band's stereo width. */
    enum MasterParam { masterMode = globalParams + maxBands * paramsPerBand, masterThreshold, masterRatio, masterAttack, masterRelease,
                       masterGain, ceiling, lowCut, firstWidth };
    static size_t widthParam (int band)             { return static_cast<size_t> (firstWidth + band); }

    /** Then the master's upward compression, and whether the bands work at all ("master only"). */
    enum LaterParam { masterBelowThreshold = firstWidth + maxBands, masterBelowRatio, bandsMode };

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Multiband"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    /** { position, frames: [[inputDb, outputDb, then per band: inputDb, outputDb, gainDb, then the
        master's inputDb and gainDb (compressor and ceiling together)], ...] }, a frame per 5 ms,
        always with four bands (an unused fourth reads -100, -100, 0). */
    model::Json readMeter (const model::Json& params) const override;

private:
    using Crossover = juce::dsp::LinkwitzRileyFilter<float>;

    // Four bands: split at mid, then each half (put through the other half's crossover as an
    // all-pass, so the phases match). Three: split at low, then the rest at high.
    Crossover splitMid, splitLow, splitHigh, allpassLow, allpassHigh;

    GainSmoother smoothers[maxBands], masterSmoother;
    float rms[maxBands] {};
    float rmsCoefficient = 0;
    Biquad lowCutFilter;
    float lastLowCut = -1;
    float ceilingGain = 1;

    static constexpr int frameSize = 4 + 3 * maxBands;
    MeterHistory meter { frameSize };
    int hop = 240, hopCount = 0;
    float hopValues[frameSize] {};

    void resetHop();
};

} // namespace gk::effects
