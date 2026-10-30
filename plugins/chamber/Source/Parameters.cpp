#include "Parameters.h"

namespace chamber
{

namespace
{
    using Attributes = juce::AudioParameterFloatAttributes;
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

    juce::String percent (float v, int)     { return juce::String (juce::roundToInt (v * 100)) + "%"; }
    juce::String hertz (float v, int)       { return v >= 1000 ? juce::String (v / 1000, v >= 10000 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String rate (float v, int)        { return juce::String (v, 2) + " Hz"; }
    juce::String seconds (float v, int)     { return v >= 1 ? juce::String (v, v >= 10 ? 1 : 2) + " s" : juce::String (juce::roundToInt (v * 1000)) + " ms"; }
    juce::String millis (float v, int)      { return juce::String (juce::roundToInt (v)) + " ms"; }

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
    addFloat (layout, id::size, "Size", { 0, 1 }, 0.5f, percent);
    addFloat (layout, id::decay, "Decay", skewed (0.1f, 30, 2.5f), 2.5f, seconds);
    addFloat (layout, id::predelay, "Pre-delay", { 0, 250 }, 10, millis);
    addFloat (layout, id::damping, "Damping", skewed (1000, 20000, 6000), 7000, hertz);
    addFloat (layout, id::lowCut, "Low cut", skewed (20, 1000, 120), 80, hertz);
    addFloat (layout, id::highCut, "High cut", skewed (1000, 20000, 8000), 16000, hertz);
    addFloat (layout, id::diffusion, "Diffusion", { 0, 1 }, 0.7f, percent);
    addFloat (layout, id::modRate, "Mod rate", skewed (0.1f, 5, 0.8f), 0.5f, rate);
    addFloat (layout, id::modDepth, "Mod depth", { 0, 1 }, 0.3f, percent);
    addFloat (layout, id::early, "Early reflections", { 0, 1 }, 0.4f, percent);
    addFloat (layout, id::width, "Width", { 0, 1 }, 1, percent);
    addFloat (layout, id::duck, "Duck", { 0, 1 }, 0, percent);
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::freeze, 1 }, "Freeze", false));
    addFloat (layout, id::mix, "Mix", { 0, 1 }, 0.35f, percent);

    juce::StringArray padNames;
    for (auto& p : padParams) padNames.add (p);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::padX, 1 }, "Pad X", padNames, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::padY, 1 }, "Pad Y", padNames, 1));
    return layout;
}

} // namespace chamber
