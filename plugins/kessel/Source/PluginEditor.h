#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

namespace kessel
{

/** The kick as it sounds now, drawn on a screen: rendered again whenever a knob moves, its
    waveform, the rumble under it, the pitch it dives down, the tail's note and length, and a
    line running along it as a kick plays. */
class Display : public juce::Component
{
public:
    Display();

    void update (const Settings&);
    void setPlayhead (double secondsSinceHit);
    void paint (juce::Graphics&) override;

private:
    static constexpr double previewRate = 24000;
    Kick preview;
    Settings shown;
    std::vector<float> samples, rumble;
    double seconds = 0.5, playhead = 1e9;
};

/** A pressure gauge: the output's peak on a needle, the compressor's gain reduction in an arc. */
class Gauge : public juce::Component
{
public:
    void setValues (float peakGain, float reductionDb);
    void paint (juce::Graphics&) override;

private:
    float needle = -40, reduction = 0;
};

/** A rubber pad that plays the kick, lit as a kick plays. */
class HitPad : public juce::Component
{
public:
    explicit HitPad (juce::MidiKeyboardState&);

    void setGlow (float);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::MidiKeyboardState& keyboard;
    float glow = 0;
    bool down = false;
};

/** A metal toggle switch for a bool parameter, its name under it. */
class Toggle : public juce::Component
{
public:
    Toggle (juce::RangedAudioParameter&, const juce::String& name);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String name;
    bool on = false;
    juce::ParameterAttachment attachment;
};

/** A die: rolls a new kick. */
class Dice : public juce::Button
{
public:
    Dice();

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

/** The panel of a hard kick machine: the screen and the pressure gauge with the pad to hit, and
    plates for the click, the body, the pitch, the drive and the rumble; a die to roll a new kick. */
class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** A plate of knobs: a choice on top, a big knob and the small ones beside it. */
    struct Plate
    {
        juce::String title;
        juce::Colour colour;
        gkp::Choice* choice = nullptr;
        gkp::Knob* big = nullptr;
        std::vector<juce::Component*> small;
        int columns = 2;
        juce::Rectangle<int> area;
    };

    Processor& owner;
    gkp::Theme theme;
    gkp::LookAndFeel look;
    gkp::PresetBar presets;
    Display display;
    Gauge gauge;
    HitPad hit;
    Toggle follow;
    Dice dice;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<Plate> plates;
    std::vector<gkp::Knob*> outputKnobs;
    gkp::Knob* volume = nullptr;
    juce::Rectangle<int> header, screenArea, outputArea, gaugeArea, padArea;
    double lastHash = -1;

    gkp::Knob* knob (const juce::String& id, const juce::String& name, juce::Colour, bool bipolar = false);
    gkp::Choice* choice (const juce::String& id, const juce::StringArray& labels, juce::Colour);
    double hashParameters() const;
    void roll();
    void timerCallback() override;

    void drawPlate (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour) const;
    void drawLogo (juce::Graphics&, juce::Rectangle<int>) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace kessel
