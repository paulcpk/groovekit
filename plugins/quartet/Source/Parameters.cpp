#include "Parameters.h"

namespace quartet
{

namespace
{
    using Attributes = juce::AudioParameterFloatAttributes;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    juce::String percent (float v, int)     { return juce::String (juce::roundToInt (v * 100)) + "%"; }
    juce::String cents (float v, int)       { return juce::String (juce::roundToInt (v)) + " ct"; }
    juce::String semitones (float v, int)   { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; }
    juce::String decibels (float v, int)    { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String hertz (float v, int)       { return v >= 1000 ? juce::String (v / 1000, v >= 10000 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String rate (float v, int)        { return juce::String (v, v < 10 ? 2 : 1) + " Hz"; }
    juce::String seconds (float v, int)     { return v >= 1 ? juce::String (v, 2) + " s" : juce::String (juce::roundToInt (v * 1000)) + " ms"; }
    juce::String ratio (float v, int)       { return juce::String (v, 2); }
    juce::String count (float v, int)       { return juce::String (juce::roundToInt (v)); }
    juce::String bipolarPercent (float v, int) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100)) + "%"; }

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
}

std::vector<int> chordIntervals (int chord)
{
    switch (chord)
    {
        case chordOctave:     return { 0, 12 };
        case chordFifth:      return { 0, 7 };
        case chordMajor:      return { 0, 4, 7 };
        case chordMinor:      return { 0, 3, 7 };
        case chordSus2:       return { 0, 2, 7 };
        case chordSus4:       return { 0, 5, 7 };
        case chordMajor7:     return { 0, 4, 7, 11 };
        case chordMinor7:     return { 0, 3, 7, 10 };
        case chordDominant7:  return { 0, 4, 7, 10 };
        case chordMinor9:     return { 0, 3, 7, 10, 14 };
        case chordHouse:      return { 0, 7, 10, 15, 19 };     // a minor 9th voiced wide, the house stab
        case chordFourths:    return { 0, 5, 10 };
        default:              return { 0 };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout layout;

    addChoice (layout, id::algorithm, "Algorithm", algorithmNames(), 0);
    addFloat (layout, id::feedback, "Feedback", { 0, 1 }, 0.2f, percent);
    addFloat (layout, id::fmLevel, "FM level", { 0, 1 }, 1, percent);
    addFloat (layout, id::vaLevel, "Analogue level", { 0, 1 }, 0, percent);

    // Out of the box: an electric-piano-like stab, op 2 a bright modulator on a sine carrier.
    const float ratios[] { 1, 1, 2, 1 }, levels[] { 1, 0.55f, 0, 0 }, decays[] { 1.2f, 0.5f, 0.4f, 0.4f }, sustains[] { 0.2f, 0, 0, 0 };

    for (int n = 1; n <= 4; ++n)
    {
        const auto name = "Op " + juce::String (n) + " ";
        addChoice (layout, id::op (n, "Wave"), name + "wave", waveNames, sine);
        addFloat (layout, id::op (n, "Ratio"), name + "ratio", { 0.25f, 16, 0.25f }, ratios[n - 1], ratio);
        addFloat (layout, id::op (n, "Fine"), name + "fine", { -50, 50 }, 0, cents);
        addFloat (layout, id::op (n, "Level"), name + "level", { 0, 1 }, levels[n - 1], percent);
        addFloat (layout, id::op (n, "Attack"), name + "attack", skewed (0.001f, 8, 0.2f), 0.002f, seconds);
        addFloat (layout, id::op (n, "Decay"), name + "decay", skewed (0.005f, 10, 0.5f), decays[n - 1], seconds);
        addFloat (layout, id::op (n, "Sustain"), name + "sustain", { 0, 1 }, sustains[n - 1], percent);
        addFloat (layout, id::op (n, "Release"), name + "release", skewed (0.005f, 10, 0.5f), 0.3f, seconds);
        addFloat (layout, id::op (n, "Velocity"), name + "velocity", { 0, 1 }, n == 1 ? 0.3f : 0.6f, percent);
        addFloat (layout, id::op (n, "KeyScale"), name + "key scaling", { -1, 1 }, 0, bipolarPercent);
    }

    addChoice (layout, id::vaShape, "Analogue shape", vaShapeNames, vaSaw);
    addFloat (layout, id::vaCoarse, "Analogue coarse", { -24, 24, 1 }, 0, semitones);
    addFloat (layout, id::vaFine, "Analogue fine", { -100, 100 }, 0, cents);
    addFloat (layout, id::vaWidth, "Pulse width", { 0.05f, 0.95f }, 0.5f, percent);
    addFloat (layout, id::vaSub, "Sub", { 0, 1 }, 0, percent);
    addFloat (layout, id::vaUnison, "Analogue unison", { 1, 3, 1 }, 1, count);
    addFloat (layout, id::vaDetune, "Analogue detune", { 0, 50 }, 12, cents);

    addChoice (layout, id::chord, "Chord", chordNames, chordOff);
    addFloat (layout, id::chordSpread, "Chord spread", { 0, 1 }, 0, percent);
    addFloat (layout, id::chordDetune, "Chord detune", { 0, 30 }, 4, cents);

    addChoice (layout, id::filterType, "Filter", filterNames, lowpass12);
    addFloat (layout, id::cutoff, "Cutoff", skewed (20, 20000, 1000), 8000, hertz);
    addFloat (layout, id::resonance, "Resonance", { 0, 1 }, 0.1f, percent);
    addFloat (layout, id::filterEnv, "Filter envelope", { -1, 1 }, 0, bipolarPercent);
    addFloat (layout, id::keyTrack, "Key tracking", { 0, 1 }, 0.5f, percent);
    addFloat (layout, id::filterAttack, "Filter attack", skewed (0.001f, 10, 0.3f), 0.003f, seconds);
    addFloat (layout, id::filterDecay, "Filter decay", skewed (0.005f, 10, 0.5f), 0.3f, seconds);
    addFloat (layout, id::filterSustain, "Filter sustain", { 0, 1 }, 0, percent);
    addFloat (layout, id::filterRelease, "Filter release", skewed (0.005f, 10, 0.5f), 0.3f, seconds);

    addFloat (layout, id::ampAttack, "Attack", skewed (0.001f, 10, 0.3f), 0.002f, seconds);
    addFloat (layout, id::ampDecay, "Decay", skewed (0.005f, 10, 0.5f), 0.5f, seconds);
    addFloat (layout, id::ampSustain, "Sustain", { 0, 1 }, 1, percent);
    addFloat (layout, id::ampRelease, "Release", skewed (0.005f, 10, 0.5f), 0.3f, seconds);

    addChoice (layout, id::lfoShape, "LFO shape", lfoShapeNames, 0);
    addFloat (layout, id::lfoRate, "LFO rate", skewed (0.05f, 20, 2), 5, rate);
    addChoice (layout, id::lfoTarget, "LFO target", lfoTargetNames, lfoOff);
    addFloat (layout, id::lfoDepth, "LFO depth", { 0, 1 }, 0.3f, percent);

    addChoice (layout, id::chorusMode, "Chorus", chorusNames, 0);
    addFloat (layout, id::chorusDepth, "Chorus depth", { 0, 1 }, 0.6f, percent);
    addFloat (layout, id::drive, "Drive", { 0, 1 }, 0, percent);
    addFloat (layout, id::tone, "Tone", { -1, 1 }, 0, bipolarPercent);

    addChoice (layout, id::voiceMode, "Voice mode", voiceModeNames, poly);
    addFloat (layout, id::glide, "Glide", skewed (0, 2, 0.2f), 0, seconds);
    addFloat (layout, id::velocity, "Velocity", { 0, 1 }, 0.5f, percent);
    addFloat (layout, id::bendRange, "Bend range", { 0, 24, 1 }, 2, semitones);
    addFloat (layout, id::gain, "Volume", { -36, 6 }, -8, decibels);

    return layout;
}

} // namespace quartet
