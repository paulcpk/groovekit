#pragma once

#include "Look.h"

namespace rhythm
{

/** A machine's colours: its panel, its lettering, its keys and knobs, and the one colour it is
    known by (the logo, the lights). */
struct Style
{
    juce::String name, tagline;
    juce::Colour panelTop, panelBottom, strip, well, wellEdge;
    juce::Colour ink, inkDim, accent, led;
    juce::Colour keyTop, keyBottom, keyEdge, keyInk, keyMark;
    juce::Colour knobSkirt, knobCap, knobCapLevel, knobPointer;
    bool brushed = false;       // anodised and brushed sideways, instead of a fine grain
};

/** The lettering printed on a panel, spaced out a little. */
juce::Font printFont (float size, bool bold = true);

/** The machines' logo lettering: heavy and condensed. */
juce::Font logoFont (float size);

/** GrooveKit's colours for a machine's preset list and menus. */
gkp::Theme themeFor (const Style&);

/** Knobs with knurled skirts and caps lit from above, keys and screens as on the panel. */
class MachineLook : public gkp::LookAndFeel
{
public:
    explicit MachineLook (const Style&);

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int, int, int, int, juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override                { return printFont (12.5f, false); }

    const Style style;
};

/** The panel: its gradient and grain, with screws in the corners. */
void drawPanel (juce::Graphics&, juce::Rectangle<float> area, const Style&);

/** A screw head, its slot turned a little. */
void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angle);

/** A recessed area of the panel. */
void drawWell (juce::Graphics&, juce::Rectangle<float> area, float corner, const Style&);

/** A light in its bezel, lit from 0 (off) to 1, with a glow when lit. */
void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, float level);

/** The machine's name as its logo, with GrooveKit's mark. */
void drawLogo (juce::Graphics&, juce::Rectangle<float> area, const Style&);

} // namespace rhythm
