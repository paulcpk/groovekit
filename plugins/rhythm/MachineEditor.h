#pragma once

#include "MachineLook.h"
#include "MachineProcessor.h"
#include "Widgets.h"

namespace rhythm
{

/** A knob on the panel: its parameter, the word printed above it, and whether it is one of the
    small ones (pans and extras). */
struct KnobSpec
{
    juce::String paramId, label;
    bool small = false, bipolar = false;
};

/** An instrument's column: its row, its name (two for a switched pair, with the switch's
    parameter), its knobs in rows from the top (the first row larger; two knobs share a row
    side by side; an empty row leaves a gap, so the rows line up across the panel), and its
    small knobs at the foot. */
struct ColumnSpec
{
    int row;
    juce::StringArray names;
    std::vector<std::vector<KnobSpec>> knobs;
    std::vector<KnobSpec> smallKnobs;
    juce::String switchId;
};

/** What a machine's panel shows. */
struct PanelSpec
{
    Style style;
    std::vector<ColumnSpec> columns;
    std::vector<KnobSpec> master;
};

/** A rhythm machine's panel, laid out as the originals were: the logo, a screen with the pattern
    and the host's tempo, the presets and the master knobs along the top; the instruments side by
    side, their knobs in rows and their names under them (click a name to hear it); along the
    bottom start and stop, the sequencer's knobs, the instrument select and the sixteen step keys
    with their lights. */
class MachineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    MachineEditor (MachineProcessor&, PanelSpec);
    ~MachineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 1150, height = 560;

private:
    class Knob;
    class Switch;
    class NamePlate;
    class StepKeys;
    class RowSelect;
    class Screen;
    class StartKey;

    MachineProcessor& machine;
    const PanelSpec spec;
    MachineLook look;
    gkp::Theme theme;
    gkp::PresetBar presets;
    struct ColumnWidgets
    {
        std::vector<std::vector<Knob*>> knobs;
        std::vector<Knob*> small;
        NamePlate* plate = nullptr;
    };

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<ColumnWidgets> columnWidgets;
    std::vector<juce::Rectangle<int>> columnAreas;
    std::vector<std::unique_ptr<Knob>> masterKnobs, sequencerKnobs;
    std::unique_ptr<StepKeys> stepKeys;
    std::unique_ptr<RowSelect> rowSelect;
    std::unique_ptr<Screen> screen;
    std::unique_ptr<StartKey> startKey;
    juce::TextButton clear { "Clear" }, copy { "Copy" }, paste { "Paste" };
    std::array<uint32_t, maxRows> clipboard {};
    juce::String clipboardName;
    bool hasClipboard = false;
    juce::Rectangle<int> topStrip, instrumentArea, bottomStrip, sequencerArea, stepArea, masterArea, presetArea;
    juce::Image background;

    int currentPattern() const;
    Knob* addKnob (const KnobSpec&, bool level);
    void paintPanel (juce::Graphics&) const;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MachineEditor)
};

} // namespace rhythm
