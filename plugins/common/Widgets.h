#pragma once

#include "Look.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace gkp
{

/** A knob for a parameter, with its name and value under it. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name, juce::Colour, const Theme&, bool bipolar = false);

    void resized() override;
    void paint (juce::Graphics&) override;

    juce::Slider slider;

private:
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String name;
    const Theme& theme;
};

/** A segmented control for a choice parameter. A painter draws a cell's picture instead of
    its label (index, area, on). */
class Choice : public juce::Component
{
public:
    using Painter = std::function<void (juce::Graphics&, int index, juce::Rectangle<float>, bool on)>;

    Choice (juce::RangedAudioParameter&, juce::StringArray labels, juce::Colour, const Theme&, Painter = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    int getSelected() const     { return selected; }

private:
    juce::StringArray labels;
    juce::Colour colour;
    const Theme& theme;
    Painter painter;
    int selected = 0;
    juce::ParameterAttachment attachment;
};

/** A drop-down for a choice parameter with many options. */
class ChoiceBox : public juce::Component
{
public:
    ChoiceBox (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::StringArray& labels);

    void resized() override    { box.setBounds (getLocalBounds()); }

    juce::ComboBox box;

private:
    juce::AudioProcessorValueTreeState::ComboBoxAttachment attachment;
};

/** Two parameters on a square: drag the dot. The axes' names and values are written in the
    corners; double-click puts both back to their defaults. */
class XYPad : public juce::Component
{
public:
    XYPad (juce::RangedAudioParameter& x, juce::RangedAudioParameter& y, juce::Colour, const Theme&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Drawn under the dot: the effect's picture of itself. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>)> background;

private:
    juce::RangedAudioParameter& xParam;
    juce::RangedAudioParameter& yParam;
    juce::ParameterAttachment xAttachment, yAttachment;
    juce::Colour colour;
    const Theme& theme;
    float x = 0.5f, y = 0.5f;
    bool dragging = false;

    juce::Rectangle<float> field() const    { return getLocalBounds().toFloat().reduced (10); }
    void moveTo (juce::Point<float>);
};

/** The preset list with previous and next, following the processor's programs. */
class PresetBar : public juce::Component, private juce::Timer
{
public:
    PresetBar (juce::AudioProcessor&, const Theme&);

    void resized() override;

private:
    juce::AudioProcessor& processor;
    juce::ComboBox presets;
    juce::TextButton previous { "<" }, next { ">" };

    void timerCallback() override;
};

/** A keyboard to play the plugin from its window, with the panel's colours. */
class Keyboard : public juce::MidiKeyboardComponent
{
public:
    Keyboard (juce::MidiKeyboardState&, const Theme&);

protected:
    void drawWhiteNote (int midiNoteNumber, juce::Graphics&, juce::Rectangle<float>, bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int midiNoteNumber, juce::Graphics&, juce::Rectangle<float>, bool isDown, bool isOver, juce::Colour noteFillColour) override;

private:
    const Theme& theme;
};

/** Lays knobs out side by side across an area. */
void layoutRow (juce::Rectangle<int> area, const std::vector<juce::Component*>&);

} // namespace gkp
