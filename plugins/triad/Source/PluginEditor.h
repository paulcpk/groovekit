#pragma once

#include "PluginProcessor.h"

namespace triad
{

/** Colours, from GrooveKit's own. */
namespace colours
{
    inline const juce::Colour bg { 0xff111113 }, panel { 0xff18181b }, raised { 0xff202024 }, control { 0xff29292e };
    inline const juce::Colour line { 0xff2c2c31 }, fg { 0xffd6d6db }, dim { 0xff8d8d96 }, faint { 0xff5b5b64 }, screen { 0xff0c0c0e };
    inline const juce::Colour accent { 0xfff2a33a };
    inline const juce::Colour osc[3] { juce::Colour (0xfff2a33a), juce::Colour (0xff58b7d6), juce::Colour (0xffc47ef0) };
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override                { return juce::FontOptions (13.0f); }
    juce::Font getPopupMenuFont() override                               { return juce::FontOptions (13.0f); }
};

/** A knob for a parameter, with its name and value under it. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name, juce::Colour, bool bipolar = false);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String name;
};

/** A segmented control for a choice parameter: text, or the oscillator shapes as pictures. */
class Choice : public juce::Component
{
public:
    Choice (juce::RangedAudioParameter&, juce::StringArray labels, juce::Colour, bool shapes = false);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray labels;
    juce::Colour colour;
    bool shapes;
    int selected = 0;
    juce::ParameterAttachment attachment;
};

//==============================================================================
class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct OscRow;

    Processor& owner;
    LookAndFeel look;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };
    std::unique_ptr<Knob> volume;
    std::vector<std::unique_ptr<OscRow>> oscillators;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<Choice> filterType, lfoShape, lfoTarget, voiceMode;
    std::vector<Knob*> filterKnobs, ampKnobs, filterEnvKnobs, lfoKnobs, voiceKnobs;
    juce::Rectangle<int> scopeArea, filterArea, filterGraph, ampArea, ampGraph, filterEnvArea, filterEnvGraph, lfoArea, voiceArea;
    std::array<float, 1024> scope {};
    double animation = 0;

    Knob* knob (const juce::String& id, const juce::String& name, juce::Colour, bool bipolar = false);
    float get (const juce::String& id) const   { return owner.state.getRawParameterValue (id)->load(); }

    void timerCallback() override;
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour) const;
    void drawScope (juce::Graphics&, juce::Rectangle<float>) const;
    void drawFilter (juce::Graphics&, juce::Rectangle<float>) const;
    void drawEnvelope (juce::Graphics&, juce::Rectangle<float>, const juce::String ids[4], juce::Colour) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace triad
