#pragma once

#include "Presets.h"
#include "Widgets.h"

namespace gkp
{

/** The window of an effect built around an X/Y pad: the pad on the left, with two drop-downs
    choosing what its axes move, and the effect's knobs, toggles and choices in a grid on the
    right. A subclass gives the lists and draws the pad's background. */
class PadEffectEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    struct KnobSpec { juce::String id, label; bool bipolar = false; };
    struct ToggleSpec { juce::String id, label; };
    struct ChoiceSpec { juce::String id; juce::StringArray labels; };

    /** padParams: the float parameters the pad's axes can be given (ids). xId and yId: the
        parameters holding the current choice (choice parameters whose options are padParams'
        names, in order). */
    PadEffectEditor (PresetProcessor&, const juce::String& name, const juce::String& subtitle, juce::Colour accent,
                     std::vector<KnobSpec> knobs, std::vector<ToggleSpec> toggles, std::vector<ChoiceSpec> choices,
                     std::vector<juce::String> padParams, const juce::String& xId, const juce::String& yId);
    ~PadEffectEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    PresetProcessor& owner;
    Theme theme;
    juce::Colour accent;

    /** The effect's picture of itself under the pad's grid. */
    virtual void drawPad (juce::Graphics&, juce::Rectangle<float>) = 0;

    /** Drawn every frame in the pad's corner: a meter, a hint. */
    virtual void drawStatus (juce::Graphics&, juce::Rectangle<int>) {}

    float get (const juce::String& id) const   { return owner.state.getRawParameterValue (id)->load(); }

private:
    const juce::String title, subtitle;
    LookAndFeel look;
    PresetBar presets;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<juce::TextButton>> toggles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAttachments;
    std::vector<std::unique_ptr<ChoiceBox>> choices;
    std::vector<juce::String> padParams;
    std::unique_ptr<ChoiceBox> xChoice, yChoice;
    std::unique_ptr<XYPad> pad;
    const juce::String xId, yId;
    int padX = -1, padY = -1;
    juce::Rectangle<int> padArea, controlsArea, statusArea;

    void timerCallback() override;
    void rebuildPad();
};

} // namespace gkp
