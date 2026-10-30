#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

namespace sextet
{

/** Six oscillator cards with their waves, the two filters and their routing, the three envelopes
    and three LFOs with their graphs, the effects, the matrix and the voice settings. */
class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct OscCard;
    struct ModSlot;

    Processor& owner;
    gkp::Theme theme;
    gkp::LookAndFeel look;
    gkp::PresetBar presets;
    std::unique_ptr<gkp::Knob> volume;
    std::vector<std::unique_ptr<OscCard>> oscs;
    std::vector<std::unique_ptr<ModSlot>> mods;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::unique_ptr<gkp::ChoiceBox> filterType[2], delaySync, lfoSync[3];
    std::unique_ptr<gkp::Choice> routing, driveType, voiceMode, lfoShape[3];
    std::unique_ptr<juce::TextButton> pingPong;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> pingPongAttachment;
    std::vector<juce::Component*> filterKnobs[2], envKnobs[3], lfoKnobs[3], fxKnobs, fxKnobs2, voiceKnobs;
    gkp::Knob* balance = nullptr;
    juce::Rectangle<int> filterArea[2], filterGraph[2], routingArea, envArea[3], envGraph[3], lfoArea[3], lfoGraph[3], fxArea, voiceArea, matrixArea, scopeArea;
    std::array<float, 1024> scope {};
    double animation = 0;

    gkp::Knob* knob (const juce::String& id, const juce::String& name, juce::Colour, bool bipolar = false);
    float get (const juce::String& id) const   { return owner.state.getRawParameterValue (id)->load(); }

    void timerCallback() override;
    void drawLfo (juce::Graphics&, juce::Rectangle<float>, int k) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace sextet
