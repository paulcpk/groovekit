#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

namespace quartet
{

/** The panel of a classic digital synth: an amber display with the preset, the algorithm and
    the chord, four operator strips with capped knobs, the analogue layer, filter, envelopes,
    chord memory, LFO, chorus and voice sections, and a keyboard to play it from. */
class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct OpStrip;

    Processor& owner;
    gkp::Theme theme;
    gkp::LookAndFeel look;
    gkp::PresetBar presets;
    std::unique_ptr<gkp::Knob> volume;
    std::vector<std::unique_ptr<OpStrip>> ops;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<gkp::Choice> vaShape, filterType, lfoShape, chorusMode, voiceMode;
    std::unique_ptr<gkp::ChoiceBox> algorithm, chord, lfoTarget;
    std::vector<juce::Component*> vaKnobs, mixKnobs, filterKnobs, ampKnobs, filterEnvKnobs, chordKnobs, lfoKnobs, chorusKnobs, voiceKnobs;
    gkp::Knob* feedback = nullptr;
    gkp::Keyboard keyboard;
    juce::Rectangle<int> lcd, vaArea, filterArea, filterGraph, ampArea, ampGraph, filterEnvArea, filterEnvGraph, mixArea,
                         algorithmArea, chordArea, lfoArea, chorusArea, voiceArea, keyboardArea;
    std::array<float, 512> scope {};

    gkp::Knob* knob (const juce::String& id, const juce::String& name, juce::Colour, bool bipolar = false);
    float get (const juce::String& id) const   { return owner.state.getRawParameterValue (id)->load(); }

    void timerCallback() override;
    void drawLcd (juce::Graphics&) const;
    void drawAlgorithm (juce::Graphics&, juce::Rectangle<float>) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace quartet
