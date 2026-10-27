#include "Parameters.h"

namespace triad
{

namespace
{
    using Attributes = juce::AudioParameterFloatAttributes;

    juce::String percent (float v, int)     { return juce::String (juce::roundToInt (v * 100)) + "%"; }
    juce::String cents (float v, int)       { return juce::String (juce::roundToInt (v)) + " ct"; }
    juce::String semitones (float v, int)   { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; }
    juce::String decibels (float v, int)    { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String hertz (float v, int)       { return v >= 1000 ? juce::String (v / 1000, v >= 10000 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String rate (float v, int)        { return juce::String (v, v < 10 ? 2 : 1) + " Hz"; }
    juce::String seconds (float v, int)     { return v >= 1 ? juce::String (v, 2) + " s" : juce::String (juce::roundToInt (v * 1000)) + " ms"; }

    juce::String pan (float v, int)
    {
        const auto p = juce::roundToInt (v * 100);
        return p == 0 ? "C" : juce::String (std::abs (p)) + (p < 0 ? " L" : " R");
    }

    juce::NormalisableRange<float> skewed (float min, float max, float centre)
    {
        juce::NormalisableRange<float> r (min, max);
        r.setSkewForCentre (centre);
        return r;
    }

    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    void addFloat (Layout& layout, const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range, float value,
                   juce::String (*text) (float, int))
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, value, Attributes().withStringFromValueFunction (text)));
    }

    void addChoice (Layout& layout, const juce::String& id, const juce::String& name, const juce::StringArray& choices, int value)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, value));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout layout;

    // Out of the box: two saws a few cents apart and a square an octave down.
    const int shapes[] { saw, saw, square };
    const float levels[] { 1.0f, 0.8f, 0.45f }, coarse[] { 0, 0, -12 }, fine[] { 0, 7, 0 };

    for (int n = 1; n <= 3; ++n)
    {
        const auto name = "Osc " + juce::String (n) + " ";
        addChoice (layout, id::osc (n, "Shape"), name + "shape", shapeNames, shapes[n - 1]);
        addFloat (layout, id::osc (n, "Coarse"), name + "coarse", { -24, 24, 1 }, coarse[n - 1], semitones);
        addFloat (layout, id::osc (n, "Fine"), name + "fine", { -100, 100 }, fine[n - 1], cents);
        addFloat (layout, id::osc (n, "Level"), name + "level", { 0, 1 }, levels[n - 1], percent);
        addFloat (layout, id::osc (n, "Pan"), name + "pan", { -1, 1 }, 0, pan);
        addFloat (layout, id::osc (n, "Phase"), name + "phase", { 0, 1 }, 0, percent);
        addFloat (layout, id::osc (n, "Detune"), name + "stereo detune", { 0, 50 }, 0, cents);

        if (n > 1)
            addChoice (layout, id::osc (n, "Mod"), name + "into osc " + juce::String (n - 1), modNames, mix);
    }

    addFloat (layout, id::unisonVoices, "Unison voices", { 1, 7, 1 }, 1, [] (float v, int) { return juce::String (juce::roundToInt (v)); });
    addFloat (layout, id::unisonDetune, "Unison detune", { 0, 100 }, 15, cents);
    addFloat (layout, id::unisonSpread, "Unison spread", { 0, 1 }, 0.6f, percent);

    addChoice (layout, id::filterType, "Filter", filterNames, lowpass24);
    addFloat (layout, id::cutoff, "Cutoff", skewed (20, 20000, 1000), 4000, hertz);
    addFloat (layout, id::resonance, "Resonance", { 0, 1 }, 0.15f, percent);
    addFloat (layout, id::filterEnv, "Filter envelope", { -1, 1 }, 0.25f, percent);
    addFloat (layout, id::keyTrack, "Key tracking", { 0, 1 }, 0.5f, percent);

    addFloat (layout, id::ampAttack, "Attack", skewed (0.001f, 10, 0.3f), 0.003f, seconds);
    addFloat (layout, id::ampDecay, "Decay", skewed (0.005f, 10, 0.5f), 0.4f, seconds);
    addFloat (layout, id::ampSustain, "Sustain", { 0, 1 }, 0.8f, percent);
    addFloat (layout, id::ampRelease, "Release", skewed (0.005f, 10, 0.5f), 0.25f, seconds);

    addFloat (layout, id::filterAttack, "Filter attack", skewed (0.001f, 10, 0.3f), 0.003f, seconds);
    addFloat (layout, id::filterDecay, "Filter decay", skewed (0.005f, 10, 0.5f), 0.35f, seconds);
    addFloat (layout, id::filterSustain, "Filter sustain", { 0, 1 }, 0.2f, percent);
    addFloat (layout, id::filterRelease, "Filter release", skewed (0.005f, 10, 0.5f), 0.3f, seconds);

    addChoice (layout, id::lfoShape, "LFO shape", lfoShapeNames, 0);
    addFloat (layout, id::lfoRate, "LFO rate", skewed (0.05f, 20, 2), 4, rate);
    addChoice (layout, id::lfoTarget, "LFO target", lfoTargetNames, lfoOff);
    addFloat (layout, id::lfoDepth, "LFO depth", { 0, 1 }, 0.3f, percent);

    addChoice (layout, id::voiceMode, "Voice mode", voiceModeNames, poly);
    addFloat (layout, id::glide, "Glide", skewed (0, 2, 0.2f), 0, seconds);
    addFloat (layout, id::velocity, "Velocity", { 0, 1 }, 0.6f, percent);
    addFloat (layout, id::bendRange, "Bend range", { 0, 24, 1 }, 2, semitones);
    addFloat (layout, id::gain, "Volume", { -36, 6 }, -6, decibels);

    return layout;
}

} // namespace triad
