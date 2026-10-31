#include "Parameters.h"

namespace groove9
{

namespace
{
    using Attributes = juce::AudioParameterFloatAttributes;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    // Knobs read 0 to 10, as on the panel.
    juce::String dial (float v, int)        { return juce::String (v * 10, 1); }
    juce::String decibels (float v, int)    { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String percent (float v, int)     { return juce::String (juce::roundToInt (v * 100)) + "%"; }
    juce::String pan (float v, int)
    {
        const auto p = juce::roundToInt (v * 100);
        return p == 0 ? "C" : juce::String (std::abs (p)) + (p < 0 ? " L" : " R");
    }

    void addFloat (Layout& layout, const juce::String& paramId, const juce::String& name, juce::NormalisableRange<float> range, float value, juce::String (*text) (float, int))
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { paramId, 1 }, name, range, value, Attributes().withStringFromValueFunction (text)));
    }
}

const std::vector<rhythm::Row>& getRows()
{
    static const std::vector<rhythm::Row> rows {
        { "bd", "BD", "Bass Drum" },
        { "sd", "SD", "Snare" },
        { "lt", "LT", "Low Tom" },
        { "mt", "MT", "Mid Tom" },
        { "ht", "HT", "High Tom" },
        { "rs", "RS", "Rim Shot" },
        { "cp", "CP", "Hand Clap" },
        { "ch", "CH", "Closed Hat" },
        { "oh", "OH", "Open Hat" },
        { "cr", "CR", "Crash" },
        { "rd", "RD", "Ride" },
        { "ac", "AC", "Accent" },
        { "fl", "FL", "Flam" },
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
        addFloat (layout, id::voice (code, "Level"), row.name + " Level", { 0, 1 }, r == cr || r == rd ? 0.6f : 0.7f, dial);

        if (r == lt || r == mt || r == ht)
        {
            addFloat (layout, id::voice (code, "Tune"), row.name + " Tune", { 0, 1 }, 0.5f, dial);
            addFloat (layout, id::voice (code, "Decay"), row.name + " Decay", { 0, 1 }, 0.5f, dial);
        }

        addFloat (layout, id::voice (code, "Pan"), row.name + " Pan", { -1, 1 }, 0, pan);
    }

    addFloat (layout, id::bdTune, "Bass Drum Tune", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::bdAttack, "Bass Drum Attack", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::bdDecay, "Bass Drum Decay", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::sdTune, "Snare Tune", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::sdTone, "Snare Tone", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::sdSnappy, "Snare Snappy", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::chDecay, "Closed Hat Decay", { 0, 1 }, 0.6f, dial);
    addFloat (layout, id::ohDecay, "Open Hat Decay", { 0, 1 }, 0.6f, dial);
    addFloat (layout, id::crTune, "Crash Tune", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::rdTune, "Ride Tune", { 0, 1 }, 0.5f, dial);

    addFloat (layout, id::accent, "Total Accent", { 0, 1 }, 0.5f, dial);
    addFloat (layout, id::drive, "Drive", { 0, 1 }, 0, percent);
    addFloat (layout, id::gain, "Volume", { -30, 6 }, -3, decibels);

    rhythm::addSequencerParameters (layout, rows, true);
    return layout;
}

} // namespace groove9
