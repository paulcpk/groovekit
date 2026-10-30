#include "PluginEditor.h"

namespace echo
{

namespace
{
    const juce::Colour coral (0xffff7a59);
}

Editor::Editor (Processor& p)
    : PadEffectEditor (p, "Echo", "stereo delay", coral,
        { { id::timeMs, "Time" }, { id::ratio, "Ratio" }, { id::feedback, "Feedback" }, { id::crossfeed, "Crossfeed" },
          { id::lowCut, "Low cut" }, { id::highCut, "High cut" }, { id::saturation, "Saturation" }, { id::wow, "Wow" },
          { id::wowRate, "Wow rate" }, { id::diffusion, "Diffusion" }, { id::duck, "Duck" }, { id::width, "Width" }, { id::mix, "Mix" } },
        { { id::sync, "Sync" }, { id::freeze, "Freeze" } },
        { { id::mode, modeNames }, { id::division, gkp::dsp::divisionNames() } },
        echo::padParams, id::padX, id::padY),
      processor (p)
{
}

void Editor::drawPad (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Two seconds of repeats: the left side's up from the middle, the right side's down.
    const auto time = std::max (0.001f, processor.timeSeconds.load());
    const auto feedback = get (id::freeze) >= 0.5f ? 1.0f : get (id::feedback);
    const auto mode = juce::roundToInt (get (id::mode));
    const auto ratio = get (id::ratio);
    const auto window = std::max (2.0f, time * 4);
    const auto mid = area.getCentreY();
    const auto x = [&] (float t) { return area.getX() + area.getWidth() * t / window; };

    g.setColour (theme.line.withAlpha (0.5f));
    g.drawHorizontalLine (juce::roundToInt (mid), area.getX(), area.getRight());

    auto bar = [&] (float t, float gain, bool up, float alpha)
    {
        if (t > window || gain < 0.01f)
            return;

        const auto h = (area.getHeight() / 2 - 6) * gain;
        g.setColour (accent.withAlpha (alpha));
        g.fillRoundedRectangle (x (t) - 3, up ? mid - h : mid, 6, h, 2);
    };

    bar (0, 1, true, 0.9f);
    bar (0, 1, false, 0.9f);

    for (int k = 1; k < 40; ++k)
    {
        const auto gain = std::pow (feedback, static_cast<float> (k)) * 0.9f;

        if (mode == 1)
        {
            bar (time * k, gain, k % 2 == 1, 0.8f);
        }
        else if (mode == 2)
        {
            bar (time * k, gain, true, 0.8f);
            bar (time * ratio * k, gain, false, 0.8f);
        }
        else
        {
            bar (time * k, gain, true, 0.8f);
            bar (time * k, gain, false, 0.8f);
        }
    }

    g.setColour (theme.faint);
    g.setFont (gkp::font (9));
    g.drawText (juce::String (juce::roundToInt (time * 1000)) + " ms", area.reduced (6, 4).toNearestInt().withTrimmedTop (area.getHeight() / 2).withTrimmedBottom (20), juce::Justification::bottomRight);
}

void Editor::drawStatus (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto level = juce::jlimit (0.0f, 1.0f, processor.inputLevel.load() * 3);
    g.setColour (theme.control);
    g.fillRoundedRectangle (area.toFloat().withHeight (4).withY (area.getCentreY() - 2), 2);
    g.setColour (accent.withAlpha (0.7f));
    g.fillRoundedRectangle (area.toFloat().withHeight (4).withY (area.getCentreY() - 2).withWidth (area.getWidth() * level), 2);
}

} // namespace echo
