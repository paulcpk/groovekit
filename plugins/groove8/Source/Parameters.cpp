#include "Parameters.h"

namespace groove8
{

namespace
{
    using Attributes = juce::AudioParameterFloatAttributes;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    // Knobs read 0 to 10, as on the panel.
    juce::String dial (float v, int)        { return juce::String (v * 10, 1); }
    juce::String decibels (float v, int)    { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String percent (float v, int)     { return juce::String (juce::roundToInt (v * 100)) + "%"; }
    juce::String semitones (float v, int)   { return (v > 0 ? "+" : "") + juce::String (v, 1) + " st"; }
    juce::String pan (float v, int)
    {
        const auto p = juce::roundToInt (v * 100);
        return p == 0 ? "C" : juce::String (std::abs (p)) + (p < 0 ? " L" : " R");
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

const std::vector<rhythm::Row>& getRows()
{
    static const std::vector<rhythm::Row> rows {
        { "bd", "BD", "Bass Drum" },
        { "sd", "SD", "Snare" },
        { "lt", "LT", "Low Tom/Conga" },
        { "mt", "MT", "Mid Tom/Conga" },
        { "ht", "HT", "High Tom/Conga" },
        { "rs", "RS", "Rim Shot/Claves" },
        { "cp", "CP", "Hand Clap/Maracas" },
        { "cb", "CB", "Cowbell" },
        { "cy", "CY", "Cymbal" },
        { "oh", "OH", "Open Hat" },
        { "ch", "CH", "Closed Hat" },
        { "ac", "AC", "Accent" },
    };
    return rows;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout layout;
    const auto& rows = getRows();

    for (int r = 0; r < numVoices; ++r)
    {
        const auto& row = rows[static_cast<size_t> (r)];
        const auto code = row.code.toRawUTF8();
        addFloat (layout, id::voice (code, "Level"), row.name + " Level", { 0, 1 }, 0.7f, dial);

        if (r == lt || r == mt || r == ht)
        {
            addFloat (layout, id::voice (code, "Tuning"), row.name + " Tuning", { 0, 1 }, 0.5f, dial);
            addChoice (layout, id::voice (code, "Switch"), row.name, tomSwitchNames, 0);
        }
        else if (r == rs)
        {
            addChoice (layout, id::voice (code, "Switch"), row.name, rimSwitchNames, 0);
        }
        else if (r == cp)
        {
            addChoice (layout, id::voice (code, "Switch"), row.name, clapSwitchNames, 0);
        }

        addFloat (layout, id::voice (code, "Pan"), row.name + " Pan", { -1, 1 }, 0, pan);
    }

    addFloat (layout, id::bdTone, "Bass Drum Tone", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::bdDecay, "Bass Drum Decay", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::bdPitch, "Bass Drum Pitch", { -12, 12 }, 0, semitones);
    addFloat (layout, id::sdTone, "Snare Tone", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::sdSnappy, "Snare Snappy", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::cyTone, "Cymbal Tone", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::cyDecay, "Cymbal Decay", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::ohDecay, "Open Hat Decay", { 0, 1 }, 0.3f, dial);

    addFloat (layout, id::accent, "Accent", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::drive, "Drive", { 0, 1 }, 0, percent);
    addFloat (layout, id::gain, "Volume", { -30, 6 }, -3, decibels);

    rhythm::addSequencerParameters (layout, rows, false);
    return layout;
}

} // namespace groove8
