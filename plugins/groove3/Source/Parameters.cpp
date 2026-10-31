#include "Parameters.h"

namespace groove3
{

namespace
{
    using Attributes = juce::AudioParameterFloatAttributes;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    juce::String percent (float v, int)     { return juce::String (juce::roundToInt (v * 100)) + "%"; }
    juce::String semitones (float v, int)   { return (v > 0 ? "+" : "") + juce::String (v, 2) + " st"; }
    juce::String decibels (float v, int)    { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String millis (float v, int)      { return v >= 1000 ? juce::String (v / 1000, 2) + " s" : juce::String (v, v < 10 ? 1 : 0) + " ms"; }
    juce::String ampMillis (float v, int)   { return v >= ampDecayHold - 1 ? juce::String ("Hold") : millis (v, 0); }
    juce::String steps (float v, int)       { return juce::String (juce::roundToInt (v)); }

    juce::NormalisableRange<float> skewed (float min, float max, float centre)
    {
        juce::NormalisableRange<float> r (min, max);
        r.setSkewForCentre (centre);
        return r;
    }

    void addFloat (Layout& layout, const juce::String& paramId, const juce::String& name, juce::NormalisableRange<float> range, float value, juce::String (*text) (float, int))
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { paramId, 1 }, name, range, value, Attributes().withStringFromValueFunction (text)));
    }

    void addChoice (Layout& layout, const juce::String& paramId, const juce::String& name, const juce::StringArray& choices, int value)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { paramId, 1 }, name, choices, value));
    }
}

juce::String noteName (int note)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout layout;

    // Out of the box: the classic squelch, as the unmodified machine sounds.
    addFloat (layout, id::tuning, "Tuning", { -12, 12 }, 0, semitones);
    addChoice (layout, id::waveform, "Waveform", waveformNames, saw);
    addFloat (layout, id::cutoff, "Cutoff", { 0, 1 }, 0.42f, percent);
    addFloat (layout, id::resonance, "Resonance", { 0, 1 }, 0.7f, percent);
    addFloat (layout, id::envMod, "Env mod", { 0, 1 }, 0.5f, percent);
    addFloat (layout, id::decay, "Decay", skewed (30, 3000, 400), 450, millis);
    addFloat (layout, id::accent, "Accent", { 0, 1 }, 0.6f, percent);
    addFloat (layout, id::volume, "Volume", { -36, 6 }, -3, decibels);

    addFloat (layout, id::overdrive, "Overdrive", { 0, 1 }, 0.25f, percent);
    addFloat (layout, id::accentDecay, "Accent decay", skewed (30, 3000, 400), 200, millis);
    addFloat (layout, id::ampDecay, "Volume decay", skewed (16, ampDecayHold, 600), 3000, ampMillis);
    addFloat (layout, id::softAttack, "Soft attack", skewed (0.3f, 30, 4), 3, millis);
    addFloat (layout, id::slideTime, "Slide time", skewed (15, 300, 70), 60, millis);
    addFloat (layout, id::tracking, "Filter tracking", { 0, 1 }, 0, percent);
    addFloat (layout, id::filterFm, "Filter FM", { 0, 1 }, 0, percent);
    addFloat (layout, id::sub, "Sub", { 0, 1 }, 0, percent);
    addChoice (layout, id::subOctave, "Sub octave", subOctaveNames, 0);
    addChoice (layout, id::sweepSpeed, "Sweep speed", sweepSpeedNames, sweepNormal);
    addChoice (layout, id::resonanceRange, "Resonance range", resonanceRangeNames, 0);
    addChoice (layout, id::muffler, "Muffler", mufflerNames, mufflerOff);
    addFloat (layout, id::distortion, "Distortion", { 0, 1 }, 0, percent);

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::run, 1 }, "Run", true));
    addFloat (layout, id::length, "Length", { 1, maxSteps, 1 }, 16, steps);

    return layout;
}

} // namespace groove3
