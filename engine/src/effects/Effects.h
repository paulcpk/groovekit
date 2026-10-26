#pragma once

#include "EffectPlugin.h"

namespace gk::effects
{

/** A delay line with fractional (linearly interpolated) reads. */
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        buffer.assign (static_cast<size_t> (juce::nextPowerOfTwo (maxSamples + 2)), 0.0f);
        mask = static_cast<int> (buffer.size()) - 1;
        write = 0;
    }

    void push (float x)
    {
        buffer[static_cast<size_t> (write)] = x;
        write = (write + 1) & mask;
    }

    /** The sample pushed `delay` samples ago (at least 1). */
    float read (float delay) const
    {
        const auto d = juce::jlimit (1.0f, static_cast<float> (mask - 1), delay);
        const auto whole = static_cast<int> (d);
        const auto frac = d - static_cast<float> (whole);
        const auto a = buffer[static_cast<size_t> ((write - whole) & mask)];
        const auto b = buffer[static_cast<size_t> ((write - whole - 1) & mask)];
        return a + (b - a) * frac;
    }

    void clear()    { std::fill (buffer.begin(), buffer.end(), 0.0f); }

private:
    std::vector<float> buffer { 2, 0.0f };
    int mask = 1, write = 0;
};

/** A one-pole filter: low-pass, or high-pass as the input minus its low-pass. */
struct OnePole
{
    float coefficient = 0, state = 0;

    void setCutoff (float hz, double sampleRate)    { coefficient = std::exp (-juce::MathConstants<float>::twoPi * hz / static_cast<float> (sampleRate)); }
    float lowpass (float x)                         { return state = x + coefficient * (state - x); }
    float highpass (float x)                        { return x - lowpass (x); }
};

//==============================================================================
/** Stereo delay, in milliseconds or synced to the song's tempo, optionally bouncing between the
    sides, with the repeats filtered so they darken and thin as they fade. Outputs only the
    repeats: the slot's mix blends in the dry signal. */
class DelayPlugin : public EffectPlugin
{
public:
    DelayPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;

    /** Note lengths for sync, in quarter notes; 0 is off (use the time in ms). */
    static const std::vector<const char*>& syncNames();
    static double syncQuarters (int index);

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Delay"; }
    double getTailLength() const override               { return 8.0; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    DelayLine lines[2];
    OnePole lowCuts[2], highCuts[2];
    juce::SmoothedValue<float> delaySamples;
};

//==============================================================================
/** Algorithmic reverb: pre-delay, input diffusion and an eight-line feedback delay network with
    damping in its loops, its decay set as the time to fall 60 dB. Outputs only the reverb. */
class ReverbPlugin : public EffectPlugin
{
public:
    ReverbPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Reverb"; }
    double getTailLength() const override               { return 20.0; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    static constexpr int numLines = 8;

    DelayLine preDelay;
    DelayLine diffusers[4];
    DelayLine lines[numLines];
    OnePole damping[numLines];
    OnePole lowCut;
    float lineLengths[numLines] {};
    float gains[numLines] {};
    float lastSize = -1, lastDecay = -1, lastDamping = -1, lastLowCut = -1;

    void updateLines (float size, float decay, float dampingHz, float lowCutHz);
};

//==============================================================================
/** Chorus: three modulated voices per side, their LFOs spread in phase and opposed between the
    sides for width. Outputs only the voices. */
class ChorusPlugin : public EffectPlugin
{
public:
    ChorusPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Chorus"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    DelayLine lines[2];
    double phase = 0;
    float feedbackState[2] {};
};

//==============================================================================
/** Distortion: drive into a shaper (soft, hard, tube, fold or crush), at four times the sample
    rate so the harmonics don't fold back, then a tone filter and output level. */
class DistortionPlugin : public EffectPlugin
{
public:
    DistortionPlugin (tracktion::PluginCreationInfo);

    static const char* xmlTypeName;
    static const std::vector<const char*>& typeNames();

    /** The shaper: what one input sample becomes, before tone and output. The UI draws it. */
    static float shape (int type, float x, float driveDb);

    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Distortion"; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    double getLatencySeconds() override;

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    OnePole tone[2];
    int maxBlock = 0;
};

} // namespace gk::effects
