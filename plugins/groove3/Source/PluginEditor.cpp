#include "PluginEditor.h"
#include "HostLink.h"

namespace groove3
{

namespace
{
    constexpr int margin = 16, topHeight = 84, labelHeight = 13;
    constexpr int pageSteps = 16;

    /** The longest pattern the step section edits in GrooveKit, in steps (sixteen pages). */
    constexpr int maxHostSteps = 256;

    const juce::String dot = juce::String::charToString (0x00b7);

    rhythm::Style makeStyle()
    {
        rhythm::Style s;
        s.name = "Groove-3";
        s.tagline = juce::String (juce::CharPointer_UTF8 ("Bass line synthesizer  \xc2\xb7  GrooveKit"));
        s.panelTop = juce::Colour (0xff332c25);
        s.panelBottom = juce::Colour (0xff1f1b17);
        s.strip = juce::Colour (0xff13110e);
        s.well = juce::Colour (0xff1a1713);
        s.wellEdge = juce::Colour (0xff3d362d);
        s.ink = juce::Colour (0xfff0e6d2);
        s.inkDim = juce::Colour (0xff9a8f7d);
        s.accent = juce::Colour (0xfff2a93b);
        s.led = juce::Colour (0xffffc94d);
        s.keyTop = juce::Colour (0xffefe8d8);
        s.keyBottom = juce::Colour (0xffcdc4af);
        s.keyEdge = juce::Colour (0xff6c6352);
        s.keyInk = juce::Colour (0xff2a241d);
        s.keyMark = juce::Colour (0xffd9862b);
        s.knobSkirt = juce::Colour (0xff17140f);
        s.knobCap = juce::Colour (0xff2a2520);
        s.knobCapLevel = juce::Colour (0xff6b3f17);
        s.knobPointer = juce::Colour (0xfff2a93b);
        s.brushed = true;
        return s;
    }

    juce::String keyText (int key)      { return noteName (key); }
}

//==============================================================================
/** A knob with its name printed above; while it is turned (or the mouse is on it) the name
    gives way to its value. */
class Editor::Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& name, const rhythm::Style& s, bool level, bool isSmall)
        : Knob (name, s, level, isSmall)
    {
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
        auto* param = state.getParameter (paramId);
        jassert (param != nullptr);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        slider.setTooltip (param->getName (64));
    }

    /** A knob for a value that isn't the plugin's parameter (GrooveKit's, while connected). */
    Knob (const juce::String& name, const rhythm::Style& s, bool level, bool isSmall)
        : label (name), style (s), small (isSmall)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
        slider.setMouseDragSensitivity (200);
        slider.setVelocityModeParameters (0.6, 1, 0.05, true, juce::ModifierKeys::shiftModifier);
        slider.getProperties().set ("level", level);
        slider.onValueChange = [this] { repaint(); };
        slider.addMouseListener (this, false);
        addAndMakeVisible (slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (labelHeight);
        const auto size = std::min (r.getWidth(), r.getHeight());
        slider.setBounds (r.withSizeKeepingCentre (size, size).withY (labelHeight));
    }

    void paint (juce::Graphics& g) override
    {
        const bool showValue = slider.isMouseOverOrDragging();
        g.setFont (rhythm::printFont (small ? 8.5f : 9.5f));
        g.setColour (showValue ? style.accent : style.ink.withAlpha (0.92f));
        g.drawText (showValue ? slider.getTextFromValue (slider.getValue()) : label.toUpperCase(), getLocalBounds().withHeight (labelHeight), juce::Justification::centred);
    }

    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void mouseUp (const juce::MouseEvent&) override     { repaint(); }

    juce::Slider slider;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String label;
    const rhythm::Style& style;
    const bool small;
};

//==============================================================================
/** A toggle switch for a choice: a lever in a slot, its positions printed beside it, top to
    bottom. A click on a position (or on the lever, to move it on) sets it. */
class Editor::Switch : public juce::Component, public juce::SettableTooltipClient
{
public:
    Switch (juce::RangedAudioParameter& p, const juce::String& name, const juce::StringArray& positions, const rhythm::Style& s)
        : title (name), names (positions), style (s), attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
    {
        setTooltip (p.getName (64));
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (rhythm::printFont (8.5f));
        g.setColour (style.ink.withAlpha (0.92f));
        g.drawText (title.toUpperCase(), getLocalBounds().withHeight (labelHeight), juce::Justification::centred);

        const auto slot = slotArea();
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRoundedRectangle (slot, 4);
        g.setColour (style.wellEdge);
        g.drawRoundedRectangle (slot, 4, 0.8f);

        // The lever: a chromed bat, its shadow below it.
        const auto lever = juce::Rectangle<float> (slot.getWidth() + 6, 11).withCentre ({ slot.getCentreX(), positionY (selected) });
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (lever.translated (1, 2), 4);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8e4dc), lever.getX(), lever.getY(), juce::Colour (0xff6d6a64), lever.getRight(), lever.getBottom(), false));
        g.fillRoundedRectangle (lever, 4);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (lever, 4, 0.7f);

        g.setFont (rhythm::printFont (8.5f, false));

        for (int i = 0; i < names.size(); ++i)
        {
            g.setColour (i == selected ? style.accent : style.inkDim);
            g.drawText (names[i].toUpperCase(), juce::Rectangle<float> (slot.getRight() + 9, positionY (i) - 6, getWidth() - slot.getRight() - 9, 12), juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto choice = (selected + 1) % names.size();

        if (e.position.x > slotArea().getRight() + 4)
        {
            auto nearest = 1.0e9f;

            for (int i = 0; i < names.size(); ++i)
                if (const auto d = std::abs (e.position.y - positionY (i)); d < nearest)
                {
                    nearest = d;
                    choice = i;
                }
        }

        attachment.setValueAsCompleteGesture (static_cast<float> (choice));
    }

private:
    juce::String title;
    juce::StringArray names;
    const rhythm::Style& style;
    juce::ParameterAttachment attachment;
    int selected = 0;

    juce::Rectangle<float> slotArea() const
    {
        return { 10, labelHeight + 6.0f, 9, static_cast<float> (getHeight() - labelHeight - 12) };
    }

    float positionY (int index) const
    {
        const auto slot = slotArea().reduced (0, 6);
        return names.size() <= 1 ? slot.getCentreY() : slot.getY() + slot.getHeight() * static_cast<float> (index) / static_cast<float> (names.size() - 1);
    }
};

//==============================================================================
/** The sound as it plays: the wave, and under it where the filter's cutoff is (on a scale of
    the audible range), with the main envelope and the accent sweep's charge as bars. */
class Editor::Display : public juce::Component
{
public:
    Display (Processor& p, const rhythm::Style& s) : processor (p), style (s) {}

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (area, 6);
        const auto glass = area.reduced (3);
        const auto tint = style.accent.withSaturation (0.4f).darker (3.4f);
        g.setGradientFill (juce::ColourGradient (tint.brighter (0.2f), 0, glass.getY(), tint.darker (0.5f), 0, glass.getBottom(), false));
        g.fillRoundedRectangle (glass, 4);

        auto inner = glass.reduced (10, 8);
        auto meters = inner.removeFromBottom (34);
        inner.removeFromBottom (4);

        // The wave, from a rising zero crossing so a steady note stands still.
        std::array<float, 1024> samples {};
        processor.readScope (samples.data(), static_cast<int> (samples.size()));
        size_t start = 0;

        for (size_t i = 1; i < samples.size() / 2; ++i)
            if (samples[i - 1] <= 0 && samples[i] > 0)
            {
                start = i;
                break;
            }

        juce::Path wave;
        const auto count = samples.size() / 2;
        auto peak = 0.05f;

        for (size_t i = 0; i < count; ++i)
            peak = std::max (peak, std::abs (samples[start + i]));

        for (size_t i = 0; i < count; ++i)
        {
            const auto x = inner.getX() + inner.getWidth() * static_cast<float> (i) / static_cast<float> (count - 1);
            const auto y = inner.getCentreY() - samples[start + i] / peak * inner.getHeight() * 0.45f;

            if (i == 0)
                wave.startNewSubPath (x, y);
            else
                wave.lineTo (x, y);
        }

        g.setColour (style.accent.withAlpha (0.12f));
        g.drawHorizontalLine (juce::roundToInt (inner.getCentreY()), inner.getX(), inner.getRight());
        g.setColour (style.accent.withAlpha (0.25f));
        g.strokePath (wave, juce::PathStrokeType (3.5f));
        g.setColour (style.accent.brighter (0.3f));
        g.strokePath (wave, juce::PathStrokeType (1.3f));

        // The cutoff on a scale from 20 Hz to 20 kHz.
        auto scale = meters.removeFromTop (14);
        const auto cutoff = processor.cutoffHz.load();
        g.setColour (style.accent.withAlpha (0.18f));
        g.fillRoundedRectangle (scale.withSizeKeepingCentre (scale.getWidth(), 3), 1.5f);

        for (auto hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const auto x = scale.getX() + scale.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f);
            g.drawVerticalLine (juce::roundToInt (x), scale.getCentreY() - 4, scale.getCentreY() + 4);
        }

        g.setFont (rhythm::printFont (8));

        if (cutoff > 0)
        {
            const auto x = scale.getX() + scale.getWidth() * juce::jlimit (0.0f, 1.0f, std::log (cutoff / 20.0f) / std::log (1000.0f));
            g.setColour (style.led);
            g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre ({ x, scale.getCentreY() }));
            g.setColour (style.led.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (15, 15).withCentre ({ x, scale.getCentreY() }));
        }

        meters.removeFromTop (4);
        g.setColour (style.accent.withAlpha (0.6f));
        const auto text = cutoff > 0 ? "CUTOFF " + (cutoff >= 1000 ? juce::String (cutoff / 1000, 2) + " KHZ" : juce::String (juce::roundToInt (cutoff)) + " HZ") : juce::String ("CUTOFF");
        g.drawText (text, meters.removeFromLeft (110), juce::Justification::centredLeft);

        // The envelope and the sweep.
        for (auto [name, level] : { std::pair<const char*, float> { "ENV", processor.envelope.load() }, { "SWEEP", processor.sweep.load() } })
        {
            auto cell = meters.removeFromLeft (meters.getWidth() > 120 ? meters.getWidth() / 2 : meters.getWidth());
            g.setColour (style.accent.withAlpha (0.6f));
            g.drawText (name, cell.removeFromLeft (38), juce::Justification::centredLeft);
            const auto bar = cell.withSizeKeepingCentre (cell.getWidth() - 8, 5);
            g.setColour (style.accent.withAlpha (0.15f));
            g.fillRoundedRectangle (bar, 2);
            g.setColour (style.led);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, level)), 2);
        }
    }

private:
    Processor& processor;
    const rhythm::Style& style;
};

//==============================================================================
/** The step section's screen: the page, what's being edited (GrooveKit's pattern or the own
    line), where it plays, and the step picked. */
class Editor::Screen : public juce::Component
{
public:
    explicit Screen (Editor& e) : editor (e) {}

    void paint (juce::Graphics& g) override
    {
        const auto& s = editor.style;
        const auto area = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (area, 6);
        const auto glass = area.reduced (3);
        const auto tint = s.accent.withSaturation (0.35f).darker (3.2f);
        g.setGradientFill (juce::ColourGradient (tint.brighter (0.25f), 0, glass.getY(), tint.darker (0.4f), 0, glass.getBottom(), false));
        g.fillRoundedRectangle (glass, 4);
        g.setColour (s.accent.withAlpha (0.05f));

        for (auto y = glass.getY() + 1; y < glass.getBottom(); y += 2)
            g.drawHorizontalLine (static_cast<int> (y), glass.getX() + 2, glass.getRight() - 2);

        auto inner = glass.reduced (10, 5);
        const auto bright = s.accent.brighter (0.3f), dim = s.accent.withAlpha (0.45f);

        auto numberArea = inner.removeFromLeft (52);
        g.setColour (dim);
        g.setFont (rhythm::printFont (8));
        g.drawText ("PAGE", numberArea.withHeight (12), juce::Justification::topLeft);
        g.setColour (s.accent.withAlpha (0.12f));
        g.setFont (juce::Font (juce::FontOptions ("Menlo", 30, juce::Font::bold)));
        g.drawText ("88", numberArea.withTrimmedTop (10), juce::Justification::centredLeft);
        g.setColour (bright);
        g.drawText (juce::String (editor.page + 1).paddedLeft ('0', 2), numberArea.withTrimmedTop (10), juce::Justification::centredLeft);

        inner.removeFromLeft (8);
        g.setFont (rhythm::printFont (12.5f));
        g.drawText (editor.lineName(), inner.withHeight (inner.getHeight() * 0.42f), juce::Justification::centredLeft, true);

        // Where it plays, or how long it is; then the step picked.
        const auto length = editor.length(), pages = editor.numPages(), now = editor.playStep();
        juce::String status;

        if (editor.hostMode() && ! editor.processor.link.isLinked())
            status = "GROOVEKIT";
        else if (now >= 0)
            status = "STEP " + juce::String (now + 1).paddedLeft ('0', 2) + " / " + juce::String (length);
        else if (! editor.hostMode() && editor.processor.value (id::run) < 0.5f)
            status = "STOPPED  " + dot + "  PLAYED BY MIDI";
        else
            status = juce::String (length) + " STEPS  " + dot + "  " + juce::String (pages) + (pages == 1 ? " PAGE" : " PAGES");

        const auto tempo = editor.processor.hostTempo.load();

        if (tempo > 0)
            status += "  " + dot + "  " + juce::String (tempo, 1) + " BPM";

        g.setFont (rhythm::printFont (8.5f, false));
        g.setColour (dim);
        auto lower = inner.withTrimmedTop (inner.getHeight() * 0.42f);
        g.drawText (status, lower.removeFromTop (lower.getHeight() * 0.5f), juce::Justification::centredLeft, true);

        const auto line = editor.line();
        const auto picked = editor.selected;
        juce::String stepText = "STEP " + juce::String (picked + 1).paddedLeft ('0', 2) + "  ";

        if (juce::isPositiveAndBelow (picked, static_cast<int> (line.size())))
        {
            const auto& step = line[static_cast<size_t> (picked)];

            if (step.time == Step::note)
                stepText += keyText (step.key) + (step.accent ? "  ACCENT" : "") + (step.slide ? "  SLIDE" : "");
            else
                stepText += step.time == Step::tie ? "TIE" : "REST";
        }

        g.setColour (bright.withAlpha (0.85f));
        g.drawText (stepText, lower, juce::Justification::centredLeft, true);
    }

private:
    Editor& editor;
};

//==============================================================================
/** The sixteen steps of the page: over them the line's shape (each note at its pitch, ties
    holding it on, slides gliding into the next), then a light and a key per step with its note. */
class Editor::StepCells : public juce::Component
{
public:
    explicit StepCells (Editor& e) : editor (e)
    {
        setWantsKeyboardFocus (true);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& s = editor.style;
        const auto line = editor.line();
        const auto first = editor.page * pageSteps;
        const auto length = editor.length();
        const auto now = editor.playStep();
        const auto stepAt = [&] (int i) -> const Step* { return juce::isPositiveAndBelow (i, std::min (length, static_cast<int> (line.size()))) ? &line[static_cast<size_t> (i)] : nullptr; };

        // The shape: the page's pitches spread over the lane.
        auto low = 127, high = 0;

        for (int i = first; i < first + pageSteps; ++i)
            if (auto* step = stepAt (i); step != nullptr && step->time == Step::note)
            {
                low = std::min (low, step->key);
                high = std::max (high, step->key);
            }

        if (low > high)
            low = high = 36;

        const auto lane = laneArea();
        const auto yFor = [&] (int key)
        {
            const auto span = std::max (12, high - low);
            const auto mid = (low + high) * 0.5f;
            return lane.getCentreY() - (static_cast<float> (key) - mid) / static_cast<float> (span) * (lane.getHeight() - 8);
        };

        g.setColour (s.accent.withAlpha (0.08f));
        g.fillRoundedRectangle (lane, 4);

        for (int i = 0; i < pageSteps; ++i)
        {
            const auto* step = stepAt (first + i);
            const auto start = editor.processor.link.isLinked() || ! editor.hostMode() ? noteStart (line, first + i) : -1;

            if (step == nullptr || start < 0)
                continue;

            const auto& note = line[static_cast<size_t> (start)];
            const auto cell = keyArea (i);
            const auto y = yFor (note.key);
            auto x0 = cell.getX() + 3, x1 = cell.getRight() - 3;

            // A tie runs on from the step before, without a gap.
            if (step->time == Step::tie)
                x0 = cell.getX() - 3;

            g.setColour (note.accent ? s.led : s.accent.withAlpha (0.8f));
            g.fillRoundedRectangle (juce::Rectangle<float> (x0, y - 2.5f, x1 - x0, 5), 2.5f);

            // A slide: a line from this note's end to the next note.
            const auto next = first + i + 1;

            if (note.slide && stepAt (next) != nullptr && line[static_cast<size_t> (next)].time == Step::note && i + 1 < pageSteps)
            {
                g.setColour (s.led.withAlpha (0.8f));
                g.drawLine (x1, y, keyArea (i + 1).getX() + 3, yFor (line[static_cast<size_t> (next)].key), 2);
            }
        }

        for (int i = 0; i < pageSteps; ++i)
        {
            const auto index = first + i;
            const auto* step = stepAt (index);
            const auto key = keyArea (i);
            const bool picked = index == editor.selected;
            const bool playing = index == now;
            const bool beyond = step == nullptr;
            const auto time = beyond ? Step::rest : step->time;

            rhythm::drawLed (g, { key.getCentreX(), key.getY() - 9 }, 4.2f, s.led, playing ? 1.0f : time == Step::note ? 0.55f : time == Step::tie ? 0.2f : 0.0f);

            // The key: its shadow, its cap lit from above, the picked one ringed in the accent.
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (key.translated (0, 2.5f), 5);
            const auto cap = key.translated (0, time == Step::note ? 1.2f : 0.0f);
            const auto top = beyond ? s.keyBottom.darker (0.5f) : time == Step::rest ? s.keyBottom.darker (0.12f) : s.keyTop;
            g.setGradientFill (juce::ColourGradient (top, 0, cap.getY(), s.keyBottom.darker (beyond ? 0.55f : 0.0f), 0, cap.getBottom(), false));
            g.fillRoundedRectangle (cap, 5);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.drawHorizontalLine (juce::roundToInt (cap.getY() + 1.5f), cap.getX() + 4, cap.getRight() - 4);
            g.setColour (picked ? s.accent : s.keyEdge);
            g.drawRoundedRectangle (cap.reduced (picked ? 1.0f : 0.5f), 5, picked ? 2.2f : 1.0f);

            if (! beyond)
            {
                g.setColour (s.keyInk.withAlpha (time == Step::rest ? 0.35f : 0.9f));
                g.setFont (rhythm::printFont (time == Step::note ? 12.0f : 14.0f));
                const auto text = time == Step::note ? keyText (step->key) : time == Step::tie ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : dot;
                g.drawText (text, cap.withTrimmedBottom (cap.getHeight() * 0.4f).translated (0, 2), juce::Justification::centred);

                // Accent and slide, as marks lit on the key.
                if (time == Step::note)
                {
                    auto marks = cap.withTrimmedTop (cap.getHeight() * 0.6f).reduced (5, 4);
                    const auto w = marks.getWidth() / 2;

                    for (auto [text2, on] : { std::pair<const char*, bool> { "A", step->accent }, { "S", step->slide } })
                    {
                        const auto mark = marks.removeFromLeft (w).reduced (1.5f, 0);
                        g.setColour (on ? s.keyMark : s.keyInk.withAlpha (0.08f));
                        g.fillRoundedRectangle (mark, 2.5f);
                        g.setColour (on ? s.keyTop : s.keyInk.withAlpha (0.3f));
                        g.setFont (rhythm::printFont (8.5f));
                        g.drawText (text2, mark, juce::Justification::centred);
                    }
                }
            }

            g.setColour (playing ? s.accent : s.inkDim);
            g.setFont (rhythm::printFont (9.5f));
            g.drawText (juce::String (index + 1), juce::Rectangle<float> (key.getX(), key.getBottom() + 5, key.getWidth(), 12), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto i = cellAt (e.position);

        if (i < 0)
            return;

        grabKeyboardFocus();
        editor.select (i);
        dragFrom = -1;

        if (e.mods.isPopupMenu())
        {
            showMenu (i);
            return;
        }

        const auto line = editor.line();

        if (juce::isPositiveAndBelow (i, static_cast<int> (line.size())) && line[static_cast<size_t> (i)].time == Step::note)
        {
            dragFrom = line[static_cast<size_t> (i)].key;
            dragStep = i;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFrom < 0)
            return;

        // Up or down a semitone every few pixels: one change to undo for the whole drag.
        const auto key = juce::jlimit (0, 127, dragFrom - juce::roundToInt (e.getDistanceFromDragStartY() / 7.0f));

        if (key == dragKey)
            return;

        if (! dragging)
        {
            dragging = true;

            if (auto* h = editor.host())
                h->beginEdit();
        }

        dragKey = key;
        editor.edit (dragStep, [&] (Line& l) { l[static_cast<size_t> (dragStep)].key = key; }, false);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
            if (auto* h = editor.host())
                h->endEdit();

        dragging = false;
        dragFrom = dragKey = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto i = cellAt (e.position);

        if (i < 0)
            return;

        editor.edit (i, [&] (Line& l)
        {
            auto& step = l[static_cast<size_t> (i)];
            step.time = step.time == Step::note ? Step::rest : Step::note;

            if (step.time == Step::note)
                step.key = editor.lastKey;
        }, false);
    }

    bool keyPressed (const juce::KeyPress& key) override     { return editor.keyPressed (key); }

private:
    Editor& editor;
    int dragFrom = -1, dragKey = -1, dragStep = 0;
    bool dragging = false;

    juce::Rectangle<float> laneArea() const     { return getLocalBounds().toFloat().withHeight (30).reduced (2, 0); }

    juce::Rectangle<float> keyArea (int i) const
    {
        const auto w = static_cast<float> (getWidth()) / pageSteps;
        return { i * w + 4, 48, w - 8, static_cast<float> (getHeight() - 48 - 20) };
    }

    int cellAt (juce::Point<float> p) const
    {
        const auto i = static_cast<int> (p.x / (static_cast<float> (getWidth()) / pageSteps));
        const auto index = editor.page * pageSteps + i;
        return juce::isPositiveAndBelow (i, pageSteps) && index < editor.length() ? index : -1;
    }

    void showMenu (int i)
    {
        const auto line = editor.line();
        const auto& step = line[static_cast<size_t> (i)];
        const bool note = step.time == Step::note;
        juce::PopupMenu menu;
        menu.addItem (1, "Note", true, note);
        menu.addItem (2, "Tie", noteStart (line, i - 1) >= 0, step.time == Step::tie);
        menu.addItem (3, "Rest", true, step.time == Step::rest);
        menu.addSeparator();
        menu.addItem (4, "Accent", note, note && step.accent);
        menu.addItem (5, "Slide", note, note && step.slide);
        menu.addSeparator();
        menu.addItem (6, "Octave up", note);
        menu.addItem (7, "Octave down", note);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, i, safe = juce::Component::SafePointer<StepCells> (this)] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            editor.edit (i, [&] (Line& l)
            {
                auto& s = l[static_cast<size_t> (i)];

                switch (result)
                {
                    case 1: if (s.time != Step::note) s.key = editor.lastKey; s.time = Step::note; break;
                    case 2: s.time = Step::tie; break;
                    case 3: s.time = Step::rest; break;
                    case 4: s.accent = ! s.accent; break;
                    case 5: s.slide = ! s.slide; break;
                    case 6: s.key = std::min (127, s.key + 12); break;
                    case 7: s.key = std::max (0, s.key - 12); break;
                    default: break;
                }
            }, false);
        });
    }
};

//==============================================================================
/** The panel's keyboard, an octave from C to C: a key writes the picked step's note (and plays
    it), then picks the next step. The picked note's key is marked, the one sounding lit. */
class Editor::Keys : public juce::Component
{
public:
    explicit Keys (Editor& e) : editor (e) {}

    void paint (juce::Graphics& g) override
    {
        const auto& s = editor.style;
        const auto line = editor.line();
        const auto picked = juce::isPositiveAndBelow (editor.selected, static_cast<int> (line.size())) ? line[static_cast<size_t> (editor.selected)] : Step {};
        const auto base = editor.keyFor (0);
        const auto playing = editor.processor.soundingKey.load();

        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i <= 12; ++i)
            {
                const bool black = isBlack (i);

                if (black != (pass == 1))
                    continue;

                const auto r = keyArea (i);
                const bool marked = picked.time == Step::note && picked.key == base + i;
                const bool lit = playing == base + i || i == held;
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillRoundedRectangle (r.translated (0, 2), 3);
                const auto top = black ? juce::Colour (0xff2b2722) : s.keyTop;
                const auto bottom = black ? juce::Colour (0xff121110) : s.keyBottom;
                g.setGradientFill (juce::ColourGradient (lit ? top.interpolatedWith (s.accent, 0.5f) : top, 0, r.getY(), lit ? bottom.interpolatedWith (s.accent, 0.5f) : bottom, 0, r.getBottom(), false));
                g.fillRoundedRectangle (r, 3);
                g.setColour (black ? juce::Colours::black : s.keyEdge);
                g.drawRoundedRectangle (r.reduced (0.5f), 3, 0.8f);

                if (marked)
                {
                    g.setColour (s.accent);
                    g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre ({ r.getCentreX(), r.getBottom() - (black ? 9.0f : 12.0f) }));
                }

                if (! black)
                {
                    g.setColour (s.keyInk.withAlpha (0.55f));
                    g.setFont (rhythm::printFont (8.5f));
                    g.drawText (i == 0 || i == 12 ? keyText (base + i) : juce::String::charToString (static_cast<juce::juce_wchar> (noteLetter (i))), r.withTrimmedTop (r.getHeight() - 26).withTrimmedBottom (16), juce::Justification::centred);
                }
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        held = keyAt (e.position);

        if (held < 0)
            return;

        sounding = editor.keyFor (held);
        editor.processor.keyboardState.noteOn (1, sounding, 0.8f);
        editor.writeKey (held);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (sounding >= 0)
            editor.processor.keyboardState.noteOff (1, sounding, 0.0f);

        sounding = held = -1;
        repaint();
    }

private:
    Editor& editor;
    int held = -1, sounding = -1;

    static bool isBlack (int i)         { const auto n = i % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
    static char noteLetter (int i)      { return "CCDDEFFGGAAB"[i % 12]; }

    static int whiteIndex (int i)
    {
        int w = 0;

        for (int k = 0; k < i; ++k)
            if (! isBlack (k))
                ++w;

        return w;
    }

    juce::Rectangle<float> keyArea (int i) const
    {
        const auto whiteWidth = static_cast<float> (getWidth()) / 8;
        const auto h = static_cast<float> (getHeight()) - 3;

        if (! isBlack (i))
            return { whiteIndex (i) * whiteWidth + 1, 0, whiteWidth - 2, h };

        return juce::Rectangle<float> (whiteWidth * 0.62f, h * 0.6f).withCentre ({ whiteIndex (i) * whiteWidth, h * 0.3f });
    }

    int keyAt (juce::Point<float> p) const
    {
        for (int i = 0; i <= 12; ++i)
            if (isBlack (i) && keyArea (i).contains (p))
                return i;

        for (int i = 0; i <= 12; ++i)
            if (! isBlack (i) && keyArea (i).contains (p))
                return i;

        return -1;
    }
};

//==============================================================================
/** Start and stop: the plugin's own line (it plays while the host plays), or in GrooveKit the
    pattern on a loop. Its light beats the quarters while it runs. */
class Editor::StartKey : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StartKey (Editor& e)
        : editor (e), attachment (*e.processor.state.getParameter (id::run), [this] (float v) { on = v >= 0.5f; repaint(); })
    {
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& s = editor.style;
        const bool host = editor.hostMode();
        const bool lit = host ? editor.processor.link.isHostPlaying() : on;
        auto key = getLocalBounds().toFloat().reduced (2).withTrimmedTop (18);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (key.translated (0, 2.5f), 6);
        key = key.translated (0, lit ? 1.2f : 0);
        const auto top = lit ? s.accent : s.keyTop;
        g.setGradientFill (juce::ColourGradient (top.brighter (0.1f), 0, key.getY(), lit ? s.accent.darker (0.3f) : s.keyBottom, 0, key.getBottom(), false));
        g.fillRoundedRectangle (key, 6);
        g.setColour (s.keyEdge);
        g.drawRoundedRectangle (key.reduced (0.5f), 6, 1);
        g.setColour (lit ? s.strip : s.keyInk);
        g.setFont (rhythm::printFont (11));
        g.drawText ("START", key.withHeight (key.getHeight() * 0.5f).translated (0, 6), juce::Justification::centred);
        g.setFont (rhythm::printFont (9, false));
        g.drawText ("STOP", key.withTrimmedTop (key.getHeight() * 0.5f).translated (0, -4), juce::Justification::centred);

        const auto now = editor.playStep();
        rhythm::drawLed (g, { getWidth() / 2.0f, 8 }, 4.5f, s.led, lit ? (now >= 0 && now % 4 == 0 ? 1.0f : 0.45f) : 0.0f);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (! editor.hostMode())
        {
            attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
            return;
        }

        if (auto* host = editor.host())
        {
            if (editor.processor.link.isHostPlaying())
                host->stop();
            else
                host->play();
        }
    }

    void update()
    {
        setTooltip (editor.hostMode() ? "Play this pattern on a loop in GrooveKit, or stop"
                                      : "Start and stop the line (it plays while the host plays)");
        repaint();
    }

private:
    Editor& editor;
    juce::ParameterAttachment attachment;
    bool on = false;
};

//==============================================================================
Editor::Editor (Processor& p)
    : AudioProcessorEditor (p), processor (p), style (makeStyle()), look (style), theme (rhythm::themeFor (style)), presets (p, theme)
{
    setLookAndFeel (&look);
    addAndMakeVisible (presets);

    const auto addKnob = [&] (std::vector<std::unique_ptr<Knob>>& into, const juce::String& paramId, const juce::String& name, bool level, bool small)
    {
        addAndMakeVisible (*into.emplace_back (std::make_unique<Knob> (processor.state, paramId, name, style, level, small)));
    };

    for (auto [paramId, name] : { std::pair<const juce::String&, const char*> { id::tuning, "Tuning" }, { id::cutoff, "Cutoff" }, { id::resonance, "Resonance" },
                                  { id::envMod, "Env Mod" }, { id::decay, "Decay" }, { id::accent, "Accent" } })
        addKnob (voiceKnobs, paramId, name, false, false);

    for (auto [paramId, name] : { std::pair<const juce::String&, const char*> { id::overdrive, "Overdrive" }, { id::accentDecay, "Acc Decay" }, { id::ampDecay, "Vol Decay" },
                                  { id::softAttack, "Soft Attack" }, { id::slideTime, "Slide Time" }, { id::tracking, "Filter Track" },
                                  { id::filterFm, "Filter FM" }, { id::sub, "Sub" }, { id::distortion, "Distortion" } })
        addKnob (modKnobs, paramId, name, false, true);

    volumeKnob = std::make_unique<Knob> (processor.state, id::volume, "Volume", style, true, false);
    addAndMakeVisible (*volumeKnob);

    const auto addSwitch = [&] (const juce::String& paramId, const juce::String& name, const juce::StringArray& positions)
    {
        return std::make_unique<Switch> (*processor.state.getParameter (paramId), name, positions, style);
    };

    waveformSwitch = addSwitch (id::waveform, "Wave", waveformNames);
    addAndMakeVisible (*waveformSwitch);
    modSwitches.push_back (addSwitch (id::sweepSpeed, "Sweep", sweepSpeedNames));
    modSwitches.push_back (addSwitch (id::resonanceRange, "Resonance", resonanceRangeNames));
    modSwitches.push_back (addSwitch (id::muffler, "Muffler", mufflerNames));
    modSwitches.push_back (addSwitch (id::subOctave, "Sub", subOctaveNames));

    for (auto& s : modSwitches)
        addAndMakeVisible (*s);

    display = std::make_unique<Display> (processor, style);
    screen = std::make_unique<Screen> (*this);
    cells = std::make_unique<StepCells> (*this);
    keys = std::make_unique<Keys> (*this);
    startKey = std::make_unique<StartKey> (*this);

    for (auto* c : std::initializer_list<juce::Component*> { display.get(), screen.get(), cells.get(), keys.get(), startKey.get() })
        addAndMakeVisible (c);

    for (auto* b : { &octaveDown, &octaveUp, &accentKey, &slideKey, &tieKey, &restKey, &back, &forward, &previousPage, &nextPage, &clear, &lines })
        addAndMakeVisible (b);

    lengthKnob = std::make_unique<Knob> (processor.state, id::length, "Length", style, false, true);
    addAndMakeVisible (*lengthKnob);

    // Connected to GrooveKit, Length is its pattern's length.
    hostLength = std::make_unique<Knob> ("Length", style, false, true);
    addChildComponent (*hostLength);
    auto& patternLength = hostLength->slider;
    patternLength.setRange (1, maxHostSteps, 1);
    patternLength.setTooltip ("The pattern's length in steps");
    patternLength.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)); };
    patternLength.onValueChange = [this]
    {
        hostLength->repaint();

        // A turn is sent once it ends: a pattern playing on a loop starts again when its length changes.
        if (! hostLength->slider.isMouseButtonDown())
            sendLength();
    };
    patternLength.onDragEnd = [this] { sendLength(); };

    // Down and Up shift the picked note an octave; on a step without one, the keyboard.
    const auto shiftOctave = [this] (int by)
    {
        const auto l = line();

        if (! juce::isPositiveAndBelow (selected, static_cast<int> (l.size())) || l[static_cast<size_t> (selected)].time != Step::note)
        {
            lastKey = juce::jlimit (0, 115, lastKey + by);
            keys->repaint();
            return;
        }

        edit (selected, [&] (Line& changed) { auto& s = changed[static_cast<size_t> (selected)]; s.key = juce::jlimit (0, 127, s.key + by); }, false);
    };

    octaveDown.onClick = [shiftOctave] { shiftOctave (-12); };
    octaveUp.onClick = [shiftOctave] { shiftOctave (12); };
    accentKey.onClick = [this] { edit (selected, [&] (Line& l) { auto& s = l[static_cast<size_t> (selected)]; if (s.time == Step::note) s.accent = ! s.accent; }, false); };
    slideKey.onClick = [this] { edit (selected, [&] (Line& l) { auto& s = l[static_cast<size_t> (selected)]; if (s.time == Step::note) s.slide = ! s.slide; }, false); };
    tieKey.onClick = [this] { edit (selected, [&] (Line& l) { l[static_cast<size_t> (selected)].time = Step::tie; }, true); };
    restKey.onClick = [this] { edit (selected, [&] (Line& l) { l[static_cast<size_t> (selected)].time = Step::rest; }, true); };
    back.onClick = [this] { select ((selected + length() - 1) % length()); };
    forward.onClick = [this] { select ((selected + 1) % length()); };
    previousPage.onClick = [this] { followPage = false; page = std::max (0, page - 1); select (page * pageSteps); };
    nextPage.onClick = [this] { followPage = false; page = std::min (numPages() - 1, page + 1); select (page * pageSteps); };
    clear.onClick = [this] { clearPage(); };
    lines.onClick = [this]
    {
        juce::PopupMenu menu;

        for (size_t i = 0; i < factoryLines().size(); ++i)
            menu.addItem (static_cast<int> (i) + 1, factoryLines()[i].name);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lines), [this, safe = juce::Component::SafePointer<Editor> (this)] (int result)
        {
            if (safe != nullptr && result > 0)
                loadLine (result - 1);
        });
    };

    octaveDown.setTooltip ("An octave down for the picked note (or the keyboard)");
    octaveUp.setTooltip ("An octave up for the picked note (or the keyboard)");
    accentKey.setTooltip ("Accent the picked note: louder, with a filter sweep (A)");
    slideKey.setTooltip ("Slide from the picked note into the next (S)");
    tieKey.setTooltip ("Hold the note before on through this step (T)");
    restKey.setTooltip ("No note on this step (Delete)");
    clear.setTooltip ("Clear the page");
    lines.setTooltip ("Load one of Groove-3's lines");

    setWantsKeyboardFocus (true);
    setSize (width, height);
    startTimerHz (30);
}

Editor::~Editor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
bool Editor::hostMode() const
{
    return processor.link.isLinked() || rhythm::HostLink::isInsideGrooveKit();
}

gkmachine::IHost* Editor::host() const
{
    return processor.link.getHost();
}

Line Editor::line() const
{
    if (! hostMode())
        return processor.getOwnLine();

    if (pending && processor.link.getChangeCount() == pendingSince)
        return *pending;

    return lineOf (processor.link.getNotes(), length());
}

int Editor::length() const
{
    if (hostMode())
        return juce::jlimit (1, maxHostSteps, processor.link.getPattern().lengthSteps);

    return juce::jlimit (1, maxSteps, juce::roundToInt (processor.value (id::length)));
}

int Editor::numPages() const
{
    return (length() + pageSteps - 1) / pageSteps;
}

int Editor::playStep() const
{
    if (hostMode())
        return processor.link.isHostPlaying() ? processor.link.getPlayStep() : -1;

    return processor.value (id::run) >= 0.5f ? processor.ownStep.load() : -1;
}

juce::String Editor::lineName() const
{
    if (! hostMode())
        return "Groove-3's own line";

    if (! processor.link.isLinked())
        return "Waiting for GrooveKit";

    return processor.link.getPattern().exists ? processor.link.getPattern().name : juce::String ("No pattern yet");
}

int Editor::keyFor (int index) const
{
    const auto l = line();
    auto base = lastKey;

    if (juce::isPositiveAndBelow (selected, static_cast<int> (l.size())) && l[static_cast<size_t> (selected)].time == Step::note)
        base = l[static_cast<size_t> (selected)].key;

    return juce::jlimit (0, 127, base - base % 12 + index);
}

//==============================================================================
void Editor::edit (int step, const std::function<void (Line&)>& change, bool advance)
{
    const auto len = length();

    if (! juce::isPositiveAndBelow (step, len))
        return;

    auto before = line();

    if (static_cast<int> (before.size()) < len)
        before.resize (static_cast<size_t> (len));

    auto after = before;
    change (after);

    // A tie holds on the note before it: after a rest (or first) it's a rest.
    for (size_t i = 0; i < after.size(); ++i)
        if (after[i].time == Step::tie && (i == 0 || after[i - 1].time == Step::rest))
            after[i].time = Step::rest;

    if (after[static_cast<size_t> (step)].time == Step::note)
        lastKey = after[static_cast<size_t> (step)].key;

    if (after != before)
        commit (before, after, step);

    if (advance)
        select ((step + 1) % len);

    for (auto* c : std::initializer_list<juce::Component*> { cells.get(), screen.get(), keys.get() })
        c->repaint();
}

void Editor::commit (const Line& before, const Line& after, int step)
{
    if (! hostMode())
    {
        processor.setOwnLine (after);
        return;
    }

    auto* h = host();

    if (h == nullptr)
        return;

    // The notes that start from the note sounding before the step (its length may change: held
    // on into the step, or sliding into it) up to the step.
    auto from = step;

    for (auto* l : { &before, &after })
        if (const auto s = noteStart (*l, step - 1); s >= 0)
            from = std::min (from, s);

    const auto notes = notesOf (after, from, step + 1);
    h->setNotes (from, step + 1 - from, notes.data(), static_cast<gkmachine::int32> (notes.size()));
    pending = after;
    pendingSince = processor.link.getChangeCount();
}

void Editor::select (int step)
{
    selected = juce::jlimit (0, std::max (0, length() - 1), step);
    page = selected / pageSteps;

    for (auto* c : std::initializer_list<juce::Component*> { cells.get(), screen.get(), keys.get() })
        c->repaint();
}

void Editor::writeKey (int index)
{
    const auto base = keyFor (0), key = keyFor (index);

    edit (selected, [&] (Line& l)
    {
        auto& s = l[static_cast<size_t> (selected)];
        s.key = key;
        s.time = Step::note;
    }, true);

    // The top C belongs to the octave below: the next step is written in the same one.
    lastKey = index == 12 ? base : key;
}

void Editor::loadLine (int index)
{
    if (! juce::isPositiveAndBelow (index, static_cast<int> (factoryLines().size())))
        return;

    const auto& factory = factoryLines()[static_cast<size_t> (index)];
    const auto loaded = fromText (factory.steps);
    const auto steps = static_cast<int> (loaded.size());

    if (hostMode())
    {
        if (auto* h = host())
        {
            const auto notes = notesOf (loaded, 0, steps);
            h->loadNotes (factory.name, notes.data(), static_cast<gkmachine::int32> (notes.size()), steps);
        }
    }
    else
    {
        processor.setOwnLine (loaded);
        auto* param = processor.state.getParameter (id::length);
        param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (steps)));
    }

    select (0);
}

void Editor::clearPage()
{
    const auto from = page * pageSteps;

    if (hostMode())
    {
        if (auto* h = host())
            h->setNotes (from, pageSteps, nullptr, 0);

        return;
    }

    auto l = processor.getOwnLine();

    for (int i = from; i < std::min (from + pageSteps, static_cast<int> (l.size())); ++i)
        l[static_cast<size_t> (i)] = {};

    for (size_t i = 0; i < l.size(); ++i)
        if (l[i].time == Step::tie && (i == 0 || l[i - 1].time == Step::rest))
            l[i].time = Step::rest;

    processor.setOwnLine (l);
}

void Editor::sendLength()
{
    if (auto* h = host())
        h->setLength (juce::roundToInt (hostLength->slider.getValue()));
}

bool Editor::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const auto len = length();

    if (code == juce::KeyPress::leftKey)                   select ((selected + len - 1) % len);
    else if (code == juce::KeyPress::rightKey)             select ((selected + 1) % len);
    else if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const auto by = (code == juce::KeyPress::upKey ? 1 : -1) * (key.getModifiers().isShiftDown() ? 12 : 1);
        edit (selected, [&] (Line& l) { auto& s = l[static_cast<size_t> (selected)]; if (s.time == Step::note) s.key = juce::jlimit (0, 127, s.key + by); }, false);
    }
    else if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)   restKey.triggerClick();
    else if (key.getTextCharacter() == 'a' || key.getTextCharacter() == 'A')              accentKey.triggerClick();
    else if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')              slideKey.triggerClick();
    else if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T')              tieKey.triggerClick();
    else return false;

    return true;
}

void Editor::timerCallback()
{
    // GrooveKit showed the pattern again: the edit is in it.
    if (pending && processor.link.getChangeCount() != pendingSince)
        pending.reset();

    // While it plays, the page follows; a page picked by hand stays until it stops.
    const auto now = playStep();

    if (now < 0)
        followPage = true;
    else if (followPage && now / pageSteps != page)
        page = now / pageSteps;

    if (selected >= length())
        select (length() - 1);

    const bool host = hostMode();
    lengthKnob->setVisible (! host);
    hostLength->setVisible (host);

    if (host && ! hostLength->slider.isMouseButtonDown())
        hostLength->slider.setValue (length(), juce::dontSendNotification);

    const auto l = line();
    const auto* step = juce::isPositiveAndBelow (selected, static_cast<int> (l.size())) ? &l[static_cast<size_t> (selected)] : nullptr;
    accentKey.setToggleState (step != nullptr && step->time == Step::note && step->accent, juce::dontSendNotification);
    slideKey.setToggleState (step != nullptr && step->time == Step::note && step->slide, juce::dontSendNotification);
    previousPage.setEnabled (page > 0);
    nextPage.setEnabled (page < numPages() - 1);

    for (auto* c : std::initializer_list<juce::Component*> { display.get(), screen.get(), cells.get(), keys.get() })
        c->repaint();

    startKey->update();
}

//==============================================================================
void Editor::resized()
{
    auto bounds = getLocalBounds();
    topStrip = bounds.removeFromTop (topHeight);
    voiceArea = juce::Rectangle<int> (margin, topHeight + 12, width - 2 * margin, 150);
    modArea = juce::Rectangle<int> (margin, voiceArea.getBottom() + 10, width - 2 * margin, 124);
    bottomStrip = getLocalBounds().withTop (modArea.getBottom() + 12);

    // Along the top: the logo, the screen, the presets.
    auto top = topStrip.reduced (margin, 0);
    top.removeFromLeft (232);
    screen->setBounds (top.removeFromLeft (330).withSizeKeepingCentre (330, 60));
    top.removeFromLeft (20);
    presetArea = top.removeFromLeft (300);
    presets.setBounds (presetArea.withSizeKeepingCentre (presetArea.getWidth(), 26).translated (0, 8));
    top.removeFromLeft (14);
    masterArea = top;

    // The voice: the waveform switch, the six knobs, the display, the volume.
    auto voice = voiceArea.reduced (12, 10);
    waveformSwitch->setBounds (voice.removeFromLeft (86).withSizeKeepingCentre (86, 70));
    voice.removeFromLeft (6);
    volumeKnob->setBounds (voice.removeFromRight (96).withSizeKeepingCentre (96, 80));
    voice.removeFromRight (10);
    display->setBounds (voice.removeFromRight (300).reduced (0, 4));
    voice.removeFromRight (10);
    const auto knobWidth = voice.getWidth() / static_cast<int> (voiceKnobs.size());

    for (auto& k : voiceKnobs)
        k->setBounds (voice.removeFromLeft (knobWidth).withSizeKeepingCentre (knobWidth, 82));

    // The modifications: knobs, then switches.
    auto mods = modArea.reduced (12, 0).withTrimmedTop (22).withTrimmedBottom (10);
    auto switchArea = mods.removeFromRight (static_cast<int> (modSwitches.size()) * 92);
    const auto modWidth = mods.getWidth() / static_cast<int> (modKnobs.size());

    for (auto& k : modKnobs)
        k->setBounds (mods.removeFromLeft (modWidth).withSizeKeepingCentre (modWidth, 68));

    for (auto& s : modSwitches)
        s->setBounds (switchArea.removeFromLeft (92).withSizeKeepingCentre (92, 76));

    // Along the bottom: start, length and the page keys; the steps over the keyboard and the
    // edit keys.
    auto bottom = bottomStrip.reduced (margin, 12);
    sequencerArea = bottom.removeFromLeft (232);
    bottom.removeFromLeft (14);
    stepArea = bottom.removeFromTop (128);
    bottom.removeFromTop (8);
    editArea = bottom;

    auto seq = sequencerArea.reduced (10, 8);
    startKey->setBounds (seq.removeFromLeft (84).withTrimmedBottom (40));
    seq.removeFromLeft (10);
    auto knobRow = seq.removeFromTop (70);
    lengthKnob->setBounds (knobRow);
    hostLength->setBounds (knobRow);
    seq.removeFromTop (20);
    auto pageRow = seq.removeFromTop (26);
    previousPage.setBounds (pageRow.removeFromLeft (pageRow.getWidth() / 2).reduced (2, 0));
    nextPage.setBounds (pageRow.reduced (2, 0));
    seq.removeFromTop (8);
    clear.setBounds (seq.removeFromTop (26).reduced (2, 0));

    auto startFoot = sequencerArea.reduced (10, 8).removeFromBottom (32).removeFromLeft (84);
    lines.setBounds (startFoot.withSizeKeepingCentre (84, 26));

    cells->setBounds (stepArea);

    auto edits = editArea;
    keys->setBounds (edits.removeFromLeft (400));
    edits.removeFromLeft (20);
    const auto buttonWidth = edits.getWidth() / 4;
    auto upper = edits.removeFromTop (edits.getHeight() / 2).reduced (0, 4);
    auto lower = edits.reduced (0, 4);

    for (auto* b : { &octaveDown, &octaveUp, &accentKey, &slideKey })
        b->setBounds (upper.removeFromLeft (buttonWidth).reduced (4, 2));

    for (auto* b : { &back, &forward, &tieKey, &restKey })
        b->setBounds (lower.removeFromLeft (buttonWidth).reduced (4, 2));

    // What never changes is drawn once, at twice the size so it stays sharp on a fine screen.
    background = juce::Image (juce::Image::RGB, width * 2, height * 2, false);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (2.0f));
    paintPanel (g);
}

void Editor::paint (juce::Graphics& g)
{
    g.drawImageTransformed (background, juce::AffineTransform::scale (0.5f));
}

void Editor::paintPanel (juce::Graphics& g) const
{
    rhythm::drawPanel (g, getLocalBounds().toFloat(), style);

    // The top and bottom strips, a shade darker, with a bevel where they meet the panel.
    for (auto strip : { topStrip, bottomStrip })
    {
        g.setColour (style.strip.withAlpha (0.55f));
        g.fillRect (strip);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawHorizontalLine (strip == topStrip ? strip.getBottom() - 1 : strip.getY(), 0, static_cast<float> (width));
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine (strip == topStrip ? strip.getBottom() : strip.getY() + 1, 0, static_cast<float> (width));
    }

    g.setColour (style.accent.withAlpha (0.85f));
    g.fillRect (margin, topStrip.getBottom() + 3, 120, 2);
    g.setColour (style.accent.withAlpha (0.25f));
    g.fillRect (margin + 124, topStrip.getBottom() + 3, width - 2 * margin - 124, 1);

    rhythm::drawLogo (g, juce::Rectangle<float> (margin + 6.0f, 12, 220, 62), style);

    g.setFont (rhythm::printFont (8.5f));
    g.setColour (style.inkDim);
    g.drawText ("PRESET", presetArea.withHeight (20).translated (2, 14), juce::Justification::centredLeft);

    // The voice and the modifications, recessed, each with a dash of the machine's colour.
    for (auto [area, title] : { std::pair<juce::Rectangle<int>, const char*> { voiceArea, "VOICE" }, { modArea, "MODIFICATIONS" } })
    {
        rhythm::drawWell (g, area.toFloat(), 6, style);
        g.setColour (style.accent.withAlpha (0.55f));
        g.fillRoundedRectangle (area.getX() + 10.0f, area.getY() + 5.0f, 14, 2, 1);
        g.setColour (style.inkDim);
        g.setFont (rhythm::printFont (8.5f));
        g.drawText (title, area.withHeight (14).translated (30, 0).withY (area.getY() + 0), juce::Justification::centredLeft);
    }

    // The step section.
    rhythm::drawWell (g, sequencerArea.toFloat(), 6, style);
    g.setFont (rhythm::printFont (8.5f));
    g.setColour (style.inkDim);
    g.drawText ("STEPS", stepArea.withHeight (12).translated (0, -13), juce::Justification::centredLeft);
    g.drawText (juce::String ("CLICK A STEP  ") + dot + "  PLAY ITS NOTE  " + dot + "  DRAG A NOTE UP OR DOWN", stepArea.withHeight (12).translated (0, -13), juce::Justification::centredRight);
    g.drawText ("PAGE", previousPage.getBounds().withRight (nextPage.getRight()).withHeight (12).translated (0, -14), juce::Justification::centred);

    // Brackets under each group of four steps.
    const auto steps = stepArea.toFloat();
    const auto cell = steps.getWidth() / pageSteps;
    g.setColour (style.inkDim.withAlpha (0.5f));

    for (int group = 0; group < 4; ++group)
    {
        const auto x0 = steps.getX() + group * 4 * cell + 6, x1 = x0 + 4 * cell - 12;
        const auto y = steps.getBottom() + 2;
        g.drawLine (x0, y, x1, y, 1);
        g.drawLine (x0, y - 4, x0, y, 1);
        g.drawLine (x1, y - 4, x1, y, 1);
    }

    juce::Random random (5);

    for (auto corner : { juce::Point<float> (8, 8), { width - 8.0f, 8 }, { 8, height - 8.0f }, { width - 8.0f, height - 8.0f } })
        rhythm::drawScrew (g, corner, 4.2f, random.nextFloat() * juce::MathConstants<float>::pi);
}

} // namespace groove3
