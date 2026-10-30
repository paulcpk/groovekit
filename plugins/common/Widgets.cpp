#include "Widgets.h"

namespace gkp
{

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& n, juce::Colour colour, const Theme& t, bool bipolar)
    : attachment (state, paramId, slider), name (n), theme (t)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (180);
    slider.setVelocityModeParameters (0.6, 1, 0.05, true, juce::ModifierKeys::shiftModifier);
    slider.getProperties().set ("bipolar", bipolar);
    auto* param = state.getParameter (paramId);
    jassert (param != nullptr);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    slider.setTooltip (param->getName (64));
    addAndMakeVisible (slider);
}

void Knob::resized()
{
    const auto size = std::min (getWidth(), getHeight() - 26);
    slider.setBounds ((getWidth() - size) / 2, 0, size, size);
}

void Knob::paint (juce::Graphics& g)
{
    auto text = getLocalBounds().withTop (slider.getBottom() + 1);
    g.setFont (font (11));
    g.setColour (theme.dim);
    g.drawText (name, text.removeFromTop (13), juce::Justification::centred);
    g.setFont (font (10));
    g.setColour (slider.isMouseButtonDown() ? theme.fg : theme.faint);
    g.drawText (slider.getTextFromValue (slider.getValue()), text.removeFromTop (12), juce::Justification::centred);
}

//==============================================================================
Choice::Choice (juce::RangedAudioParameter& p, juce::StringArray l, juce::Colour c, const Theme& t, Painter painterFn)
    : labels (std::move (l)), colour (c), theme (t), painter (std::move (painterFn)),
      attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
{
    attachment.sendInitialUpdate();
}

void Choice::mouseDown (const juce::MouseEvent& e)
{
    const auto index = juce::jlimit (0, labels.size() - 1, e.x * labels.size() / std::max (1, getWidth()));
    attachment.setValueAsCompleteGesture (static_cast<float> (index));
}

void Choice::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (theme.control);
    g.fillRoundedRectangle (bounds, 4);
    const auto cell = bounds.getWidth() / labels.size();

    for (int i = 0; i < labels.size(); ++i)
    {
        const auto r = juce::Rectangle<float> (bounds.getX() + i * cell, bounds.getY(), cell, bounds.getHeight()).reduced (2);
        const bool on = i == selected;

        if (on)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (r, 3);
        }

        g.setColour (on ? juce::Colours::black : theme.dim);

        if (painter)
        {
            painter (g, i, r, on);
        }
        else
        {
            g.setFont (font (11, on));
            g.drawText (labels[i], r, juce::Justification::centred);
        }
    }
}

//==============================================================================
ChoiceBox::ChoiceBox (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::StringArray& labels)
    : attachment ((box.addItemList (labels, 1), state), paramId, box)
{
    addAndMakeVisible (box);
}

//==============================================================================
XYPad::XYPad (juce::RangedAudioParameter& xp, juce::RangedAudioParameter& yp, juce::Colour c, const Theme& t)
    : xParam (xp), yParam (yp),
      xAttachment (xp, [this] (float v) { x = xParam.convertTo0to1 (v); repaint(); }),
      yAttachment (yp, [this] (float v) { y = yParam.convertTo0to1 (v); repaint(); }),
      colour (c), theme (t)
{
    xAttachment.sendInitialUpdate();
    yAttachment.sendInitialUpdate();
}

void XYPad::moveTo (juce::Point<float> p)
{
    const auto f = field();
    const auto nx = juce::jlimit (0.0f, 1.0f, (p.x - f.getX()) / f.getWidth());
    const auto ny = juce::jlimit (0.0f, 1.0f, 1 - (p.y - f.getY()) / f.getHeight());
    xAttachment.setValueAsPartOfGesture (xParam.convertFrom0to1 (nx));
    yAttachment.setValueAsPartOfGesture (yParam.convertFrom0to1 (ny));
}

void XYPad::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    xAttachment.beginGesture();
    yAttachment.beginGesture();
    moveTo (e.position);
}

void XYPad::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        moveTo (e.position);
}

void XYPad::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    xAttachment.endGesture();
    yAttachment.endGesture();
}

void XYPad::mouseDoubleClick (const juce::MouseEvent&)
{
    xAttachment.setValueAsCompleteGesture (xParam.convertFrom0to1 (xParam.getDefaultValue()));
    yAttachment.setValueAsCompleteGesture (yParam.convertFrom0to1 (yParam.getDefaultValue()));
}

void XYPad::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawScreen (g, bounds, theme);
    const auto f = field();

    if (background)
        background (g, f);

    g.setColour (theme.line.withAlpha (0.5f));
    for (int i = 1; i < 4; ++i)
    {
        g.drawVerticalLine (juce::roundToInt (f.getX() + f.getWidth() * i / 4), f.getY(), f.getBottom());
        g.drawHorizontalLine (juce::roundToInt (f.getY() + f.getHeight() * i / 4), f.getX(), f.getRight());
    }

    const auto dot = juce::Point<float> (f.getX() + x * f.getWidth(), f.getBottom() - y * f.getHeight());
    g.setColour (colour.withAlpha (0.35f));
    g.drawLine (f.getX(), dot.y, f.getRight(), dot.y, 1);
    g.drawLine (dot.x, f.getY(), dot.x, f.getBottom(), 1);
    g.setColour (colour.withAlpha (0.25f));
    g.fillEllipse (juce::Rectangle<float> (26, 26).withCentre (dot));
    g.setColour (colour);
    g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre (dot));
    g.setColour (theme.bg);
    g.fillEllipse (juce::Rectangle<float> (4, 4).withCentre (dot));

    g.setFont (font (10));
    g.setColour (theme.dim);
    const auto xText = xParam.getName (24) + "  " + xParam.getText (x, 24);
    const auto yText = yParam.getName (24) + "  " + yParam.getText (y, 24);
    g.drawText (xText, bounds.reduced (8, 5).toNearestInt(), juce::Justification::bottomRight);
    g.drawText (yText, bounds.reduced (8, 5).toNearestInt(), juce::Justification::topLeft);
}

//==============================================================================
PresetBar::PresetBar (juce::AudioProcessor& p, const Theme&) : processor (p)
{
    juce::String category;

    for (int i = 0; i < processor.getNumPrograms(); ++i)
    {
        // Names as "Category: Name" group under headings.
        const auto name = processor.getProgramName (i);
        const auto colon = name.indexOf (": ");

        if (colon > 0 && name.substring (0, colon) != category)
        {
            category = name.substring (0, colon);
            presets.addSectionHeading (category);
        }

        presets.addItem (colon > 0 ? name.substring (colon + 2) : name, i + 1);
    }

    presets.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.onChange = [this] { if (presets.getSelectedId() > 0) processor.setCurrentProgram (presets.getSelectedId() - 1); };
    previous.onClick = [this] { presets.setSelectedId ((presets.getSelectedId() + processor.getNumPrograms() - 2) % processor.getNumPrograms() + 1); };
    next.onClick = [this] { presets.setSelectedId (presets.getSelectedId() % processor.getNumPrograms() + 1); };
    addAndMakeVisible (presets);
    addAndMakeVisible (previous);
    addAndMakeVisible (next);
    startTimerHz (4);
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    previous.setBounds (r.removeFromLeft (28));
    next.setBounds (r.removeFromRight (28));
    presets.setBounds (r.reduced (4, 0));
}

void PresetBar::timerCallback()
{
    if (presets.getSelectedId() != processor.getCurrentProgram() + 1)
        presets.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
}

//==============================================================================
Keyboard::Keyboard (juce::MidiKeyboardState& state, const Theme& t) : MidiKeyboardComponent (state, horizontalKeyboard), theme (t)
{
    setColour (keySeparatorLineColourId, juce::Colours::transparentBlack);
    setColour (mouseOverKeyOverlayColourId, juce::Colours::transparentBlack);
    setColour (keyDownOverlayColourId, juce::Colours::transparentBlack);
    setScrollButtonsVisible (false);

    // Played with the mouse; typed keys are left to the host (and its own typing keyboard).
    setWantsKeyboardFocus (false);
}

void Keyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour, juce::Colour)
{
    auto key = area.reduced (0.5f, 0).withTrimmedBottom (1);
    g.setGradientFill (juce::ColourGradient (isDown ? theme.accent : juce::Colour (0xfff4f1e8), 0, key.getY(), isDown ? theme.accent.darker (0.2f) : juce::Colour (0xffd9d4c4), 0, key.getBottom(), false));
    g.fillRoundedRectangle (key, 2);

    if (isOver && ! isDown)
    {
        g.setColour (theme.accent.withAlpha (0.2f));
        g.fillRoundedRectangle (key, 2);
    }

    if (note % 12 == 0)
    {
        g.setColour (juce::Colour (0xff7a7668));
        g.setFont (font (9));
        g.drawText ("C" + juce::String (note / 12 - 1), key.withTrimmedBottom (4).toNearestInt(), juce::Justification::centredBottom);
    }
}

void Keyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour)
{
    auto key = area.reduced (1, 0);
    g.setGradientFill (juce::ColourGradient (isDown ? theme.accent : juce::Colour (0xff2a2a2e), 0, key.getY(), isDown ? theme.accent.darker (0.3f) : juce::Colour (0xff0e0e10), 0, key.getBottom(), false));
    g.fillRoundedRectangle (key, 2);

    if (isOver && ! isDown)
    {
        g.setColour (theme.accent.withAlpha (0.25f));
        g.fillRoundedRectangle (key, 2);
    }
}

//==============================================================================
void layoutRow (juce::Rectangle<int> area, const std::vector<juce::Component*>& items)
{
    if (items.empty())
        return;

    const auto w = area.getWidth() / static_cast<int> (items.size());

    for (auto* c : items)
        c->setBounds (area.removeFromLeft (w));
}

} // namespace gkp
