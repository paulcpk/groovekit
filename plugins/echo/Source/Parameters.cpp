#include "Parameters.h"
#include "Dsp.h"

namespace echo
{

namespace
{
    using Attributes = juce::AudioParameterFloatAttributes;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    juce::String percent (float v, int)     { return juce::String (juce::roundToInt (v * 100)) + "%"; }
    juce::String hertz (float v, int)       { return v >= 1000 ? juce::String (v / 1000, v >= 10000 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String rate (float v, int)        { return juce::String (v, 2) + " Hz"; }
    juce::String millis (float v, int)      { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String ratioText (float v, int)   { return juce::String (juce::roundToInt (v * 100)) + "%"; }

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
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::mode, 1 }, "Mode", modeNames, stereo));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::sync, 1 }, "Sync", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::division, 1 }, "Division", gkp::dsp::divisionNames(), 6));
    addFloat (layout, id::timeMs, "Time", skewed (1, 2000, 300), 375, millis);
    addFloat (layout, id::ratio, "Right ratio", skewed (0.25f, 2, 1), 1, ratioText);
    addFloat (layout, id::feedback, "Feedback", { 0, 1 }, 0.45f, percent);
    addFloat (layout, id::crossfeed, "Crossfeed", { 0, 1 }, 0, percent);
    addFloat (layout, id::lowCut, "Low cut", skewed (20, 2000, 200), 120, hertz);
    addFloat (layout, id::highCut, "High cut", skewed (500, 20000, 5000), 6000, hertz);
    addFloat (layout, id::saturation, "Saturation", { 0, 1 }, 0.2f, percent);
    addFloat (layout, id::wow, "Wow", { 0, 1 }, 0.15f, percent);
    addFloat (layout, id::wowRate, "Wow rate", skewed (0.1f, 5, 0.8f), 0.5f, rate);
    addFloat (layout, id::diffusion, "Diffusion", { 0, 1 }, 0, percent);
    addFloat (layout, id::duck, "Duck", { 0, 1 }, 0, percent);
    addFloat (layout, id::width, "Width", { 0, 1 }, 1, percent);
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::freeze, 1 }, "Freeze", false));
    addFloat (layout, id::mix, "Mix", { 0, 1 }, 0.3f, percent);

    juce::StringArray padNames;
    for (auto& p : padParams) padNames.add (p);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::padX, 1 }, "Pad X", padNames, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::padY, 1 }, "Pad Y", padNames, 1));
    return layout;
}

} // namespace echo
