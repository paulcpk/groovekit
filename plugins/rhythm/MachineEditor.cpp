#include "MachineEditor.h"

namespace rhythm
{

namespace
{
    constexpr int margin = 16, topHeight = 84, bottomTop = 388, plateHeight = 44;
    constexpr int levelSize = 44, knobSize = 38, pairSize = 34, smallSize = 26, labelHeight = 13;

    /** The dot between words on the panel's printing. */
    const juce::String dot = juce::String::charToString (0x00b7);
}

//==============================================================================
/** A knob with its name printed above; while it is turned (or the mouse is on it) the name
    gives way to its value. */
class MachineEditor::Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const KnobSpec& k, const Style& s, bool level)
        : attachment (state, k.paramId, slider), label (k.label), style (s), small (k.small)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
        slider.setMouseDragSensitivity (200);
        slider.setVelocityModeParameters (0.6, 1, 0.05, true, juce::ModifierKeys::shiftModifier);
        slider.getProperties().set ("level", level);
        auto* param = state.getParameter (k.paramId);
        jassert (param != nullptr);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        slider.setTooltip (param->getName (64));
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
        g.setFont (printFont (small ? 8.5f : 9.5f));
        g.setColour (showValue ? style.accent : style.ink.withAlpha (0.92f));
        g.drawText (showValue ? slider.getTextFromValue (slider.getValue()) : label.toUpperCase(), getLocalBounds().withHeight (labelHeight), juce::Justification::centred);
    }

    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    void mouseUp (const juce::MouseEvent&) override     { repaint(); }

    juce::Slider slider;

private:
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String label;
    const Style& style;
    const bool small;
};

//==============================================================================
/** An instrument's name plate: its light flashes when it plays, a click plays it. A switched
    pair shows both names, the one that plays lit, with the slide switch beside them. */
class MachineEditor::NamePlate : public juce::Component, public juce::SettableTooltipClient
{
public:
    NamePlate (MachineProcessor& m, const ColumnSpec& c, const Style& s)
        : machine (m), column (c), style (s)
    {
        if (column.switchId.isNotEmpty())
        {
            auto* param = m.state.getParameter (column.switchId);
            switchAttachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { alternate = v >= 0.5f; repaint(); });
            switchAttachment->sendInitialUpdate();
        }

        setRepaintsOnMouseActivity (true);
        setTooltip (column.names.joinIntoString (" / ") + ": click to hear it");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (3, 2);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (r.translated (0, 1), 4);
        g.setGradientFill (juce::ColourGradient (style.strip.brighter (0.16f), 0, r.getY(), style.strip.brighter (0.04f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 4);
        g.setColour (isMouseOver() ? style.accent.withAlpha (0.6f) : juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4, 0.8f);

        // The light, then the name or names.
        drawLed (g, { r.getX() + 9, r.getCentreY() }, 3.6f, style.led, flash);
        auto text = r.withTrimmedLeft (17).withTrimmedRight (column.switchId.isNotEmpty() ? 18 : 4);

        if (column.names.size() < 2)
        {
            g.setColour (style.ink);
            g.setFont (printFont (fontFor (column.names[0], text.getWidth())));
            g.drawFittedText (column.names[0].toUpperCase(), text.toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
        }
        else
        {
            for (int i = 0; i < 2; ++i)
            {
                const bool on = (i == 1) == alternate;
                const auto line = text.withHeight (text.getHeight() / 2).translated (0, i * text.getHeight() / 2);
                g.setColour (on ? style.ink : style.inkDim.withAlpha (0.55f));
                g.setFont (printFont (fontFor (column.names[i], line.getWidth()), on));
                g.drawText (column.names[i].toUpperCase(), line, juce::Justification::centredLeft);
            }

            // The slide switch: a slot with a ridged slider at the top or bottom.
            const auto slot = juce::Rectangle<float> (8, r.getHeight() - 10).withCentre ({ r.getRight() - 10, r.getCentreY() });
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillRoundedRectangle (slot, 3);
            auto knob = slot.reduced (1).withHeight (slot.getHeight() * 0.5f);

            if (alternate)
                knob = knob.withY (slot.getBottom() - knob.getHeight() - 1);

            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9d9d4), knob.getX(), knob.getY(), juce::Colour (0xff7d7f84), knob.getRight(), knob.getBottom(), false));
            g.fillRoundedRectangle (knob, 2);
            g.setColour (juce::Colours::black.withAlpha (0.35f));

            for (int k = -1; k <= 1; ++k)
                g.drawHorizontalLine (juce::roundToInt (knob.getCentreY()) + k * 2, knob.getX() + 1.5f, knob.getRight() - 1.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (switchAttachment != nullptr && e.x > getWidth() - 22)
        {
            switchAttachment->setValueAsCompleteGesture (alternate ? 0.0f : 1.0f);
            return;
        }

        held = machine.noteForRow (column.row);

        if (held >= 0)
            machine.keyboardState.noteOn (10, held, 0.82f);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (held >= 0)
            machine.keyboardState.noteOff (10, held, 0.0f);

        held = -1;
    }

    /** Called by the editor's timer: lights on a new hit, then fades. */
    void update()
    {
        const auto count = machine.hits[static_cast<size_t> (column.row)].load();

        if (count != seen)
        {
            seen = count;
            flash = 1;
            repaint();
        }
        else if (flash > 0.02f)
        {
            flash *= 0.72f;
            repaint();
        }
        else if (flash > 0)
        {
            flash = 0;
            repaint();
        }
    }

private:
    MachineProcessor& machine;
    const ColumnSpec column;
    const Style& style;
    std::unique_ptr<juce::ParameterAttachment> switchAttachment;
    bool alternate = false;
    int held = -1;
    uint32_t seen = 0;
    float flash = 0;

    static float fontFor (const juce::String& name, float space)
    {
        return name.length() * 7.2f > space ? 8.5f : 10.0f;
    }
};

//==============================================================================
/** The sixteen step keys: lit where the selected instrument plays in this pattern, a marker
    running along under the step that plays. Click a key to set or clear its step. */
class MachineEditor::StepKeys : public juce::Component
{
public:
    explicit StepKeys (MachineEditor& e) : editor (e) {}

    void paint (juce::Graphics& g) override
    {
        const auto& s = editor.spec.style;
        const auto pattern = editor.currentPattern();
        const auto row = editRow();
        const auto bits = editor.machine.patterns.getRow (pattern, row);
        const auto running = editor.machine.sequencer.currentStep.load();
        const auto last = juce::jlimit (1, numSteps, juce::roundToInt (editor.machine.value (id::lastStep)));

        for (int i = 0; i < numSteps; ++i)
        {
            const auto key = keyArea (i);
            const bool on = (bits >> i & 1) != 0;
            const bool beyond = i >= last;
            const bool now = i == running;

            // The light above the key.
            drawLed (g, { key.getCentreX(), key.getY() - 9 }, 4.2f, s.led, on ? 1.0f : now ? 0.35f : 0.0f);

            // The key: its shadow, its cap lit from above, a mark for its group of four.
            const auto pressed = on ? 1.2f : 0.0f;
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (key.translated (0, 2.5f), 5);
            auto cap = key.translated (0, pressed);
            const auto top = beyond ? s.keyBottom.darker (0.35f) : s.keyTop;
            g.setGradientFill (juce::ColourGradient (top, 0, cap.getY(), s.keyBottom.darker (beyond ? 0.4f : 0.0f), 0, cap.getBottom(), false));
            g.fillRoundedRectangle (cap, 5);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.drawHorizontalLine (juce::roundToInt (cap.getY() + 1.5f), cap.getX() + 4, cap.getRight() - 4);
            g.setColour (s.keyEdge);
            g.drawRoundedRectangle (cap.reduced (0.5f), 5, 1);

            // A gently dished top: a shade darker towards the middle.
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.07f), cap.getCentreX(), cap.getCentreY() - cap.getHeight() * 0.1f,
                                                     juce::Colours::transparentBlack, cap.getRight(), cap.getCentreY(), true));
            g.fillRoundedRectangle (cap.reduced (3), 4);

            // The group mark: one colour, every other group of four filled.
            const auto mark = cap.withTrimmedTop (cap.getHeight() - 7).reduced (5, 2);
            g.setColour ((i / 4) % 2 == 0 ? s.keyMark : s.keyMark.withAlpha (0.35f));
            g.fillRoundedRectangle (mark, 1.5f);

            // The number under it, and the running marker.
            g.setColour (now ? s.accent : s.inkDim);
            g.setFont (printFont (9.5f));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (key.getX(), key.getBottom() + 5, key.getWidth(), 12), juce::Justification::centred);

            if (now)
            {
                g.setColour (s.accent);
                g.fillRoundedRectangle (juce::Rectangle<float> (key.getWidth() * 0.5f, 2.5f).withCentre ({ key.getCentreX(), key.getBottom() + 20 }), 1.2f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < numSteps; ++i)
        {
            if (keyArea (i).expanded (2, 12).contains (e.position))
            {
                editor.machine.patterns.toggle (editor.currentPattern(), editRow(), i);
                repaint();
                return;
            }
        }
    }

    /** Repaints when the pattern, the row, its steps or the running step changed. */
    void update()
    {
        const auto pattern = editor.currentPattern();
        const auto row = editRow();
        const auto now = std::make_tuple (pattern, row, editor.machine.patterns.version.load(), editor.machine.sequencer.currentStep.load(),
                                          juce::roundToInt (editor.machine.value (id::lastStep)));

        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

private:
    MachineEditor& editor;
    std::tuple<int, int, uint32_t, int, int> shown { -1, -1, 0, -2, 0 };

    int editRow() const     { return juce::roundToInt (editor.machine.value (id::editRow)); }

    juce::Rectangle<float> keyArea (int i) const
    {
        const auto cell = static_cast<float> (getWidth()) / numSteps;
        return juce::Rectangle<float> (static_cast<float> (i) * cell + 4, 20, cell - 8, static_cast<float> (getHeight()) - 56);
    }
};

//==============================================================================
/** The instrument select: a key per row, the one being edited lit; a dot shows the rows that
    play in this pattern. */
class MachineEditor::RowSelect : public juce::Component
{
public:
    explicit RowSelect (MachineEditor& e)
        : editor (e), attachment (*e.machine.state.getParameter (id::editRow), [this] (float v) { selected = juce::roundToInt (v); repaint(); })
    {
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& s = editor.spec.style;
        const auto& rows = editor.machine.rows;
        const auto pattern = editor.currentPattern();

        for (int i = 0; i < static_cast<int> (rows.size()); ++i)
        {
            const auto r = cellArea (i).reduced (2, 1);
            const bool on = i == selected;
            const bool used = editor.machine.patterns.getRow (pattern, i) != 0;
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (r.translated (0, 1.2f), 3);
            const auto top = on ? s.accent : (i >= editor.machine.numVoiceRows ? s.strip.brighter (0.32f) : s.strip.brighter (0.2f));
            g.setGradientFill (juce::ColourGradient (top.brighter (0.1f), 0, r.getY(), top.darker (0.25f), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, 3);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 3, 0.7f);
            g.setColour (on ? s.strip : s.ink);
            g.setFont (printFont (10));
            g.drawText (rows[static_cast<size_t> (i)].shortName, r.withTrimmedLeft (used ? 6 : 0), juce::Justification::centred);

            if (used)
            {
                g.setColour (on ? s.strip : s.led);
                g.fillEllipse (juce::Rectangle<float> (4, 4).withCentre ({ r.getX() + 7, r.getCentreY() }));
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < static_cast<int> (editor.machine.rows.size()); ++i)
            if (cellArea (i).contains (e.position))
                attachment.setValueAsCompleteGesture (static_cast<float> (i));
    }

    void update()
    {
        const auto v = editor.machine.patterns.version.load() + static_cast<uint32_t> (editor.currentPattern()) * 7919u;

        if (v != shown)
        {
            shown = v;
            repaint();
        }
    }

private:
    MachineEditor& editor;
    juce::ParameterAttachment attachment;
    int selected = 0;
    uint32_t shown = 0;

    juce::Rectangle<float> cellArea (int i) const
    {
        const auto cell = static_cast<float> (getWidth()) / static_cast<float> (editor.machine.rows.size());
        return { static_cast<float> (i) * cell, 0, cell, static_cast<float> (getHeight()) };
    }
};

//==============================================================================
/** The screen: the pattern's number and name, the host's tempo and where the sequencer is. */
class MachineEditor::Screen : public juce::Component
{
public:
    explicit Screen (MachineEditor& e) : editor (e)
    {
        for (auto* b : { &down, &up })
        {
            addAndMakeVisible (*b);
            b->setTooltip (b == &down ? "Previous pattern" : "Next pattern");
        }

        down.onClick = [this] { step (-1); };
        up.onClick = [this] { step (1); };
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6).removeFromRight (22);
        up.setBounds (r.removeFromTop (r.getHeight() / 2).reduced (0, 1));
        down.setBounds (r.reduced (0, 1));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& s = editor.spec.style;
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

        auto inner = glass.reduced (10, 5).withTrimmedRight (26);
        const auto pattern = editor.currentPattern();
        const auto bright = s.accent.brighter (0.3f), dim = s.accent.withAlpha (0.45f);

        // The pattern's number, large, and its name.
        auto numberArea = inner.removeFromLeft (52);
        g.setColour (dim);
        g.setFont (printFont (8));
        g.drawText ("PATTERN", numberArea.withHeight (12), juce::Justification::topLeft);
        g.setColour (s.accent.withAlpha (0.12f));
        g.setFont (juce::Font (juce::FontOptions ("Menlo", 30, juce::Font::bold)));
        g.drawText ("88", numberArea.withTrimmedTop (10), juce::Justification::centredLeft);
        g.setColour (bright);
        g.drawText (juce::String (pattern + 1).paddedLeft ('0', 2), numberArea.withTrimmedTop (10), juce::Justification::centredLeft);

        inner.removeFromLeft (8);
        const auto name = editor.machine.patterns.getName (pattern);
        g.setFont (printFont (12.5f));
        g.setColour (bright);
        g.drawText (name.isEmpty() ? juce::String ("Pattern ") + juce::String (pattern + 1) : name, inner.withHeight (inner.getHeight() * 0.5f), juce::Justification::centredLeft);

        // The tempo from the host and the sequencer's state.
        const auto tempo = editor.machine.hostTempo.load();
        const bool seq = editor.machine.value (id::seq) >= 0.5f;
        const bool playing = editor.machine.hostPlaying.load();
        const auto stepNow = editor.machine.sequencer.currentStep.load();
        juce::String status;

        if (! seq)
            status = "SEQ OFF  " + dot + "  PLAYED BY MIDI";
        else if (! playing)
            status = "SEQ ON  " + dot + "  WAITING FOR THE HOST";
        else
            status = "STEP " + juce::String (stepNow + 1).paddedLeft ('0', 2) + " / " + juce::String (juce::roundToInt (editor.machine.value (id::lastStep)));

        auto lower = inner.withTrimmedTop (inner.getHeight() * 0.5f);
        g.setFont (printFont (9));
        g.setColour (dim.withAlpha (0.8f));
        g.drawText (status, lower, juce::Justification::centredLeft);
        g.setColour (bright);
        g.setFont (juce::Font (juce::FontOptions ("Menlo", 13, juce::Font::bold)));
        g.drawText (tempo > 0 ? juce::String (tempo, 1) : juce::String ("---.-"), lower.withTrimmedRight (26), juce::Justification::centredRight);
        g.setFont (printFont (8));
        g.setColour (dim);
        g.drawText ("BPM", lower, juce::Justification::centredRight);
    }

    void update()
    {
        const auto now = std::make_tuple (editor.currentPattern(), editor.machine.hostTempo.load(), editor.machine.hostPlaying.load(),
                                          editor.machine.sequencer.currentStep.load(), editor.machine.value (id::seq) >= 0.5f, editor.machine.patterns.version.load());

        if (now != shown)
        {
            shown = now;
            repaint();
        }
    }

private:
    MachineEditor& editor;
    juce::TextButton down { "-" }, up { "+" };
    std::tuple<int, double, bool, int, bool, uint32_t> shown { -1, 0.0, false, -2, false, 0 };

    void step (int delta)
    {
        auto* param = editor.machine.state.getParameter (id::pattern);
        const auto next = (editor.currentPattern() + delta + numPatterns) % numPatterns;
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (next)));
        param->endChangeGesture();
    }
};

//==============================================================================
/** Start and stop: a big key that switches the sequencer on and off, its light beating with the
    quarters while it runs. */
class MachineEditor::StartKey : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StartKey (MachineEditor& e)
        : editor (e), attachment (*e.machine.state.getParameter (id::seq), [this] (float v) { on = v >= 0.5f; repaint(); })
    {
        attachment.sendInitialUpdate();
        setTooltip ("Start and stop the sequencer (it plays while the host plays)");
    }

    void paint (juce::Graphics& g) override
    {
        const auto& s = editor.spec.style;
        auto key = getLocalBounds().toFloat().reduced (2).withTrimmedTop (18);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (key.translated (0, 2.5f), 6);
        key = key.translated (0, on ? 1.2f : 0);
        const auto top = on ? s.accent : s.keyTop;
        g.setGradientFill (juce::ColourGradient (top.brighter (0.1f), 0, key.getY(), on ? s.accent.darker (0.3f) : s.keyBottom, 0, key.getBottom(), false));
        g.fillRoundedRectangle (key, 6);
        g.setColour (s.keyEdge);
        g.drawRoundedRectangle (key.reduced (0.5f), 6, 1);
        g.setColour (on ? s.strip : s.keyInk);
        g.setFont (printFont (11));
        g.drawText ("START", key.withHeight (key.getHeight() * 0.5f).translated (0, 6), juce::Justification::centred);
        g.setFont (printFont (9, false));
        g.drawText ("STOP", key.withTrimmedTop (key.getHeight() * 0.5f).translated (0, -4), juce::Justification::centred);

        const auto stepNow = editor.machine.sequencer.currentStep.load();
        const auto beat = on && editor.machine.hostPlaying.load() && stepNow >= 0 && stepNow % 4 == 0;
        drawLed (g, { getWidth() / 2.0f, 8 }, 4.5f, s.led, on ? (beat ? 1.0f : 0.45f) : 0.0f);
    }

    void mouseDown (const juce::MouseEvent&) override   { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

    void update()
    {
        const auto stepNow = editor.machine.sequencer.currentStep.load();

        if (stepNow != shownStep)
        {
            shownStep = stepNow;
            repaint();
        }
    }

private:
    MachineEditor& editor;
    juce::ParameterAttachment attachment;
    bool on = false;
    int shownStep = -2;
};

//==============================================================================
MachineEditor::MachineEditor (MachineProcessor& p, PanelSpec panel)
    : AudioProcessorEditor (p), machine (p), spec (std::move (panel)), look (spec.style), theme (themeFor (spec.style)), presets (p, theme)
{
    setLookAndFeel (&look);
    addAndMakeVisible (presets);

    for (auto& column : spec.columns)
    {
        auto& widgets = columnWidgets.emplace_back();

        for (size_t r = 0; r < column.knobs.size(); ++r)
        {
            auto& row = widgets.knobs.emplace_back();

            for (auto& k : column.knobs[r])
                row.push_back (addKnob (k, r == 0));
        }

        for (auto& k : column.smallKnobs)
            widgets.small.push_back (addKnob (k, false));

        auto plate = std::make_unique<NamePlate> (machine, column, spec.style);
        widgets.plate = plate.get();
        addAndMakeVisible (*plate);
        owned.push_back (std::move (plate));
    }

    for (auto& k : spec.master)
        addAndMakeVisible (*masterKnobs.emplace_back (std::make_unique<Knob> (machine.state, k, spec.style, ! k.small)));

    std::vector<KnobSpec> sequencerSpecs { { id::lastStep, "Last Step", true }, { id::scale, "Scale", true }, { id::shuffle, "Shuffle", true } };

    if (machine.flamRow >= 0)
        sequencerSpecs.push_back ({ id::flam, "Flam", true });

    for (auto& k : sequencerSpecs)
        addAndMakeVisible (*sequencerKnobs.emplace_back (std::make_unique<Knob> (machine.state, k, spec.style, false)));

    stepKeys = std::make_unique<StepKeys> (*this);
    rowSelect = std::make_unique<RowSelect> (*this);
    screen = std::make_unique<Screen> (*this);
    startKey = std::make_unique<StartKey> (*this);

    for (auto* c : std::initializer_list<juce::Component*> { stepKeys.get(), rowSelect.get(), screen.get(), startKey.get(), &clear, &copy, &paste })
        addAndMakeVisible (c);

    clear.setTooltip ("Clear this pattern");
    copy.setTooltip ("Copy this pattern");
    paste.setTooltip ("Paste the copied pattern here");
    clear.onClick = [this] { machine.patterns.clear (currentPattern()); };
    copy.onClick = [this]
    {
        for (int r = 0; r < maxRows; ++r)
            clipboard[static_cast<size_t> (r)] = machine.patterns.getRow (currentPattern(), r);

        clipboardName = machine.patterns.getName (currentPattern());
        hasClipboard = true;
        paste.setEnabled (true);
    };
    paste.onClick = [this]
    {
        if (! hasClipboard)
            return;

        for (int r = 0; r < maxRows; ++r)
            machine.patterns.setRow (currentPattern(), r, clipboard[static_cast<size_t> (r)]);

        machine.patterns.setName (currentPattern(), clipboardName);
    };
    paste.setEnabled (false);

    setSize (width, height);
    startTimerHz (30);
}

MachineEditor::~MachineEditor()
{
    setLookAndFeel (nullptr);
}

MachineEditor::Knob* MachineEditor::addKnob (const KnobSpec& k, bool level)
{
    auto knob = std::make_unique<Knob> (machine.state, k, spec.style, level);
    auto* raw = knob.get();
    addAndMakeVisible (*raw);
    owned.push_back (std::move (knob));
    return raw;
}

int MachineEditor::currentPattern() const
{
    return juce::jlimit (0, numPatterns - 1, juce::roundToInt (machine.value (id::pattern)));
}

void MachineEditor::timerCallback()
{
    for (auto& widgets : columnWidgets)
        widgets.plate->update();

    stepKeys->update();
    rowSelect->update();
    screen->update();
    startKey->update();
}

void MachineEditor::resized()
{
    auto bounds = getLocalBounds();
    topStrip = bounds.removeFromTop (topHeight);
    bottomStrip = bounds.withTop (bottomTop);
    instrumentArea = juce::Rectangle<int> (margin, topHeight + 10, width - 2 * margin, bottomTop - topHeight - 20);

    // Along the top: the logo, the screen, the presets, the master knobs.
    auto top = topStrip.reduced (margin, 0);
    top.removeFromLeft (232);
    screen->setBounds (top.removeFromLeft (276).withSizeKeepingCentre (276, 58));
    top.removeFromLeft (20);
    presetArea = top.removeFromLeft (250);
    presets.setBounds (presetArea.withSizeKeepingCentre (presetArea.getWidth(), 26).translated (0, 8));
    top.removeFromLeft (14);
    masterArea = top;
    auto master = masterArea.reduced (8, 6);
    const auto masterWidth = master.getWidth() / std::max (1, static_cast<int> (masterKnobs.size()));

    for (size_t i = 0; i < masterKnobs.size(); ++i)
    {
        const auto cell = master.removeFromLeft (masterWidth);
        const bool small = spec.master[i].small;
        const auto size = small ? smallSize : levelSize - 6;
        masterKnobs[i]->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), size + labelHeight).withY (cell.getY() + (small ? 24 : 12)));
    }

    // The instruments: a column each, their knobs in rows, their names at the foot.
    columnAreas.clear();
    const auto columns = static_cast<int> (spec.columns.size());
    const auto columnWidth = instrumentArea.getWidth() / std::max (1, columns);

    for (int c = 0; c < columns; ++c)
    {
        const auto& widgets = columnWidgets[static_cast<size_t> (c)];
        auto area = juce::Rectangle<int> (instrumentArea.getX() + c * columnWidth, instrumentArea.getY(), columnWidth, instrumentArea.getHeight()).reduced (3, 0);
        columnAreas.push_back (area);
        auto inner = area.reduced (4, 8);
        widgets.plate->setBounds (inner.removeFromBottom (plateHeight));
        inner.removeFromBottom (4);
        auto smallRow = inner.removeFromBottom (smallSize + labelHeight + 2);
        auto y = inner.getY();

        for (size_t r = 0; r < widgets.knobs.size(); ++r)
        {
            const auto& row = widgets.knobs[r];
            const auto size = r == 0 ? levelSize : row.size() > 1 ? pairSize : knobSize;
            const auto h = size + labelHeight + 2;
            auto line = juce::Rectangle<int> (inner.getX(), y, inner.getWidth(), h);
            const auto w = line.getWidth() / std::max (1, static_cast<int> (row.size()));

            for (auto* knob : row)
                knob->setBounds (line.removeFromLeft (w));

            y += h + 5;
        }

        const auto smallWidth = smallRow.getWidth() / std::max (1, static_cast<int> (widgets.small.size()));

        for (auto* knob : widgets.small)
            knob->setBounds (smallRow.removeFromLeft (smallWidth));
    }

    // Along the bottom: start, the sequencer's knobs and the pattern's keys, then the steps.
    auto bottom = bottomStrip.reduced (margin, 10);
    sequencerArea = bottom.removeFromLeft (248);
    bottom.removeFromLeft (14);
    stepArea = bottom;

    auto seq = sequencerArea.reduced (8, 6);
    startKey->setBounds (seq.removeFromLeft (84).withTrimmedBottom (30));
    seq.removeFromLeft (8);
    auto buttons = seq.removeFromBottom (24);
    const auto bw = buttons.getWidth() / 3;
    clear.setBounds (buttons.removeFromLeft (bw).reduced (2, 0));
    copy.setBounds (buttons.removeFromLeft (bw).reduced (2, 0));
    paste.setBounds (buttons.reduced (2, 0));
    seq.removeFromBottom (4);
    const auto knobsPerRow = 2;
    const auto cellW = seq.getWidth() / knobsPerRow;
    const auto cellH = seq.getHeight() / 2;

    for (size_t i = 0; i < sequencerKnobs.size(); ++i)
    {
        const auto col = static_cast<int> (i) % knobsPerRow, row = static_cast<int> (i) / knobsPerRow;
        sequencerKnobs[i]->setBounds (seq.getX() + col * cellW, seq.getY() + row * cellH, cellW, cellH);
    }

    auto steps = stepArea;
    rowSelect->setBounds (steps.removeFromTop (24));
    steps.removeFromTop (2);
    stepKeys->setBounds (steps);

    // What never changes is drawn once, at twice the size so it stays sharp on a fine screen.
    background = juce::Image (juce::Image::RGB, width * 2, height * 2, false);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (2.0f));
    paintPanel (g);
}

//==============================================================================
void MachineEditor::paint (juce::Graphics& g)
{
    g.drawImageTransformed (background, juce::AffineTransform::scale (0.5f));
}

void MachineEditor::paintPanel (juce::Graphics& g) const
{
    const auto& s = spec.style;
    drawPanel (g, getLocalBounds().toFloat(), s);

    // The top and bottom strips, a shade darker, with a bevel where they meet the panel.
    for (auto strip : { topStrip, bottomStrip })
    {
        g.setColour (s.strip.withAlpha (0.55f));
        g.fillRect (strip);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawHorizontalLine (strip == topStrip ? strip.getBottom() - 1 : strip.getY(), 0, static_cast<float> (width));
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine (strip == topStrip ? strip.getBottom() : strip.getY() + 1, 0, static_cast<float> (width));
    }

    // The accent line under the top strip, the machine's colour.
    g.setColour (s.accent.withAlpha (0.85f));
    g.fillRect (margin, topStrip.getBottom() + 3, 120, 2);
    g.setColour (s.accent.withAlpha (0.25f));
    g.fillRect (margin + 124, topStrip.getBottom() + 3, width - 2 * margin - 124, 1);

    drawLogo (g, juce::Rectangle<float> (margin + 6.0f, 12, 220, 62), s);

    // Printed labels for the top strip's sections.
    g.setFont (printFont (8.5f));
    g.setColour (s.inkDim);
    g.drawText ("PRESET", presetArea.withHeight (20).translated (2, 14), juce::Justification::centredLeft);
    g.drawText ("MASTER", masterArea.withHeight (20).translated (8, 4), juce::Justification::centredLeft);

    // The instruments' columns, recessed, each with a dash of the machine's colour.
    for (size_t c = 0; c < columnAreas.size(); ++c)
    {
        const auto area = columnAreas[c].toFloat();
        drawWell (g, area, 6, s);
        g.setColour (s.accent.withAlpha (0.55f));
        g.fillRoundedRectangle (area.getX() + 8, area.getY() + 4, 14, 2, 1);
    }

    // The sequencer's section and the step section's printing.
    drawWell (g, sequencerArea.toFloat(), 6, s);
    g.setFont (printFont (8.5f));
    g.setColour (s.inkDim);
    g.drawText ("INSTRUMENT SELECT", stepArea.withHeight (12).translated (0, -13), juce::Justification::centredLeft);
    g.drawText ("STEP", stepArea.withHeight (12).translated (0, -13), juce::Justification::centredRight);

    // Brackets under each group of four steps.
    const auto keys = stepKeys->getBounds().toFloat();
    const auto cell = keys.getWidth() / numSteps;
    g.setColour (s.inkDim.withAlpha (0.5f));

    for (int group = 0; group < 4; ++group)
    {
        const auto x0 = keys.getX() + group * 4 * cell + 6, x1 = x0 + 4 * cell - 12;
        const auto y = keys.getBottom() - 6;
        g.drawLine (x0, y, x1, y, 1);
        g.drawLine (x0, y - 4, x0, y, 1);
        g.drawLine (x1, y - 4, x1, y, 1);
    }

    // Screws in the corners.
    juce::Random random (3);

    for (auto corner : { juce::Point<float> (8, 8), { width - 8.0f, 8 }, { 8, height - 8.0f }, { width - 8.0f, height - 8.0f } })
        drawScrew (g, corner, 4.2f, random.nextFloat() * juce::MathConstants<float>::pi);
}

} // namespace rhythm
