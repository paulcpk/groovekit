#pragma once

#include "MachineLook.h"
#include "PluginProcessor.h"
#include "Widgets.h"

namespace groove3
{

/** Groove-3's panel, laid out as the original's was: along the top the logo, a screen and the
    presets; under it the voice's six knobs with the waveform switch and the volume, and a display
    of the sound and its filter; a strip with the modifications' knobs and switches; along the
    bottom the step section.

    The step section shows a page of sixteen steps: each a note (its name, a light, accent and
    slide marked, a slide drawn into the next note), a tie (a bar from the note before) or a rest.
    A click picks a step; the keyboard under it writes the picked step's note and moves on; Down
    and Up shift it an octave; Accent and Slide mark it; Tie and Rest set it and move on. Dragging
    a note up or down changes its pitch, a double-click turns a step on or off, and the computer's
    keys work too (arrows, A, S, T, Delete).

    Inside GrooveKit the step section edits the notes of the GrooveKit pattern the channel plays
    (a slide is a note held into the next one, a tie a longer note), Start plays it, Length is its
    length; elsewhere it edits the plugin's own line, which plays while the host plays. */
class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 1150, height = 650;

private:
    class Knob;
    class Switch;
    class Display;
    class Screen;
    class StepCells;
    class Keys;
    class StartKey;

    Processor& processor;
    const rhythm::Style style;
    rhythm::MachineLook look;
    gkp::Theme theme;
    gkp::PresetBar presets;

    std::vector<std::unique_ptr<Knob>> voiceKnobs, modKnobs;
    std::unique_ptr<Knob> volumeKnob, lengthKnob, hostLength;
    std::vector<std::unique_ptr<Switch>> modSwitches;
    std::unique_ptr<Switch> waveformSwitch;
    std::unique_ptr<Display> display;
    std::unique_ptr<Screen> screen;
    std::unique_ptr<StepCells> cells;
    std::unique_ptr<Keys> keys;
    std::unique_ptr<StartKey> startKey;
    juce::TextButton octaveDown { "Down" }, octaveUp { "Up" }, accentKey { "Accent" }, slideKey { "Slide" }, tieKey { "Tie" }, restKey { "Rest" };
    juce::TextButton back { "Back" }, forward { "Next" }, previousPage { "<" }, nextPage { ">" }, clear { "Clear" }, lines { "Lines" };

    juce::Rectangle<int> topStrip, voiceArea, modArea, bottomStrip, sequencerArea, stepArea, editArea, presetArea, masterArea;
    juce::Image background;

    int selected = 0, page = 0, lastKey = 36;
    bool followPage = true;

    // Connected, an edit shows here until GrooveKit shows the pattern again.
    std::optional<Line> pending;
    uint32_t pendingSince = 0;

    bool hostMode() const;
    gkmachine::IHost* host() const;

    /** The line shown: GrooveKit's pattern's notes as steps, or the plugin's own line. */
    Line line() const;
    int length() const;
    int numPages() const;
    int playStep() const;
    juce::String lineName() const;

    /** Changes the line at a step (in GrooveKit, the notes starting from the note sounding before
        it up to it), then picks the next step if asked. */
    void edit (int step, const std::function<void (Line&)>& change, bool advance);
    void commit (const Line& before, const Line& after, int step);
    void select (int step);
    void writeKey (int index);
    void loadLine (int index);
    void clearPage();
    void sendLength();

    /** The key an index of the panel's keyboard (0 to 12, C to C) writes: in the picked note's
        octave, or the last one written. */
    int keyFor (int index) const;

    bool keyPressed (const juce::KeyPress&) override;
    void timerCallback() override;
    void paintPanel (juce::Graphics&) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace groove3
