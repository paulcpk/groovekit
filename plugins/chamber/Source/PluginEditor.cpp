#include "PluginEditor.h"

namespace chamber
{

namespace
{
    const juce::Colour cyan (0xff4fd6e8);
}

Editor::Editor (Processor& p)
    : PadEffectEditor (p, "Chamber", "algorithmic reverb", cyan,
        { { id::decay, "Decay" }, { id::size, "Size" }, { id::predelay, "Pre-delay" }, { id::damping, "Damping" },
          { id::lowCut, "Low cut" }, { id::highCut, "High cut" }, { id::diffusion, "Diffusion" }, { id::early, "Early" },
          { id::modRate, "Mod rate" }, { id::modDepth, "Mod depth" }, { id::width, "Width" }, { id::duck, "Duck" }, { id::mix, "Mix" } },
        { { id::freeze, "Freeze" } }, {}, chamber::padParams, id::padX, id::padY),
      processor (p)
{
}

void Editor::drawPad (juce::Graphics& g, juce::Rectangle<float> area)
{
    // The tail over time: reflections as ticks at the start, then the fall to -60 dB, with the
    // modulation as a sway of the line. The window spans the decay and a little more.
    const auto decay = get (id::decay), size = get (id::size), early = get (id::early), predelay = get (id::predelay) * 0.001f;
    const auto window = std::max (0.5f, decay * 1.3f + predelay);
    const auto x = [&] (float t) { return area.getX() + area.getWidth() * t / window; };
    const auto y = [&] (float db) { return area.getY() + area.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 60.0f); };
    const bool frozen = get (id::freeze) >= 0.5f;

    for (float db : { -20.0f, -40.0f })
    {
        g.setColour (theme.line.withAlpha (0.4f));
        g.drawHorizontalLine (juce::roundToInt (y (db)), area.getX(), area.getRight());
    }

    const float earlyMs[6] { 7, 11, 17, 23, 31, 41 };

    for (int t = 0; t < 6; ++t)
    {
        const auto at = predelay + earlyMs[t] * (0.5f + size) * 0.001f;
        const auto db = juce::Decibels::gainToDecibels (early * (0.8f - 0.1f * t), -60.0f);
        g.setColour (accent.withAlpha (0.5f));
        g.drawLine (x (at), area.getBottom(), x (at), y (db), 1.2f);
    }

    juce::Path tail;
    const auto modDepth = get (id::modDepth);

    for (int i = 0; i <= 160; ++i)
    {
        const auto t = window * i / 160.0f;
        const auto since = t - predelay - 0.02f;
        float db = -60;

        if (since >= 0)
            db = frozen ? -3.0f : -60.0f * since / std::max (0.05f, decay) - 3 + 1.5f * modDepth * std::sin (since * 9);

        const auto p = juce::Point<float> (x (t), y (db));
        i == 0 ? tail.startNewSubPath (p) : tail.lineTo (p);
    }

    juce::Path fill (tail);
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.25f), 0, area.getY(), accent.withAlpha (0.0f), 0, area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (accent.withAlpha (0.9f));
    g.strokePath (tail, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (theme.faint);
    g.setFont (gkp::font (9));
    g.drawText (juce::String (window, 1) + " s", area.reduced (6, 4).toNearestInt().withTrimmedTop (area.getHeight() / 2).withTrimmedBottom (20), juce::Justification::bottomRight);
}

void Editor::drawStatus (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto level = juce::jlimit (0.0f, 1.0f, processor.inputLevel.load() * 3);
    g.setColour (theme.control);
    g.fillRoundedRectangle (area.toFloat().withHeight (4).withY (area.getCentreY() - 2), 2);
    g.setColour (accent.withAlpha (0.7f));
    g.fillRoundedRectangle (area.toFloat().withHeight (4).withY (area.getCentreY() - 2).withWidth (area.getWidth() * level), 2);
}

} // namespace chamber
