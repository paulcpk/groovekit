#include "Parameters.h"

namespace kessel
{

namespace
{
    using Attributes = juce::AudioParameterFloatAttributes;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    juce::String percent (float v, int)     { return juce::String (juce::roundToInt (v * 100)) + "%"; }
    juce::String semitones (float v, int)   { return (v > 0 ? "+" : "") + juce::String (v, std::abs (v) < 10 ? 1 : 0) + " st"; }
    juce::String decibels (float v, int)    { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String hertz (float v, int)       { return v >= 1000 ? juce::String (v / 1000, v >= 10000 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String millis (float v, int)      { return v >= 1000 ? juce::String (v / 1000, 2) + " s" : juce::String (v, v < 10 ? 1 : 0) + " ms"; }
    juce::String seconds (float v, int)     { return juce::String (v, 2) + " s"; }
    juce::String note (float v, int)        { return noteName (v); }

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

juce::String noteName (float note)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto whole = juce::roundToInt (note);
    const auto cents = juce::roundToInt ((note - static_cast<float> (whole)) * 100);
    auto text = juce::String (names[((whole % 12) + 12) % 12]) + juce::String (whole / 12 - 1);
    return cents == 0 ? text : text + (cents > 0 ? " +" : " ") + juce::String (cents);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout layout;

    // Out of the box: a punchy, lightly driven techno kick on F1.
    addChoice (layout, id::clickType, "Click type", clickNames, clickTok);
    addFloat (layout, id::clickLevel, "Click level", { 0, 1 }, 0.35f, percent);
    addFloat (layout, id::clickTone, "Click tone", skewed (300, 12000, 2500), 2500, hertz);
    addFloat (layout, id::clickDecay, "Click decay", skewed (1, 80, 12), 10, millis);

    addChoice (layout, id::bodyShape, "Body shape", shapeNames, shapeSine);
    addFloat (layout, id::tune, "Tune", { 22, 46 }, 29, note);
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::followKeys, 1 }, "Follow keys", true));
    addFloat (layout, id::hold, "Hold", skewed (0, 1500, 120), 30, millis);
    addFloat (layout, id::decay, "Decay", skewed (20, 3000, 400), 380, millis);
    addFloat (layout, id::sub, "Sub", { 0, 1 }, 0.25f, percent);

    addFloat (layout, id::pitchStart, "Pitch start", { 0, 72 }, 38, semitones);
    addFloat (layout, id::pitchTime, "Pitch time", skewed (2, 400, 40), 38, millis);
    addFloat (layout, id::pitchCurve, "Pitch curve", { 0, 1 }, 0.45f, percent);
    addFloat (layout, id::glide, "Glide", { -24, 12 }, 0, semitones);

    addChoice (layout, id::driveType, "Drive type", driveNames, driveWarm);
    addFloat (layout, id::drive, "Drive", { 0, 48 }, 8, decibels);
    addFloat (layout, id::crunch, "Crunch", { 0, 24 }, 0, decibels);
    addFloat (layout, id::crunchFreq, "Crunch freq", skewed (150, 5000, 900), 900, hertz);
    addFloat (layout, id::tone, "Tone", skewed (200, 20000, 3000), 14000, hertz);

    addFloat (layout, id::rumble, "Rumble", { 0, 1 }, 0, percent);
    addFloat (layout, id::rumbleSize, "Rumble size", skewed (0.2f, 6, 1.5f), 1.4f, seconds);
    addFloat (layout, id::rumbleTone, "Rumble tone", skewed (40, 600, 150), 140, hertz);
    addFloat (layout, id::rumbleDrive, "Rumble drive", { 0, 1 }, 0.35f, percent);
    addFloat (layout, id::rumbleDuck, "Rumble duck", { 0, 1 }, 0.85f, percent);

    addFloat (layout, id::compress, "Compress", { 0, 1 }, 0.2f, percent);
    addFloat (layout, id::clip, "Clip", { 0, 1 }, 0.15f, percent);
    addFloat (layout, id::lowCut, "Low cut", skewed (10, 120, 30), 20, hertz);
    addFloat (layout, id::velocity, "Velocity", { 0, 1 }, 0.3f, percent);
    addFloat (layout, id::gain, "Volume", { -36, 6 }, -4, decibels);

    return layout;
}

} // namespace kessel
