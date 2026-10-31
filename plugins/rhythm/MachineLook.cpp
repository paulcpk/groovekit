#include "MachineLook.h"

namespace rhythm
{

juce::Font printFont (float size, bool bold)
{
    juce::Font f (juce::FontOptions ("Avenir Next", size, bold ? juce::Font::bold : juce::Font::plain));
    f.setExtraKerningFactor (bold ? 0.08f : 0.03f);
    return f;
}

juce::Font logoFont (float size)
{
    juce::Font f (juce::FontOptions ("Avenir Next Condensed", size, juce::Font::bold | juce::Font::italic));
    f.setExtraKerningFactor (-0.01f);
    return f;
}

gkp::Theme themeFor (const Style& s)
{
    auto t = gkp::Theme::hardwarePanel (s.accent);
    t.bg = s.strip;
    t.panel = s.panelBottom;
    t.raised = s.strip.brighter (0.12f);
    t.control = s.well;
    t.line = s.wellEdge;
    t.fg = s.ink;
    t.dim = s.inkDim;
    t.faint = s.inkDim.withMultipliedAlpha (0.6f);
    return t;
}

//==============================================================================
MachineLook::MachineLook (const Style& s) : gkp::LookAndFeel (themeFor (s)), style (s)
{
    setColour (juce::ComboBox::textColourId, style.ink);
    setColour (juce::PopupMenu::backgroundColourId, style.strip.brighter (0.08f));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, style.accent);
    setColour (juce::TextButton::textColourOffId, style.ink);
}

void MachineLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat();
    const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2 - 1;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + position * (endAngle - startAngle);
    const bool level = slider.getProperties()["level"];
    const bool small = radius < 13;

    // The scale printed around it.
    g.setColour (style.inkDim.withAlpha (0.75f));
    const auto ticks = small ? 6 : 11;

    for (int i = 0; i < ticks; ++i)
    {
        const auto a = startAngle + (endAngle - startAngle) * static_cast<float> (i) / static_cast<float> (ticks - 1);
        const auto major = i == 0 || i == ticks - 1 || (! small && i == 5);
        g.drawLine ({ centre.getPointOnCircumference (radius - (major ? 3.5f : 2.0f), a), centre.getPointOnCircumference (radius, a) }, major ? 1.3f : 0.9f);
    }

    const auto body = radius * 0.78f;

    // Its shadow on the panel, falling down and to the right.
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.13f));
        g.fillEllipse (juce::Rectangle<float> (body * 2 + static_cast<float> (i) * 1.6f, body * 2 + static_cast<float> (i) * 1.6f).withCentre (centre.translated (1.2f, 2.2f)));
    }

    // The knurled skirt.
    const auto skirt = juce::Rectangle<float> (body * 2, body * 2).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (style.knobSkirt.brighter (0.35f), skirt.getX(), skirt.getY(), style.knobSkirt.darker (0.6f), skirt.getRight(), skirt.getBottom(), false));
    g.fillEllipse (skirt);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    const auto ridges = small ? 18 : 30;

    for (int i = 0; i < ridges; ++i)
    {
        const auto a = angle + juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (ridges);
        g.drawLine ({ centre.getPointOnCircumference (body * 0.86f, a), centre.getPointOnCircumference (body - 0.4f, a) }, 0.8f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (skirt, 0.8f);

    // The cap, lit from the top left, with a soft sheen.
    const auto capRadius = body * 0.72f;
    const auto cap = juce::Rectangle<float> (capRadius * 2, capRadius * 2).withCentre (centre);
    const auto capColour = level ? style.knobCapLevel : style.knobCap;
    g.setGradientFill (juce::ColourGradient (capColour.brighter (0.28f), cap.getX(), cap.getY(), capColour.darker (0.35f), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), cap.getCentreX(), cap.getY(), juce::Colours::white.withAlpha (0.0f), cap.getCentreX(), cap.getCentreY(), false));
    g.fillEllipse (cap.reduced (capRadius * 0.12f).withTrimmedBottom (capRadius * 0.5f));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawEllipse (cap, 0.7f);

    // The pointer, from the cap's middle to the skirt's edge.
    const auto pointer = level ? style.knobPointer : style.knobPointer;
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawLine ({ centre.getPointOnCircumference (capRadius * 0.25f, angle).translated (0.6f, 0.8f), centre.getPointOnCircumference (body - 1.5f, angle).translated (0.6f, 0.8f) }, small ? 1.8f : 2.4f);
    g.setColour (pointer);
    g.drawLine ({ centre.getPointOnCircumference (capRadius * 0.25f, angle), centre.getPointOnCircumference (body - 1.5f, angle) }, small ? 1.6f : 2.2f);
}

void MachineLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0, 0, static_cast<float> (w), static_cast<float> (h));
    drawWell (g, r, 4, style);
    juce::Path arrow;
    arrow.addTriangle (static_cast<float> (w - 16), h / 2.0f - 2, static_cast<float> (w - 8), h / 2.0f - 2, w - 12.0f, h / 2.0f + 3);
    g.setColour (style.inkDim);
    g.fillPath (arrow);
}

void MachineLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void MachineLook::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
{
    // A small rubber key with a printed legend.
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0, 1.2f), 3.5f);
    auto top = on ? style.accent : style.strip.brighter (0.22f);

    if (highlighted)
        top = top.brighter (0.08f);

    if (down)
        r = r.translated (0, 0.8f);

    g.setGradientFill (juce::ColourGradient (top.brighter (0.12f), 0, r.getY(), top.darker (0.25f), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.5f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (juce::roundToInt (r.getY() + 1), r.getX() + 3, r.getRight() - 3);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 3.5f, 0.7f);
}

void MachineLook::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    g.setFont (printFont (10.5f));
    g.setColour (button.getToggleState() ? style.strip : style.ink.withAlpha (button.isEnabled() ? 0.9f : 0.4f));
    g.drawText (button.getButtonText().toUpperCase(), button.getLocalBounds().translated (0, down ? 1 : 0), juce::Justification::centred);
}

//==============================================================================
void drawPanel (juce::Graphics& g, juce::Rectangle<float> area, const Style& s)
{
    g.setGradientFill (juce::ColourGradient (s.panelTop, 0, area.getY(), s.panelBottom, 0, area.getBottom(), false));
    g.fillRect (area);

    // The finish: brushed sideways, or a fine grain. Drawn from a fixed seed, so it never shimmers.
    juce::Random random (s.brushed ? 9 : 8);

    if (s.brushed)
    {
        for (float y = area.getY(); y < area.getBottom(); y += 1.0f)
        {
            const auto shade = random.nextFloat();
            g.setColour ((shade > 0.5f ? juce::Colours::white : juce::Colours::black).withAlpha (0.012f + 0.02f * std::abs (shade - 0.5f)));
            g.drawHorizontalLine (static_cast<int> (y), area.getX(), area.getRight());
        }
    }
    else
    {
        for (int i = 0; i < 9000; ++i)
        {
            const auto p = juce::Point<float> (area.getX() + random.nextFloat() * area.getWidth(), area.getY() + random.nextFloat() * area.getHeight());
            g.setColour ((random.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.035f));
            g.fillRect (p.x, p.y, 1.0f, 1.0f);
        }
    }

    // A soft light falling from above.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), area.getCentreX(), area.getY(), juce::Colours::transparentWhite, area.getCentreX(), area.getY() + area.getHeight() * 0.6f, true));
    g.fillRect (area);
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float angle)
{
    const auto r = juce::Rectangle<float> (radius * 2, radius * 2).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (r.expanded (1.2f).translated (0.4f, 0.8f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb9bcc2), r.getX(), r.getY(), juce::Colour (0xff4b4e55), r.getRight(), r.getBottom(), false));
    g.fillEllipse (r);
    g.setColour (juce::Colour (0xff1f2024));
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.75f, angle), centre.getPointOnCircumference (radius * 0.75f, angle + juce::MathConstants<float>::pi) }, 1.6f);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.75f, angle).translated (0.5f, 0.6f), centre.getPointOnCircumference (radius * 0.75f, angle + juce::MathConstants<float>::pi).translated (0.5f, 0.6f) }, 0.6f);
}

void drawWell (juce::Graphics& g, juce::Rectangle<float> area, float corner, const Style& s)
{
    g.setColour (s.well);
    g.fillRoundedRectangle (area, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), 0, area.getY(), juce::Colours::transparentBlack, 0, area.getY() + 6, false));
    g.fillRoundedRectangle (area, corner);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine (juce::roundToInt (area.getBottom()), area.getX() + corner, area.getRight() - corner);
    g.setColour (s.wellEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 0.8f);
}

void drawLed (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float level)
{
    const auto r = juce::Rectangle<float> (radius * 2, radius * 2).withCentre (centre);
    level = juce::jlimit (0.0f, 1.0f, level);

    // The glow on the panel around it.
    if (level > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f * level), centre.x, centre.y, colour.withAlpha (0.0f), centre.x + radius * 4.2f, centre.y, true));
        g.fillEllipse (r.expanded (radius * 3.2f));
    }

    // The bezel, the lens and its highlight.
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillEllipse (r.expanded (1.4f));
    const auto off = colour.withSaturation (colour.getSaturation() * 0.5f).darker (2.2f);
    const auto lens = off.interpolatedWith (colour, level);
    g.setGradientFill (juce::ColourGradient (lens.brighter (0.4f + 0.6f * level), centre.x - radius * 0.3f, centre.y - radius * 0.4f, lens.darker (0.4f), centre.x + radius, centre.y + radius, true));
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (0.25f + 0.35f * level));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.8f, radius * 0.55f).withCentre (centre.translated (-radius * 0.25f, -radius * 0.38f)));
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> area, const Style& s)
{
    // The name, heavy and leaning, its number in the machine's colour; GrooveKit's mark beneath.
    const auto dash = s.name.indexOfChar ('-');
    const auto word = dash > 0 ? s.name.substring (0, dash) : s.name;
    const auto number = dash > 0 ? s.name.substring (dash + 1) : juce::String();
    auto font = logoFont (area.getHeight() * 0.66f);
    const auto wordWidth = juce::GlyphArrangement::getStringWidth (font, word);
    const auto top = area.removeFromTop (area.getHeight() * 0.68f);

    g.setFont (font);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawText (word, top.translated (1, 1.5f), juce::Justification::bottomLeft);
    g.setColour (s.ink);
    g.drawText (word, top, juce::Justification::bottomLeft);

    // The number in a slanted badge.
    if (number.isNotEmpty())
    {
        const auto h = top.getHeight() * 0.74f;
        auto badge = juce::Rectangle<float> (top.getX() + wordWidth + 6, top.getBottom() - h - 1, h * 0.95f, h);
        juce::Path p;
        const auto slant = h * 0.22f;
        p.startNewSubPath (badge.getX() + slant, badge.getY());
        p.lineTo (badge.getRight() + slant, badge.getY());
        p.lineTo (badge.getRight(), badge.getBottom());
        p.lineTo (badge.getX(), badge.getBottom());
        p.closeSubPath();
        g.setColour (s.accent);
        g.fillPath (p);
        g.setColour (s.strip);
        g.setFont (logoFont (h * 0.95f));
        g.drawText (number, badge.translated (slant * 0.5f, 1), juce::Justification::centred);
    }

    g.setColour (s.inkDim);
    g.setFont (printFont (9.5f));
    g.drawText (s.tagline.toUpperCase(), area.withTrimmedTop (3), juce::Justification::topLeft);
}

} // namespace rhythm
