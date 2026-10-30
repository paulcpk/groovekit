#pragma once

#include "EffectPlugin.h"
#include "Meter.h"

#include <juce_dsp/juce_dsp.h>

namespace gk::effects
{

/** Duck: dips the volume on a tempo grid and swells back, the "sidechain" pump without a key.
    Each period it falls to 1 - depth over the attack, holds, then comes back over a share of
    the period, bent by the curve; offset moves the dip within the period. Follows the song's
    beats while playing, and runs on at its tempo while stopped. */
class DuckPlugin : public EffectPlugin
{
public:
    DuckPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;
    static const std::vector<const char*>& syncNames();

    /** The gain (0-1) at a point through the period, for these settings (the UI draws it). */
    static float shape (float phase, double periodMs, float depth, float attackMs, float holdMs, float release, float curve);

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Duck"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    /** { position, frames: [[phase %, gainDb], ...] }, a frame per 5 ms. */
    model::Json readMeter (const model::Json& params) const override;

private:
    double freePhase = 0;
    double bpm = 120;   // the song's tempo, taken at initialise
    MeterHistory meter { 2 };
    int hop = 240, hopCount = 0;
};

/** Clipper: drives the sound into a ceiling no sample passes, smoothly or hard, four times
    oversampled so the clipping doesn't alias. Before a limiter it takes the peaks off for
    loudness with less pumping. */
class ClipperPlugin : public EffectPlugin
{
public:
    ClipperPlugin (tracktion::PluginCreationInfo);
    ~ClipperPlugin() override;

    static const char* xmlTypeName;

    /** The clipping curve: x through a ceiling c with a knee of softness (0 hard, 1 soft). */
    static float clip (float x, float ceiling, float softness);

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Clip"; }
    double getLatencySeconds() override;
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    /** { position, frames: [[inputDb, outputDb, clippedDb], ...] }, a frame per 5 ms. */
    model::Json readMeter (const model::Json& params) const override;

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::vector<float> inputPeaks;      // this block's, driven
    MeterHistory meter { 3 };
    int hop = 240, hopCount = 0;
    float hopIn = 0, hopOut = 0;
};

} // namespace gk::effects
