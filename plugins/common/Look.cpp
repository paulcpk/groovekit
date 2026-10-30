#include "Look.h"

namespace gkp
{

Theme Theme::dark (juce::Colour accent)
{
    Theme t;
    t.bg = juce::Colour (0xff111113);
    t.panel = juce::Colour (0xff18181b);
    t.raised = juce::Colour (0xff202024);
    t.control = juce::Colour (0xff29292e);
    t.line = juce::Colour (0xff2c2c31);
    t.fg = juce::Colour (0xffd6d6db);
    t.dim = juce::Colour (0xff8d8d96);
    t.faint = juce::Colour (0xff5b5b64);
    t.screen = juce::Colour (0xff0c0c0e);
    t.accent = accent;
    return t;
}

Theme Theme::hardwarePanel (juce::Colour accent)
{
    Theme t;
    t.bg = juce::Colour (0xff2a2b2e);
    t.panel = juce::Colour (0xff34363a);
    t.raised = juce::Colour (0xff3e4045);
    t.control = juce::Colour (0xff1e1f22);
    t.line = juce::Colour (0xff4a4c52);
    t.fg = juce::Colour (0xffe8e4d8);
    t.dim = juce::Colour (0xffa9a69c);
    t.faint = juce::Colour (0xff75736b);
    t.screen = juce::Colour (0xff101410);
    t.accent = accent;
    t.hardware = true;
    return t;
}

juce::Font font (float size, bool bold)
{
    return juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain);
}

//==============================================================================
LookAndFeel::LookAndFeel (const Theme& t) : theme (t)
{
    setColour (juce::ComboBox::backgroundColourId, theme.control);
    setColour (juce::ComboBox::textColourId, theme.fg);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId, theme.dim);
    setColour (juce::PopupMenu::backgroundColourId, theme.raised);
    setColour (juce::PopupMenu::textColourId, theme.fg);
    setColour (juce::PopupMenu::headerTextColourId, theme.faint);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId, theme.control);
    setColour (juce::TextButton::buttonOnColourId, theme.accent);
    setColour (juce::TextButton::textColourOffId, theme.dim);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::TooltipWindow::backgroundColourId, theme.raised);
    setColour (juce::TooltipWindow::textColourId, theme.fg);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3);
    const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + position * (endAngle - startAngle);
    const bool bipolar = slider.getProperties()["bipolar"];
    const auto origin = bipolar ? (startAngle + endAngle) / 2 : startAngle;
    const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);

    if (theme.hardware)
    {
        // A capped knob: a dark skirt with tick marks, a lighter cap, a line for the position.
        g.setColour (juce::Colour (0xff17181a));

        for (int i = 0; i <= 10; ++i)
        {
            const auto a = startAngle + (endAngle - startAngle) * i / 10.0f;
            g.drawLine ({ centre.getPointOnCircumference (radius - 1, a), centre.getPointOnCircumference (radius + 1.5f, a) }, 1.0f);
        }

        if (std::abs (angle - origin) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius + 0.5f, radius + 0.5f, 0, std::min (origin, angle), std::max (origin, angle), true);
            g.setColour (colour);
            g.strokePath (arc, { 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        }

        const auto skirt = juce::Rectangle<float> (radius * 1.7f, radius * 1.7f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3d42), skirt.getX(), skirt.getY(), juce::Colour (0xff141517), skirt.getRight(), skirt.getBottom(), true));
        g.fillEllipse (skirt);
        const auto cap = juce::Rectangle<float> (radius * 1.25f, radius * 1.25f).withCentre (centre.translated (0, -1));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2eee2), cap.getX(), cap.getY(), juce::Colour (0xffc9c3b2), cap.getRight(), cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setColour (juce::Colour (0xff1b1c1e));
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.15f, angle), centre.getPointOnCircumference (radius * 0.58f, angle) }, 2.4f);
        return;
    }

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff3a3a41));
    g.strokePath (track, { 3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    if (std::abs (angle - origin) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0, std::min (origin, angle), std::max (origin, angle), true);
        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (arc, { 7, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
        g.setColour (colour);
        g.strokePath (arc, { 3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
    }

    g.setColour (theme.raised.brighter (0.05f));
    g.fillEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (centre));
    g.setColour (theme.fg);
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.2f, angle), centre.getPointOnCircumference (radius - 5, angle) }, 1.8f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (0, 0, static_cast<float> (w), static_cast<float> (h), 4);
    juce::Path arrow;
    arrow.addTriangle (static_cast<float> (w - 18), h / 2.0f - 2, static_cast<float> (w - 10), h / 2.0f - 2, w - 14.0f, h / 2.0f + 3);
    g.setColour (theme.dim);
    g.fillPath (arrow);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour, bool highlighted, bool down)
{
    auto c = button.getToggleState() ? theme.accent : colour;

    if (highlighted || down)
        c = c.brighter (down ? 0.2f : 0.1f);

    g.setColour (c);
    g.fillRoundedRectangle (button.getLocalBounds().toFloat(), 4);
}

//==============================================================================
void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, juce::Colour colour, const Theme& theme)
{
    if (theme.hardware)
    {
        g.setGradientFill (juce::ColourGradient (theme.panel.brighter (0.06f), 0, static_cast<float> (area.getY()), theme.panel.darker (0.08f), 0, static_cast<float> (area.getBottom()), false));
        g.fillRoundedRectangle (area.toFloat(), 6);
        g.setColour (theme.line);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 6, 1);

        // A label strip, as the printing on a panel.
        g.setColour (colour);
        g.fillRect (area.getX() + 10, area.getY() + 24, 36, 2);
        g.setColour (theme.fg);
        g.setFont (font (11, true));
        g.drawText (title.toUpperCase(), area.withTrimmedLeft (10).withHeight (26), juce::Justification::centredLeft);
        return;
    }

    g.setColour (theme.panel);
    g.fillRoundedRectangle (area.toFloat(), 8);
    g.setColour (colour);
    g.fillRoundedRectangle (area.toFloat().withSize (3, 12).translated (10, 11), 1.5f);
    g.setColour (theme.fg);
    g.setFont (font (12, true));
    g.drawText (title, area.withTrimmedLeft (20).withHeight (34), juce::Justification::centredLeft);
}

void drawScreen (juce::Graphics& g, juce::Rectangle<float> area, const Theme& theme)
{
    g.setColour (theme.screen);
    g.fillRoundedRectangle (area, 5);

    if (theme.hardware)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (area.reduced (0.5f), 5, 1);
    }
}

void drawEnvelope (juce::Graphics& g, juce::Rectangle<float> area, float attack, float decay, float sustain, float release, juce::Colour colour, const Theme& theme)
{
    drawScreen (g, area, theme);
    const auto a = std::sqrt (attack), d = std::sqrt (decay), s = sustain, r = std::sqrt (release);
    const auto hold = 0.6f;
    const auto total = std::max (0.01f, a + d + hold + r);
    const auto inner = area.reduced (6, 8);
    const auto x = [&] (float t) { return inner.getX() + inner.getWidth() * t / total; };
    const auto y = [&] (float v) { return inner.getBottom() - inner.getHeight() * v; };

    juce::Path path;
    path.startNewSubPath (x (0), y (0));
    path.lineTo (x (a), y (1));
    path.quadraticTo (x (a + d * 0.2f), y (s), x (a + d), y (s));
    path.lineTo (x (a + d + hold), y (s));
    path.quadraticTo (x (a + d + hold + r * 0.2f), y (0), x (total), y (0));

    juce::Path fill (path);
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.3f), 0, inner.getY(), colour.withAlpha (0.02f), 0, inner.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (auto [t, v] : { std::pair { a, 1.0f }, { a + d, s }, { a + d + hold, s } })
        g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ x (t), y (v) }));
}

void drawScope (juce::Graphics& g, juce::Rectangle<float> area, const float* samples, int count, juce::Colour colour, const Theme& theme)
{
    drawScreen (g, area, theme);

    if (count < 4)
        return;

    int start = 0;
    for (int i = 1; i < count / 2; ++i)
        if (samples[i - 1] <= 0 && samples[i] > 0) { start = i; break; }

    const auto n = count / 2;
    float peak = 0.05f;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::abs (samples[start + i]));

    juce::Path path;
    const auto inner = area.reduced (6, 5);

    for (int i = 0; i < n; ++i)
    {
        const auto p = juce::Point<float> (inner.getX() + inner.getWidth() * static_cast<float> (i) / static_cast<float> (n - 1),
                                           inner.getCentreY() - samples[start + i] / peak * inner.getHeight() / 2);
        i == 0 ? path.startNewSubPath (p) : path.lineTo (p);
    }

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.5f));
}

void drawFilterResponse (juce::Graphics& g, juce::Rectangle<float> area, int type, float cutoff, float resonance, juce::Colour colour, const Theme& theme)
{
    drawScreen (g, area, theme);
    const auto k = 2.0f - 1.96f * resonance;
    const auto xOf = [&] (float hz) { return area.getX() + area.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    const auto yOf = [&] (float db) { return area.getY() + area.getHeight() * (18 - juce::jlimit (-36.0f, 18.0f, db)) / 54.0f; };

    g.setColour (theme.line.withAlpha (0.6f));
    for (auto hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), area.getY() + 2, area.getBottom() - 2);
    g.drawHorizontalLine (juce::roundToInt (yOf (0)), area.getX() + 2, area.getRight() - 2);

    juce::Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const auto hz = 20.0f * std::pow (1000.0f, i / 120.0f);
        const auto w = hz / std::max (20.0f, cutoff);
        const auto denominator = std::sqrt ((1 - w * w) * (1 - w * w) + (k * w) * (k * w));
        float gain = 1;

        switch (type)
        {
            case 1: gain = 1 / (denominator * denominator); break;
            case 2: gain = 1 / denominator; break;
            case 3: gain = w * w / denominator; break;
            case 4: gain = k * w / denominator; break;
            case 5: gain = std::abs (1 - w * w) / denominator; break;
            default: break;
        }

        const auto p = juce::Point<float> (xOf (hz), yOf (juce::Decibels::gainToDecibels (gain, -60.0f)));
        i == 0 ? curve.startNewSubPath (p) : curve.lineTo (p);
    }

    juce::Path fill (curve);
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.3f), 0, area.getY(), colour.withAlpha (0.02f), 0, area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (type == 0 ? theme.faint : colour);
    g.strokePath (curve, juce::PathStrokeType (1.8f));
}

} // namespace gkp
