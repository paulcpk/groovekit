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
    with their lights.

    Inside GrooveKit the step section edits the GrooveKit pattern the machine's channel plays, a
    page of sixteen steps at a time: the screen shows its name and the page, Start plays it, Last
    Step is its length and Shuffle the song's swing, Clear, Copy and Paste work on the selected
    instrument's steps on the page, and a click on the screen loads one of the machine's grooves. */
class MachineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    MachineEditor (MachineProcessor&, PanelSpec);
    ~MachineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 1150, height = 560;

    /** The longest pattern the panel edits in GrooveKit, in steps (sixteen pages). */
    static constexpr int maxHostSteps = 256;

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

    // Connected to GrooveKit: the page shown, its knobs for the pattern's length and the song's
    // swing (in place of Last Step and Shuffle), and a row's steps copied from a page.
    struct CopiedStep
    {
        bool on = false, flam = false;
        int velocity = 0;
    };

    std::unique_ptr<Knob> hostLastStep, hostShuffle;
    int page = 0;
    bool shownHostMode = false;
    std::array<CopiedStep, numSteps> hostClipboard {};
    int hostClipboardRow = -1;

    bool hostMode() const;
    gkmachine::IHost* host() const;
    float flamMs() const;
    int numPages() const;
    int lastPage() const;

    /** Whether a row plays on a step of GrooveKit's pattern (counted from its start). */
    bool hostStep (int row, int step) const;
    bool hostRowUsed (int row) const;
    std::vector<int> hostKeys (int row, int fromStep, int numStepsToSearch) const;
    void setHostStep (int row, int step, bool on);
    void loadGroove (int index);
    void clearHostRow();
    void copyHostRow();
    void pasteHostRow();
    void sendLength();
    void updateMode();

    int currentPattern() const;
    Knob* addKnob (const KnobSpec&, bool level);
    void paintPanel (juce::Graphics&) const;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MachineEditor)
};

} // namespace rhythm
