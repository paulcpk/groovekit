#include "PluginEditor.h"

namespace kessel
{

namespace
{
    constexpr int width = 1220, height = 688;

    const juce::Colour heat (0xffff6a1f), ember (0xffe23b1c), brass (0xffd9a441);
    const juce::Colour clickColour (0xfff2d27a), bodyColour (0xffff8a3d), pitchColour (0xff68d8f0), driveColour (0xffff4d2e), rumbleColour (0xffa58bff), outputColour (0xffb9c4cf);
    const juce::Colour phosphor (0xffffb347), phosphorRumble (0xff9d84ff), phosphorPitch (0xff6fe3ff), glass (0xff07090a);

    /** Brushed metal: a gradient with faint streaks, the same every time it's drawn. */
    void brushed (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour top, juce::Colour bottom, float radius)
    {
        g.setGradientFill (juce::ColourGradient (top, 0, area.getY(), bottom, 0, area.getBottom(), false));
        g.fillRoundedRectangle (area, radius);

        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (area, radius);
        g.reduceClipRegion (clip);
        juce::Random random (7);

        for (auto y = area.getY(); y < area.getBottom(); y += 1.5f)
        {
            g.setColour ((random.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.012f + random.nextFloat() * 0.02f));
            g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
        }
    }

    /** A screw head with its slot. */
    void screw (juce::Graphics& g, juce::Point<float> centre, float angle)
    {
        const auto r = juce::Rectangle<float> (9, 9).withCentre (centre);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (r.translated (0, 1));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffcfd2d6), r.getX(), r.getY(), juce::Colour (0xff5d6168), r.getRight(), r.getBottom(), false));
        g.fillEllipse (r);
        g.setColour (juce::Colour (0xff2a2c30));
        g.drawLine ({ centre.getPointOnCircumference (3.4f, angle), centre.getPointOnCircumference (3.4f, angle + juce::MathConstants<float>::pi) }, 1.4f);
    }

    /** Engraved lettering: dark, with a light edge under it. */
    void engrave (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, float size, juce::Justification justification, float tracking = 0.15f)
    {
        auto f = gkp::font (size, true).withExtraKerningFactor (tracking);
        g.setFont (f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawText (text, area.translated (0, 1), justification);
        g.setColour (juce::Colour (0xff121315));
        g.drawText (text, area, justification);
    }
}

//==============================================================================
Display::Display()
{
    preview.prepare (previewRate, 2048);
}

void Display::update (const Settings& settings)
{
    shown = settings;
    shown.velocity = 0;
    const auto rumbleSeconds = settings.rumble > 0.001f ? std::min (2.5, static_cast<double> (settings.rumbleSize) * 0.6) : 0.0;
    seconds = juce::jlimit (0.25, 3.0, settings.length() * 1.15 + rumbleSeconds);
    const auto count = static_cast<int> (seconds * previewRate);
    samples.assign (static_cast<size_t> (count), 0);
    rumble.assign (static_cast<size_t> (count), 0);
    // The kick without its rumble, then the rumble alone as heard (after the volume), so the
    // screen shows where each is.
    auto dry = shown;
    dry.rumble = 0;
    preview.reset();
    preview.trigger (rootKey, 1.0f);
    preview.render (samples.data(), count, dry);

    if (shown.rumble > 0.001f)
    {
        std::vector<float> scratch (static_cast<size_t> (count));
        preview.reset();
        preview.trigger (rootKey, 1.0f);
        preview.render (scratch.data(), count, shown, rumble.data());

        for (auto& r : rumble)
            r *= juce::Decibels::decibelsToGain (shown.gain);
    }

    repaint();
}

void Display::setPlayhead (double secondsSinceHit)
{
    if (std::abs (secondsSinceHit - playhead) < 1e-6)
        return;

    const bool visible = playhead < seconds || secondsSinceHit < seconds;
    playhead = secondsSinceHit;

    if (visible)
        repaint();
}

void Display::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // The bezel and the glass.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5b6067), bounds.getX(), bounds.getY(), juce::Colour (0xff1d1f22), bounds.getRight(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 12);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12, 1);
    const auto screen = bounds.reduced (10);
    g.setColour (glass);
    g.fillRoundedRectangle (screen, 8);
    g.setGradientFill (juce::ColourGradient (phosphor.withAlpha (0.07f), screen.getCentreX(), screen.getCentreY(), juce::Colours::transparentBlack, screen.getX(), screen.getY(), true));
    g.fillRoundedRectangle (screen, 8);

    const auto plot = screen.reduced (16, 30);
    const auto xOf = [&] (double t) { return plot.getX() + plot.getWidth() * static_cast<float> (t / seconds); };

    // The grid: every 50 ms (100 ms for longer kicks), and the levels by quarters.
    const auto step = seconds > 1.2 ? 0.25 : seconds > 0.6 ? 0.1 : 0.05;
    g.setFont (gkp::font (10));

    for (double t = 0; t <= seconds + 1e-9; t += step)
    {
        const auto x = xOf (t);
        g.setColour (phosphor.withAlpha (0.08f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (phosphor.withAlpha (0.35f));
        g.drawText (juce::String (juce::roundToInt (t * 1000)) + " ms", juce::Rectangle<float> (x - 30, plot.getBottom() + 6, 60, 14), juce::Justification::centred);
    }

    for (int i = 1; i < 4; ++i)
    {
        const auto y = plot.getY() + plot.getHeight() * i / 4.0f;
        g.setColour (phosphor.withAlpha (i == 2 ? 0.16f : 0.07f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    // The waveform, the lowest and highest sample in each column, and the rumble over it.
    const auto columns = std::max (1, juce::roundToInt (plot.getWidth()));
    float peak = 0.05f;

    for (auto s : samples)
        peak = std::max (peak, std::abs (s));

    const auto drawWave = [&] (const std::vector<float>& wave, juce::Colour colour, float alpha)
    {
        if (wave.empty())
            return;

        juce::Path fill;
        std::vector<std::pair<float, float>> extremes (static_cast<size_t> (columns));
        const auto perColumn = static_cast<double> (wave.size()) / columns;

        for (int c = 0; c < columns; ++c)
        {
            const auto from = static_cast<size_t> (c * perColumn), to = std::min (wave.size(), static_cast<size_t> ((c + 1) * perColumn) + 1);
            float low = 0, high = 0;

            for (auto i = from; i < to; ++i)
                low = std::min (low, wave[i]), high = std::max (high, wave[i]);

            extremes[static_cast<size_t> (c)] = { low, high };
        }

        const auto yOf = [&] (float v) { return plot.getCentreY() - v / peak * plot.getHeight() * 0.47f; };
        fill.startNewSubPath (plot.getX(), yOf (extremes[0].second));

        for (int c = 1; c < columns; ++c)
            fill.lineTo (plot.getX() + static_cast<float> (c), yOf (extremes[static_cast<size_t> (c)].second));

        for (int c = columns - 1; c >= 0; --c)
            fill.lineTo (plot.getX() + static_cast<float> (c), yOf (extremes[static_cast<size_t> (c)].first));

        fill.closeSubPath();
        g.setColour (colour.withAlpha (alpha * 0.25f));
        g.strokePath (fill, juce::PathStrokeType (4.0f));
        g.setColour (colour.withAlpha (alpha * 0.55f));
        g.fillPath (fill);
        g.setColour (colour.withAlpha (alpha));
        g.strokePath (fill, juce::PathStrokeType (1.0f));
    };

    if (shown.rumble > 0.001f)
        drawWave (rumble, phosphorRumble, 0.9f);

    drawWave (samples, phosphor, 1.0f);

    // The pitch it dives down, on a scale from 20 Hz to 2 kHz.
    const auto tail = shown.tailNote (rootKey);
    const auto yOfHz = [&] (double hz) { return plot.getBottom() - plot.getHeight() * static_cast<float> (std::log (hz / 20.0) / std::log (100.0)); };
    juce::Path pitch;

    for (int c = 0; c <= columns; c += 2)
    {
        const auto t = seconds * c / columns;
        const auto hz = juce::jlimit (20.0, 2000.0, gkp::dsp::noteHz (tail + shown.pitchOffset (t)));
        const juce::Point<float> p (plot.getX() + static_cast<float> (c), yOfHz (hz));
        c == 0 ? pitch.startNewSubPath (p) : pitch.lineTo (p);
    }

    g.setColour (phosphorPitch.withAlpha (0.25f));
    g.strokePath (pitch, juce::PathStrokeType (4.0f));
    g.setColour (phosphorPitch);
    g.strokePath (pitch, juce::PathStrokeType (1.4f));

    // The line along a kick as it plays.
    if (playhead < seconds)
    {
        const auto x = xOf (playhead);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), x - 40, 0, juce::Colours::white.withAlpha (0.12f), x, 0, false));
        g.fillRect (juce::Rectangle<float> (x - 40, plot.getY(), 40, plot.getHeight()));
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }

    // The readouts.
    const auto topLine = juce::Rectangle<float> (screen.getX() + 16, screen.getY() + 7, screen.getWidth() - 32, 18);
    g.setFont (gkp::font (12, true));
    g.setColour (phosphor);
    g.drawText ("TAIL " + noteName (tail) + "   " + juce::String (gkp::dsp::noteHz (tail), 1) + " Hz", topLine, juce::Justification::centredLeft);
    g.setColour (phosphorPitch);
    g.drawText ("PITCH " + juce::String (juce::roundToInt (gkp::dsp::noteHz (tail + shown.pitchStart))) + " Hz", topLine, juce::Justification::centred);
    g.setColour (phosphor);
    g.drawText ("LENGTH " + juce::String (juce::roundToInt (shown.length() * 1000)) + " ms", topLine, juce::Justification::centredRight);

    if (shown.rumble > 0.001f)
    {
        g.setColour (phosphorRumble);
        g.setFont (gkp::font (11, true));
        g.drawText ("RUMBLE", topLine.translated (0, 16), juce::Justification::centredRight);
    }

    // Scan lines, a vignette and a reflection on the glass.
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (screen, 8);
        g.reduceClipRegion (clip);
        g.setColour (juce::Colours::black.withAlpha (0.18f));

        for (auto y = screen.getY(); y < screen.getBottom(); y += 3)
            g.drawHorizontalLine (juce::roundToInt (y), screen.getX(), screen.getRight());

        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, screen.getCentreX(), screen.getCentreY(), juce::Colours::black.withAlpha (0.55f), screen.getX(), screen.getY(), true));
        g.fillRect (screen);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), screen.getX(), screen.getY(), juce::Colours::transparentWhite, screen.getX() + screen.getWidth() * 0.45f, screen.getY() + screen.getHeight() * 0.6f, false));
        g.fillRect (screen);
    }
}

//==============================================================================
void Gauge::setValues (float peakGain, float reductionDb)
{
    const auto db = juce::jlimit (-40.0f, 6.0f, juce::Decibels::gainToDecibels (peakGain, -60.0f));
    const auto newNeedle = needle + (db - needle) * (db > needle ? 0.6f : 0.18f);
    const auto newReduction = reduction + (reductionDb - reduction) * 0.3f;

    if (std::abs (newNeedle - needle) > 0.02f || std::abs (newReduction - reduction) > 0.02f)
    {
        needle = newNeedle;
        reduction = newReduction;
        repaint();
    }
}

void Gauge::paint (juce::Graphics& g)
{
    const auto size = static_cast<float> (std::min (getWidth(), getHeight()));
    const auto bounds = juce::Rectangle<float> (size, size).withCentre (getLocalBounds().toFloat().getCentre());
    const auto centre = bounds.getCentre();

    // A chrome ring and a cream face.
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (bounds.translated (0, 3));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeef0f2), bounds.getX(), bounds.getY(), juce::Colour (0xff4a4e55), bounds.getRight(), bounds.getBottom(), false));
    g.fillEllipse (bounds);
    const auto face = bounds.reduced (size * 0.07f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff3ecd9), face.getCentreX(), face.getY(), juce::Colour (0xffd5cbb2), face.getCentreX(), face.getBottom(), false));
    g.fillEllipse (face);

    const auto radius = face.getWidth() / 2;
    const auto start = juce::MathConstants<float>::pi * -0.75f, end = juce::MathConstants<float>::pi * 0.75f;
    const auto angleOf = [&] (float db) { return start + (end - start) * (db + 40) / 46; };

    // The red zone over 0 dB, the ticks and their numbers.
    juce::Path red;
    red.addCentredArc (centre.x, centre.y, radius * 0.78f, radius * 0.78f, 0, angleOf (0), angleOf (6), true);
    g.setColour (juce::Colour (0xffc8321e));
    g.strokePath (red, juce::PathStrokeType (radius * 0.1f));

    g.setFont (gkp::font (radius * 0.14f, true));

    for (int db = -40; db <= 6; db += 2)
    {
        const bool major = db % 10 == 0 || db == 6 || db == -6;
        const auto a = angleOf (static_cast<float> (db));
        g.setColour (juce::Colour (0xff1c1c1c));
        g.drawLine ({ centre.getPointOnCircumference (radius * (major ? 0.7f : 0.76f), a), centre.getPointOnCircumference (radius * 0.86f, a) }, major ? 1.8f : 1.0f);

        if (major)
        {
            const auto p = centre.getPointOnCircumference (radius * 0.55f, a);
            g.drawText (db > 0 ? "+" + juce::String (db) : juce::String (db), juce::Rectangle<float> (40, 14).withCentre (p), juce::Justification::centred);
        }
    }

    // The compressor's arc, inside: how much it's holding back.
    if (reduction > 0.1f)
    {
        juce::Path arc;
        const auto a = start + (end - start) * std::min (1.0f, reduction / 20.0f);
        arc.addCentredArc (centre.x, centre.y, radius * 0.36f, radius * 0.36f, 0, start, a, true);
        g.setColour (heat);
        g.strokePath (arc, juce::PathStrokeType (radius * 0.07f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (juce::Colour (0xff3a3630));
    g.setFont (gkp::font (radius * 0.13f, true).withExtraKerningFactor (0.2f));
    g.drawText ("PRESSURE", juce::Rectangle<float> (radius * 1.4f, 14).withCentre (centre.translated (0, radius * 0.64f)), juce::Justification::centred);
    g.setFont (gkp::font (radius * 0.1f));
    g.drawText ("dB  /  COMP", juce::Rectangle<float> (radius * 1.4f, 14).withCentre (centre.translated (0, radius * 0.8f)), juce::Justification::centred);

    // The needle and its brass cap.
    const auto a = angleOf (needle);
    juce::Path hand;
    hand.addTriangle (centre.getPointOnCircumference (radius * 0.86f, a), centre.getPointOnCircumference (radius * 0.06f, a + juce::MathConstants<float>::halfPi),
                      centre.getPointOnCircumference (radius * 0.06f, a - juce::MathConstants<float>::halfPi));
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillPath (hand, juce::AffineTransform::translation (1.5f, 2.5f));
    g.setColour (juce::Colour (0xffd2281a));
    g.fillPath (hand);
    const auto cap = juce::Rectangle<float> (radius * 0.2f, radius * 0.2f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff6d88a), cap.getX(), cap.getY(), juce::Colour (0xff8a6420), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);

    // The glass over it.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), face.getX(), face.getY(), juce::Colours::transparentWhite, face.getCentreX(), face.getCentreY(), false));
    g.fillEllipse (face.reduced (2));
}

//==============================================================================
HitPad::HitPad (juce::MidiKeyboardState& state) : keyboard (state)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void HitPad::setGlow (float g)
{
    if (std::abs (g - glow) > 0.01f)
    {
        glow = g;
        repaint();
    }
}

void HitPad::mouseDown (const juce::MouseEvent&)
{
    down = true;
    keyboard.noteOn (1, rootKey, 1.0f);
    repaint();
}

void HitPad::mouseUp (const juce::MouseEvent&)
{
    down = false;
    keyboard.noteOff (1, rootKey, 0.0f);
    repaint();
}

void HitPad::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (4);
    const auto lit = std::max (glow, down ? 0.6f : 0.0f);

    // A metal well, a glowing ring and the rubber pad.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (bounds.translated (0, 2), 16);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff15161a), bounds.getX(), bounds.getY(), juce::Colour (0xff3a3d44), bounds.getRight(), bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 16);

    const auto ring = bounds.reduced (7);
    g.setColour (heat.withAlpha (0.15f + 0.85f * lit));
    g.drawRoundedRectangle (ring, 12, 2.5f);

    if (lit > 0.02f)
    {
        g.setColour (heat.withAlpha (0.25f * lit));
        g.drawRoundedRectangle (ring.expanded (2), 14, 6);
    }

    auto pad = ring.reduced (6).translated (0, down ? 1.5f : 0);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3c3e44).interpolatedWith (heat, 0.25f * lit), pad.getX(), pad.getY(),
                                             juce::Colour (0xff1e1f23).interpolatedWith (ember, 0.2f * lit), pad.getRight(), pad.getBottom(), false));
    g.fillRoundedRectangle (pad, 10);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (pad.reduced (0.5f), 10, 1);

    g.setFont (gkp::font (20, true).withExtraKerningFactor (0.25f));
    g.setColour (juce::Colour (0xffe8e0cc).interpolatedWith (juce::Colour (0xffffd9a8), lit));
    g.drawText ("HIT", pad, juce::Justification::centred);
}

//==============================================================================
Toggle::Toggle (juce::RangedAudioParameter& parameter, const juce::String& n)
    : name (n), attachment (parameter, [this] (float v) { on = v >= 0.5f; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Toggle::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void Toggle::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto label = bounds.removeFromBottom (26);
    const auto plate = juce::Rectangle<float> (26, std::min (44.0f, bounds.getHeight() - 4)).withCentre (bounds.getCentre());

    // A chrome bush with a bat lever, up for on; a lamp beside it.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (plate.translated (0, 1), 5);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4b4f56), plate.getX(), plate.getY(), juce::Colour (0xff1c1d21), plate.getRight(), plate.getBottom(), false));
    g.fillRoundedRectangle (plate, 5);
    const auto bush = juce::Rectangle<float> (14, 14).withCentre (plate.getCentre());
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6e8ea), bush.getX(), bush.getY(), juce::Colour (0xff6b6f76), bush.getRight(), bush.getBottom(), false));
    g.fillEllipse (bush);
    const auto tip = plate.getCentre().translated (0, on ? -plate.getHeight() * 0.36f : plate.getHeight() * 0.36f);
    g.setColour (juce::Colour (0xffd8dadd));
    g.drawLine ({ plate.getCentre(), tip }, 5.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (8, 8).withCentre (tip));

    const auto lamp = juce::Rectangle<float> (7, 7).withCentre ({ plate.getRight() + 9, plate.getY() + 6 });
    g.setColour (on ? bodyColour : juce::Colour (0xff2a1a12));
    g.fillEllipse (lamp);

    if (on)
    {
        g.setColour (bodyColour.withAlpha (0.3f));
        g.fillEllipse (lamp.expanded (3));
    }

    g.setFont (gkp::font (11));
    g.setColour (juce::Colour (0xffa9a69c));
    g.drawText (name, label.removeFromTop (13), juce::Justification::centred);
    g.setFont (gkp::font (10));
    g.setColour (juce::Colour (0xff75736b));
    g.drawText (on ? "On" : "Off", label.removeFromTop (12), juce::Justification::centred);
}

//==============================================================================
Dice::Dice() : juce::Button ("Roll")
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Dice::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat();
    const auto size = std::min (bounds.getHeight(), 34.0f);
    auto die = juce::Rectangle<float> (size, size).withCentre ({ bounds.getX() + size / 2 + 2, bounds.getCentreY() }).translated (0, down ? 1.0f : 0);

    // An ivory die showing five, tilted a little, and the word beside it.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::rotation (-0.18f, die.getCentreX(), die.getCentreY()));
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (die.translated (0, 2), 6);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffbf6ea), die.getX(), die.getY(), juce::Colour (0xffcfc6b0), die.getRight(), die.getBottom(), false));
        g.fillRoundedRectangle (die, 6);

        if (highlighted)
        {
            g.setColour (heat.withAlpha (0.6f));
            g.drawRoundedRectangle (die.expanded (1.5f), 7, 1.5f);
        }

        g.setColour (juce::Colour (0xff2b2622));
        const auto pip = size * 0.16f, near = size * 0.27f;

        for (auto [dx, dy] : { std::pair { -near, -near }, { near, -near }, { 0.0f, 0.0f }, { -near, near }, { near, near } })
            g.fillEllipse (juce::Rectangle<float> (pip, pip).withCentre (die.getCentre().translated (dx, dy)));
    }

    g.setFont (gkp::font (11, true).withExtraKerningFactor (0.2f));
    g.setColour (highlighted ? juce::Colour (0xffe8e4d8) : juce::Colour (0xffa9a69c));
    g.drawText ("ROLL", bounds.withTrimmedLeft (size + 10), juce::Justification::centredLeft);
}

//==============================================================================
Editor::Editor (Processor& p)
    : AudioProcessorEditor (p), owner (p), theme (gkp::Theme::hardwarePanel (heat)), look (theme), presets (p, theme), hit (p.keyboardState),
      follow (*p.state.getParameter (id::followKeys), "Follow keys")
{
    theme.bg = juce::Colour (0xff1f2124);
    theme.panel = juce::Colour (0xff2e3135);
    look.setColour (juce::PopupMenu::highlightedBackgroundColourId, heat);
    setLookAndFeel (&look);

    addAndMakeVisible (presets);
    addAndMakeVisible (display);
    addAndMakeVisible (gauge);
    addAndMakeVisible (hit);
    addAndMakeVisible (follow);
    addAndMakeVisible (dice);
    dice.setTooltip ("Roll a new kick: every knob but the volume, within what sounds like a kick");
    dice.onClick = [this] { roll(); };

    volume = knob (id::gain, "Volume", outputColour);

    plates.push_back ({ "Click", clickColour, choice (id::clickType, clickNames, clickColour), knob (id::clickLevel, "Level", clickColour),
                        { knob (id::clickTone, "Tone", clickColour), knob (id::clickDecay, "Decay", clickColour) }, 1, {} });
    plates.push_back ({ "Body", bodyColour, choice (id::bodyShape, { "Sine", "Tri", "Saw", "Sqr" }, bodyColour), knob (id::tune, "Tune", bodyColour),
                        { knob (id::hold, "Hold", bodyColour), knob (id::decay, "Decay", bodyColour), knob (id::sub, "Sub", bodyColour), &follow }, 2, {} });
    plates.push_back ({ "Pitch", pitchColour, nullptr, knob (id::pitchStart, "Start", pitchColour),
                        { knob (id::pitchTime, "Time", pitchColour), knob (id::pitchCurve, "Curve", pitchColour), knob (id::glide, "Glide", pitchColour, true) }, 2, {} });
    plates.push_back ({ "Drive", driveColour, choice (id::driveType, driveNames, driveColour), knob (id::drive, "Drive", driveColour),
                        { knob (id::crunch, "Crunch", driveColour), knob (id::crunchFreq, "Crunch freq", driveColour), knob (id::tone, "Tone", driveColour) }, 2, {} });
    plates.push_back ({ "Rumble", rumbleColour, nullptr, knob (id::rumble, "Rumble", rumbleColour),
                        { knob (id::rumbleSize, "Size", rumbleColour), knob (id::rumbleTone, "Tone", rumbleColour), knob (id::rumbleDrive, "Drive", rumbleColour),
                          knob (id::rumbleDuck, "Duck", rumbleColour) }, 2, {} });

    outputKnobs = { knob (id::compress, "Compress", outputColour), knob (id::clip, "Clip", outputColour), knob (id::lowCut, "Low cut", outputColour),
                    knob (id::velocity, "Velocity", outputColour) };

    setSize (width, height);
    display.update (owner.readSettings());
    lastHash = hashParameters();
    startTimerHz (30);
}

Editor::~Editor()
{
    setLookAndFeel (nullptr);
}

gkp::Knob* Editor::knob (const juce::String& paramId, const juce::String& name, juce::Colour colour, bool bipolar)
{
    auto k = std::make_unique<gkp::Knob> (owner.state, paramId, name, colour, theme, bipolar);
    auto* raw = k.get();
    addAndMakeVisible (*raw);
    owned.push_back (std::move (k));
    return raw;
}

gkp::Choice* Editor::choice (const juce::String& paramId, const juce::StringArray& labels, juce::Colour colour)
{
    auto c = std::make_unique<gkp::Choice> (*owner.state.getParameter (paramId), labels, colour, theme);
    auto* raw = c.get();
    addAndMakeVisible (*raw);
    owned.push_back (std::move (c));
    return raw;
}

double Editor::hashParameters() const
{
    double hash = 0;
    int i = 1;

    for (auto* parameter : owner.getParameters())
        hash += parameter->getValue() * (i++ * 7919.0 + 0.123);

    return hash;
}

void Editor::roll()
{
    // Each knob somewhere it sounds like a kick: wide where anything goes, narrow where it doesn't.
    auto& random = juce::Random::getSystemRandom();
    const auto between = [&] (float low, float high) { return low + random.nextFloat() * (high - low); };
    const auto rumbleOn = random.nextFloat() < 0.45f;
    const auto shape = random.nextFloat();
    const std::vector<std::pair<juce::String, float>> values {
        { id::clickType, static_cast<float> (random.nextInt (4)) }, { id::clickLevel, between (0.1f, 0.6f) }, { id::clickTone, between (800, 6000) },
        { id::clickDecay, between (3, 25) }, { id::bodyShape, shape < 0.6f ? 0.0f : shape < 0.8f ? 1.0f : shape < 0.9f ? 2.0f : 3.0f },
        { id::tune, between (27, 34) }, { id::hold, between (0, 240) }, { id::decay, between (180, 800) }, { id::sub, between (0.1f, 0.5f) },
        { id::pitchStart, between (20, 56) }, { id::pitchTime, between (14, 80) }, { id::pitchCurve, between (0.25f, 0.85f) }, { id::glide, between (-6, 2) },
        { id::driveType, static_cast<float> (random.nextInt (5)) }, { id::drive, between (4, 38) }, { id::crunch, between (0, 18) },
        { id::crunchFreq, between (400, 2500) }, { id::tone, between (3000, 14000) },
        { id::rumble, rumbleOn ? between (0.3f, 0.8f) : 0.0f }, { id::rumbleSize, between (0.8f, 3) }, { id::rumbleTone, between (80, 240) },
        { id::rumbleDrive, between (0.1f, 0.7f) }, { id::rumbleDuck, between (0.6f, 1) }, { id::compress, between (0.15f, 0.55f) }, { id::clip, between (0.1f, 0.55f) },
    };

    for (auto& [paramId, value] : values)
    {
        if (auto* parameter = owner.state.getParameter (paramId))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            parameter->endChangeGesture();
        }
    }

    // Heard straight away.
    owner.keyboardState.noteOn (1, rootKey, 1.0f);
    owner.keyboardState.noteOff (1, rootKey, 0.0f);
}

void Editor::timerCallback()
{
    if (const auto hash = hashParameters(); std::abs (hash - lastHash) > 1e-9)
    {
        lastHash = hash;
        display.update (owner.readSettings());
    }

    const auto since = owner.sinceHit.load();
    display.setPlayhead (since);
    hit.setGlow (static_cast<float> (std::exp (-since * 7)));
    gauge.setValues (owner.peak.load(), owner.reduction.load());
}

void Editor::resized()
{
    auto r = getLocalBounds().reduced (14, 0);
    header = r.removeFromTop (70);
    auto h = header.reduced (0, 14);
    h.removeFromLeft (300);
    volume->setBounds (h.removeFromRight (64).withSizeKeepingCentre (64, 70));
    h.removeFromRight (12);
    dice.setBounds (h.removeFromRight (86).withSizeKeepingCentre (86, 40));
    h.removeFromRight (12);
    presets.setBounds (h.removeFromRight (420).withSizeKeepingCentre (420, 30));

    r.removeFromTop (6);
    auto upper = r.removeFromTop (292);
    outputArea = upper.removeFromRight (390);
    upper.removeFromRight (12);
    screenArea = upper;
    display.setBounds (screenArea);

    auto o = outputArea.reduced (14, 10).withTrimmedTop (26);
    auto knobsRow = o.removeFromBottom (84);
    gaugeArea = o.removeFromLeft (o.getHeight() + 10);
    o.removeFromLeft (8);
    padArea = o.withSizeKeepingCentre (std::min (o.getWidth(), 150), std::min (o.getHeight(), 150));
    gauge.setBounds (gaugeArea);
    hit.setBounds (padArea);
    gkp::layoutRow (knobsRow, { outputKnobs[0], outputKnobs[1], outputKnobs[2], outputKnobs[3] });

    r.removeFromTop (12);
    auto lower = r.removeFromTop (294);

    // Plate widths: room for the big knob and each column of small ones.
    constexpr int big = 96, column = 66, padding = 28, gap = 10;
    int total = 0;

    for (auto& p : plates)
        total += padding + big + column * p.columns;

    const auto spare = (lower.getWidth() - total - gap * (static_cast<int> (plates.size()) - 1)) / static_cast<int> (plates.size());

    for (auto& p : plates)
    {
        p.area = lower.removeFromLeft (padding + big + column * p.columns + spare);
        lower.removeFromLeft (gap);

        auto inner = p.area.reduced (14, 10).withTrimmedTop (28);

        if (p.choice != nullptr)
        {
            p.choice->setBounds (inner.removeFromTop (24));
            inner.removeFromTop (8);
        }

        auto bigArea = inner.removeFromLeft (big + spare / 2);
        p.big->setBounds (bigArea.withSizeKeepingCentre (bigArea.getWidth(), std::min (bigArea.getHeight(), big + 30)));

        const auto rows = (static_cast<int> (p.small.size()) + p.columns - 1) / p.columns;
        const auto cellWidth = inner.getWidth() / p.columns, cellHeight = inner.getHeight() / std::max (1, rows);

        for (size_t i = 0; i < p.small.size(); ++i)
        {
            const auto col = static_cast<int> (i) % p.columns, row = static_cast<int> (i) / p.columns;
            p.small[i]->setBounds (juce::Rectangle<int> (inner.getX() + col * cellWidth, inner.getY() + row * cellHeight, cellWidth, cellHeight).reduced (2, 4));
        }
    }
}

void Editor::drawPlate (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, juce::Colour colour) const
{
    const auto a = area.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (a.translated (0, 2), 8);
    brushed (g, a, juce::Colour (0xff3c4045), juce::Colour (0xff2a2d31), 8);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (a.reduced (1), 8, 1);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (a.reduced (0.5f), 8, 1);

    // The title engraved, a lamp in the plate's colour, a screw in each corner.
    const auto lamp = juce::Rectangle<float> (8, 8).withCentre ({ a.getX() + 22, a.getY() + 20 });
    g.setColour (colour.withAlpha (0.25f));
    g.fillEllipse (lamp.expanded (3));
    g.setColour (colour);
    g.fillEllipse (lamp);
    engrave (g, title.toUpperCase(), area.withTrimmedLeft (34).withHeight (40), 12, juce::Justification::centredLeft, 0.25f);

    const auto inset = 9.0f;
    screw (g, { a.getRight() - inset, a.getY() + inset }, 0.6f);
    screw (g, { a.getX() + inset, a.getBottom() - inset }, 2.1f);
    screw (g, { a.getRight() - inset, a.getBottom() - inset }, 1.2f);
}

void Editor::drawLogo (juce::Graphics& g, juce::Rectangle<int> area) const
{
    // KESSEL in letters that glow like a furnace, set into the panel.
    juce::GlyphArrangement letters;
    const auto logoFont = gkp::font (40, true).withExtraKerningFactor (0.12f);
    letters.addLineOfText (logoFont, "KESSEL", static_cast<float> (area.getX()), static_cast<float> (area.getCentreY() + 8));
    juce::Path path;
    letters.createPath (path);
    const auto box = path.getBounds();

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillPath (path, juce::AffineTransform::translation (0, 2));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffd36b), 0, box.getY(), ember, 0, box.getBottom(), false));
    g.fillPath (path);
    g.setColour (juce::Colour (0xffffe9b8).withAlpha (0.5f));
    g.strokePath (path, juce::PathStrokeType (0.6f));

    g.setFont (gkp::font (10, true).withExtraKerningFactor (0.35f));
    g.setColour (juce::Colour (0xffa9a69c));
    g.drawText ("HARD KICK MACHINE", juce::Rectangle<float> (box.getRight() + 16, box.getY() + 4, 160, 14), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff75736b));
    g.drawText ("GROOVEKIT", juce::Rectangle<float> (box.getRight() + 16, box.getY() + 20, 160, 14), juce::Justification::centredLeft);
}

void Editor::paint (juce::Graphics& g)
{
    // The case: dark gunmetal, a header strip, a thin heat line under it.
    brushed (g, getLocalBounds().toFloat(), juce::Colour (0xff2b2e33), juce::Colour (0xff17191c), 0);
    const auto top = header.withX (0).withWidth (getWidth()).toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff15171a), 0, top.getY(), juce::Colour (0xff202327), 0, top.getBottom(), false));
    g.fillRect (top);
    g.setGradientFill (juce::ColourGradient (ember.withAlpha (0.0f), 0, 0, heat.withAlpha (0.8f), static_cast<float> (getWidth()) / 2, 0, false));
    g.fillRect (juce::Rectangle<float> (0, top.getBottom() - 2, static_cast<float> (getWidth()) / 2, 2));
    g.setGradientFill (juce::ColourGradient (heat.withAlpha (0.8f), static_cast<float> (getWidth()) / 2, 0, ember.withAlpha (0.0f), static_cast<float> (getWidth()), 0, false));
    g.fillRect (juce::Rectangle<float> (static_cast<float> (getWidth()) / 2, top.getBottom() - 2, static_cast<float> (getWidth()) / 2, 2));
    drawLogo (g, header.withTrimmedLeft (4));

    drawPlate (g, outputArea, "Output", outputColour);

    for (auto& p : plates)
        drawPlate (g, p.area, p.title, p.colour);

    // The case's own screws.
    for (auto x : { 8.0f, static_cast<float> (getWidth()) - 8 })
        for (auto y : { 8.0f, static_cast<float> (getHeight()) - 8 })
            screw (g, { x, y }, x * 0.01f + y * 0.02f);
}

} // namespace kessel
