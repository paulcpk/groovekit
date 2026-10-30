#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** What GrooveKit's plugins share: a look, presets and DSP. */
namespace gkp
{

/** A plugin's colours: GrooveKit's dark ones, or a hardware panel's. */
struct Theme
{
    juce::Colour bg, panel, raised, control, line, fg, dim, faint, screen, accent;
    bool hardware = false;      // knobs with caps on a grey panel, instead of flat rings

    static Theme dark (juce::Colour accent);
    static Theme hardwarePanel (juce::Colour accent);
};

juce::Font font (float size, bool bold = false);

/** Rotary sliders as rings (or capped knobs), flat combo boxes and menus. */
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel (const Theme&);

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int, int, int, int, juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override                { return font (13.0f); }
    juce::Font getPopupMenuFont() override                               { return font (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override       { return font (12.0f); }

private:
    const Theme theme;
};

/** A rounded panel with a coloured mark and a title. */
void drawPanel (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title, juce::Colour, const Theme&);

/** A dark screen inside a panel. */
void drawScreen (juce::Graphics&, juce::Rectangle<float> area, const Theme&);

/** An ADSR as a curve: times on a square-root scale so short ones still show. */
void drawEnvelope (juce::Graphics&, juce::Rectangle<float> area, float attack, float decay, float sustain, float release, juce::Colour, const Theme&);

/** The latest output as a wave, from a rising zero crossing so a steady note stands still. */
void drawScope (juce::Graphics&, juce::Rectangle<float> area, const float* samples, int count, juce::Colour, const Theme&);

/** A state-variable filter's response: type 0 off, 1 low-pass 24, 2 low-pass 12, 3 high-pass, 4 band-pass, 5 notch. */
void drawFilterResponse (juce::Graphics&, juce::Rectangle<float> area, int type, float cutoffHz, float resonance, juce::Colour, const Theme&);

} // namespace gkp
