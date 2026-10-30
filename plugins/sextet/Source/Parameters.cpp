#include "Parameters.h"
#include "Dsp.h"

namespace sextet
{

namespace
{
    using Attributes = juce::AudioParameterFloatAttributes;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    juce::String percent (float v, int)     { return juce::String (juce::roundToInt (v * 100)) + "%"; }
    juce::String bipolar (float v, int)     { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100)) + "%"; }
    juce::String cents (float v, int)       { return juce::String (juce::roundToInt (v)) + " ct"; }
    juce::String semitones (float v, int)   { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; }
    juce::String decibels (float v, int)    { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String hertz (float v, int)       { return v >= 1000 ? juce::String (v / 1000, v >= 10000 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String rate (float v, int)        { return juce::String (v, v < 10 ? 2 : 1) + " Hz"; }
    juce::String seconds (float v, int)     { return v >= 0.9995f ? juce::String (v, 2) + " s" : juce::String (juce::roundToInt (v * 1000)) + " ms"; }
    juce::String count (float v, int)       { return juce::String (juce::roundToInt (v)); }
    juce::String morphText (float v, int)   { const auto i = juce::jlimit (0, 4, juce::roundToInt (v)); return morphNames[i] + (std::abs (v - i) > 0.05f ? "~" : ""); }
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

    void addFloat (Layout& layout, const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range, float value, juce::String (*text) (float, int))
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, value, Attributes().withStringFromValueFunction (text)));
    }

    void addChoice (Layout& layout, const juce::String& id, const juce::String& name, const juce::StringArray& choices, int value)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, value));
    }

    void addBool (Layout& layout, const juce::String& id, const juce::String& name, bool value)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, value));
    }

    void addEnvelope (Layout& layout, const char* which, const juce::String& name, float a, float d, float s, float r)
    {
        addFloat (layout, id::env (which, "Attack"), name + " attack", skewed (0.001f, 10, 0.3f), a, seconds);
        addFloat (layout, id::env (which, "Decay"), name + " decay", skewed (0.005f, 10, 0.5f), d, seconds);
        addFloat (layout, id::env (which, "Sustain"), name + " sustain", { 0, 1 }, s, percent);
        addFloat (layout, id::env (which, "Release"), name + " release", skewed (0.005f, 10, 0.5f), r, seconds);
        addFloat (layout, id::env (which, "Curve"), name + " curve", { 0, 1 }, 0.6f, percent);
    }
}

juce::StringArray lfoSyncNames()
{
    juce::StringArray names { "Free" };
    names.addArray (gkp::dsp::divisionNames());
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout layout;

    // Out of the box: two saws a few cents apart through the first filter.
    for (int n = 1; n <= numOscs; ++n)
    {
        const auto name = "Osc " + juce::String (n) + " ";
        addBool (layout, id::osc (n, "On"), name + "on", n <= 2);
        addFloat (layout, id::osc (n, "Morph"), name + "shape", { 0, 4 }, 2, morphText);
        addFloat (layout, id::osc (n, "Warp"), name + "warp", { 0, 1 }, 0, percent);
        addFloat (layout, id::osc (n, "Coarse"), name + "coarse", { -36, 36, 1 }, 0, semitones);
        addFloat (layout, id::osc (n, "Fine"), name + "fine", { -100, 100 }, n == 2 ? 7.0f : 0.0f, cents);
        addFloat (layout, id::osc (n, "Level"), name + "level", { 0, 1 }, n <= 2 ? 0.8f : 0.5f, percent);
        addFloat (layout, id::osc (n, "Pan"), name + "pan", { -1, 1 }, 0, pan);
        addFloat (layout, id::osc (n, "Unison"), name + "unison", { 1, 4, 1 }, 1, count);
        addFloat (layout, id::osc (n, "Detune"), name + "detune", { 0, 60 }, 15, cents);
        addChoice (layout, id::osc (n, "ModSource"), name + "modulator", modSourceNames, 0);
        addFloat (layout, id::osc (n, "Fm"), name + "FM", { 0, 1 }, 0, percent);
        addFloat (layout, id::osc (n, "Ring"), name + "ring", { 0, 1 }, 0, percent);
        addBool (layout, id::osc (n, "Sync"), name + "sync to osc 1", false);
        addChoice (layout, id::osc (n, "Route"), name + "route", routeNames, routeFilter1);
    }

    for (int k = 1; k <= 2; ++k)
    {
        const auto name = "Filter " + juce::String (k) + " ";
        addChoice (layout, id::filter (k, "Type"), name + "type", filterTypeNames, k == 1 ? lowpass24 : filterOff);
        addFloat (layout, id::filter (k, "Cutoff"), name + "cutoff", skewed (20, 20000, 1000), k == 1 ? 3000.0f : 800.0f, hertz);
        addFloat (layout, id::filter (k, "Res"), name + "resonance", { 0, 1 }, 0.15f, percent);
        addFloat (layout, id::filter (k, "Drive"), name + "drive", { 0, 1 }, 0, percent);
        addFloat (layout, id::filter (k, "Env"), name + (k == 1 ? "env 2 amount" : "env 3 amount"), { -1, 1 }, k == 1 ? 0.3f : 0.0f, bipolar);
        addFloat (layout, id::filter (k, "Key"), name + "key tracking", { 0, 1 }, 0.5f, percent);
    }

    addChoice (layout, id::filterRouting, "Filter routing", routingNames, 0);
    addFloat (layout, id::filterBalance, "Filter balance", { -1, 1 }, 0, bipolar);

    addEnvelope (layout, "amp", "Amp", 0.003f, 0.4f, 0.8f, 0.3f);
    addEnvelope (layout, "env2", "Env 2", 0.003f, 0.35f, 0.2f, 0.3f);
    addEnvelope (layout, "env3", "Env 3", 0.5f, 1, 0.5f, 0.5f);

    for (int k = 1; k <= numLfos; ++k)
    {
        const auto name = "LFO " + juce::String (k) + " ";
        addChoice (layout, id::lfo (k, "Shape"), name + "shape", lfoShapeNames, k == 3 ? 5 : 0);
        addFloat (layout, id::lfo (k, "Rate"), name + "rate", skewed (0.02f, 40, 2), k == 1 ? 5.0f : k == 2 ? 0.5f : 0.1f, rate);
        addChoice (layout, id::lfo (k, "Sync"), name + "sync", lfoSyncNames(), 0);
    }

    for (int m = 1; m <= numMods; ++m)
    {
        const auto name = "Mod " + juce::String (m) + " ";
        addChoice (layout, id::mod (m, "Source"), name + "source", sourceNames, 0);
        addChoice (layout, id::mod (m, "Target"), name + "target", targetNames, 0);
        addFloat (layout, id::mod (m, "Amount"), name + "amount", { -1, 1 }, 0, bipolar);
    }

    addChoice (layout, id::driveType, "Drive type", driveNames, 0);
    addFloat (layout, id::drive, "Drive", { 0, 1 }, 0, percent);
    addFloat (layout, id::chorusRate, "Chorus rate", skewed (0.05f, 5, 0.5f), 0.4f, rate);
    addFloat (layout, id::chorusDepth, "Chorus depth", { 0, 1 }, 0.5f, percent);
    addFloat (layout, id::chorusMix, "Chorus mix", { 0, 1 }, 0, percent);
    addChoice (layout, id::delaySync, "Delay time", gkp::dsp::divisionNames(), 6);
    addFloat (layout, id::delayFeedback, "Delay feedback", { 0, 0.95f }, 0.4f, percent);
    addFloat (layout, id::delayMix, "Delay mix", { 0, 1 }, 0, percent);
    addFloat (layout, id::delayTone, "Delay tone", skewed (200, 16000, 3000), 5000, hertz);
    addBool (layout, id::delayPingPong, "Delay ping-pong", true);
    addFloat (layout, id::reverbSize, "Reverb size", { 0, 1 }, 0.6f, percent);
    addFloat (layout, id::reverbDamp, "Reverb damping", { 0, 1 }, 0.4f, percent);
    addFloat (layout, id::reverbMix, "Reverb mix", { 0, 1 }, 0, percent);

    addChoice (layout, id::voiceMode, "Voice mode", voiceModeNames, poly);
    addFloat (layout, id::glide, "Glide", skewed (0, 2, 0.2f), 0, seconds);
    addFloat (layout, id::velocity, "Velocity", { 0, 1 }, 0.5f, percent);
    addFloat (layout, id::bendRange, "Bend range", { 0, 24, 1 }, 2, semitones);
    addFloat (layout, id::gain, "Volume", { -36, 6 }, -8, decibels);

    return layout;
}

} // namespace sextet
