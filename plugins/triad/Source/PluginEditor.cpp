#include "PluginEditor.h"

namespace triad
{

namespace
{
    constexpr int width = 940, height = 700;

    juce::Font font (float size, bool bold = false)
    {
        return juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain);
    }
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, colours::control);
    setColour (juce::ComboBox::textColourId, colours::fg);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId, colours::dim);
    setColour (juce::PopupMenu::backgroundColourId, colours::raised);
    setColour (juce::PopupMenu::textColourId, colours::fg);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId, colours::control);
    setColour (juce::TextButton::textColourOffId, colours::dim);
    setColour (juce::TextButton::textColourOnId, colours::fg);
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

    g.setColour (colours::raised.brighter (0.05f));
    g.fillEllipse (juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (centre));
    const auto tip = centre.getPointOnCircumference (radius - 5, angle);
    g.setColour (colours::fg);
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.2f, angle), tip }, 1.8f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (0, 0, static_cast<float> (w), static_cast<float> (h), 4);
    juce::Path arrow;
    arrow.addTriangle (static_cast<float> (w - 18), h / 2.0f - 2, static_cast<float> (w - 10), h / 2.0f - 2, w - 14.0f, h / 2.0f + 3);
    g.setColour (colours::dim);
    g.fillPath (arrow);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& n, juce::Colour colour, bool bipolar)
    : attachment (state, paramId, slider), name (n)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (180);
    slider.setVelocityModeParameters (0.6, 1, 0.05, true, juce::ModifierKeys::shiftModifier);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setDoubleClickReturnValue (true, state.getParameter (paramId)->convertFrom0to1 (state.getParameter (paramId)->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    slider.setTooltip (state.getParameter (paramId)->getName (64));
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
    g.setColour (colours::dim);
    g.drawText (name, text.removeFromTop (13), juce::Justification::centred);
    g.setFont (font (10));
    g.setColour (slider.isMouseButtonDown() ? colours::fg : colours::faint);
    g.drawText (slider.getTextFromValue (slider.getValue()), text.removeFromTop (12), juce::Justification::centred);
}

//==============================================================================
Choice::Choice (juce::RangedAudioParameter& p, juce::StringArray l, juce::Colour c, bool s)
    : labels (std::move (l)), colour (c), shapes (s),
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
    g.setColour (colours::control);
    g.fillRoundedRectangle (bounds, 4);
    const auto cell = bounds.getWidth() / labels.size();
    juce::Random random (3);

    for (int i = 0; i < labels.size(); ++i)
    {
        const auto r = juce::Rectangle<float> (bounds.getX() + i * cell, bounds.getY(), cell, bounds.getHeight()).reduced (2);
        const bool on = i == selected;

        if (on)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (r, 3);
        }

        g.setColour (on ? juce::Colours::black : colours::dim);

        if (shapes)
        {
            // A cycle and a half of the shape.
            juce::Path wave;
            const auto area = r.reduced (5, 6);

            for (int k = 0; k <= 40; ++k)
            {
                const auto t = k / 40.0;
                const auto v = Synth::waveform (i, t * 1.5, 0.0, random);
                const auto p = juce::Point<float> (area.getX() + static_cast<float> (t) * area.getWidth(), area.getCentreY() - v * area.getHeight() / 2);
                k == 0 ? wave.startNewSubPath (p) : wave.lineTo (p);
            }

            g.strokePath (wave, juce::PathStrokeType (1.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setFont (font (11, on));
            g.drawText (labels[i], r, juce::Justification::centred);
        }
    }
}

//==============================================================================
/** One oscillator: its waveform as it sounds (moving while notes play), shape, tuning, level,
    pan, phase, stereo detune and, for the second and third, what they do to the one above. */
struct Editor::OscRow : public juce::Component
{
    OscRow (Editor& e, int n) : editor (e), number (n), colour (colours::osc[n - 1])
    {
        auto& state = editor.owner.state;
        shape = std::make_unique<Choice> (*state.getParameter (id::osc (n, "Shape")), shapeNames, colour, true);
        addAndMakeVisible (*shape);

        const std::pair<const char*, const char*> controls[] { { "Coarse", "Coarse" }, { "Fine", "Fine" }, { "Level", "Level" },
                                                               { "Pan", "Pan" }, { "Phase", "Phase" }, { "Detune", "Stereo" } };

        for (auto [suffix, name] : controls)
        {
            const auto paramId = id::osc (n, suffix);
            const bool bipolar = juce::String (suffix) == "Coarse" || juce::String (suffix) == "Fine" || juce::String (suffix) == "Pan";
            addAndMakeVisible (*knobs.emplace_back (std::make_unique<Knob> (state, paramId, name, colour, bipolar)));
        }

        if (n > 1)
        {
            mod = std::make_unique<Choice> (*state.getParameter (id::osc (n, "Mod")), modNames, colour);
            addAndMakeVisible (*mod);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        r.removeFromLeft (28);
        wave = r.removeFromLeft (170).reduced (0, 4);
        r.removeFromLeft (12);
        auto shapeArea = r.removeFromLeft (196);
        shape->setBounds (shapeArea.withSizeKeepingCentre (196, 26).translated (0, number > 1 ? -14 : 0));

        if (mod != nullptr)
            mod->setBounds (shapeArea.withSizeKeepingCentre (150, 22).translated (0, 22));

        r.removeFromLeft (12);

        const auto w = r.getWidth() / static_cast<int> (knobs.size());

        for (auto& k : knobs)
            k->setBounds (r.removeFromLeft (w));
    }

    void paint (juce::Graphics& g) override
    {
        auto& state = editor.owner.state;
        const auto level = state.getRawParameterValue (id::osc (number, "Level"))->load();
        const auto modMode = number > 1 ? juce::roundToInt (state.getRawParameterValue (id::osc (number, "Mod"))->load()) : mix;

        g.setColour (colours::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8);
        g.setColour (colour);
        g.fillRoundedRectangle (juce::Rectangle<float> (12, getHeight() / 2.0f - 13, 24, 26), 5);
        g.setColour (juce::Colours::black);
        g.setFont (font (14, true));
        g.drawText (juce::String (number), juce::Rectangle<int> (12, getHeight() / 2 - 13, 24, 26), juce::Justification::centred);

        if (mod != nullptr)
        {
            g.setColour (colours::faint);
            g.setFont (font (10));
            g.drawText ("into osc " + juce::String (number - 1), mod->getBounds().translated (0, -15).withHeight (14), juce::Justification::centred);
        }

        // The wave on a little screen, drifting while notes play.
        const auto area = wave.toFloat();
        g.setColour (colours::screen);
        g.fillRoundedRectangle (area, 5);
        g.setColour (colours::line);
        g.drawHorizontalLine (static_cast<int> (area.getCentreY()), area.getX() + 4, area.getRight() - 4);

        const auto shapeIndex = juce::roundToInt (state.getRawParameterValue (id::osc (number, "Shape"))->load());
        const auto phase = state.getRawParameterValue (id::osc (number, "Phase"))->load();
        const auto inner = area.reduced (6, 8);
        juce::Path path;
        juce::Random random (static_cast<juce::int64> (editor.animation * 30));

        for (int k = 0; k <= 160; ++k)
        {
            const auto t = k / 160.0;
            const auto v = Synth::waveform (shapeIndex, t * 2 + phase + editor.animation * (number * 0.37), 0.0, random) * std::max (0.08f, level);
            const auto p = juce::Point<float> (inner.getX() + static_cast<float> (t) * inner.getWidth(), inner.getCentreY() - v * inner.getHeight() / 2);
            k == 0 ? path.startNewSubPath (p) : path.lineTo (p);
        }

        const auto heard = modMode == mix && level > 0;
        g.setColour (colour.withAlpha (heard ? 0.25f : 0.1f));
        g.strokePath (path, juce::PathStrokeType (5, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (heard ? colour : colour.withAlpha (0.5f));
        g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (font (10));
        g.setColour (colours::faint);
        g.drawText (modMode == fm ? "modulating (FM)" : modMode == ring ? "modulating (ring)" : shapeNames[shapeIndex],
                    area.reduced (6, 3).toNearestInt(), juce::Justification::topLeft);
    }

    Editor& editor;
    const int number;
    const juce::Colour colour;
    std::unique_ptr<Choice> shape, mod;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::Rectangle<int> wave;
};

//==============================================================================
Editor::Editor (Processor& p) : AudioProcessorEditor (p), owner (p)
{
    setLookAndFeel (&look);

    for (int i = 0; i < owner.getNumPrograms(); ++i)
        presets.addItem (owner.getProgramName (i), i + 1);

    presets.setSelectedId (owner.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.onChange = [this] { owner.setCurrentProgram (presets.getSelectedId() - 1); };
    previous.onClick = [this] { presets.setSelectedId ((presets.getSelectedId() + presets.getNumItems() - 2) % presets.getNumItems() + 1); };
    next.onClick = [this] { presets.setSelectedId (presets.getSelectedId() % presets.getNumItems() + 1); };
    addAndMakeVisible (presets);
    addAndMakeVisible (previous);
    addAndMakeVisible (next);

    volume = std::make_unique<Knob> (owner.state, id::gain, "Volume", colours::accent);
    addAndMakeVisible (*volume);

    for (int n = 1; n <= 3; ++n)
        addAndMakeVisible (*oscillators.emplace_back (std::make_unique<OscRow> (*this, n)));

    auto& state = owner.state;
    filterType = std::make_unique<Choice> (*state.getParameter (id::filterType), juce::StringArray { "Off", "LP24", "LP12", "HP", "BP" }, colours::accent);
    lfoShape = std::make_unique<Choice> (*state.getParameter (id::lfoShape), juce::StringArray { "Sin", "Tri", "Sqr", "Saw", "Rnd" }, colours::osc[2]);
    lfoTarget = std::make_unique<Choice> (*state.getParameter (id::lfoTarget), juce::StringArray { "Off", "Pitch", "Cut", "Vol", "Pan" }, colours::osc[2]);
    voiceMode = std::make_unique<Choice> (*state.getParameter (id::voiceMode), voiceModeNames, colours::accent);

    for (auto* c : { filterType.get(), lfoShape.get(), lfoTarget.get(), voiceMode.get() })
        addAndMakeVisible (*c);

    filterKnobs = { knob (id::cutoff, "Cutoff", colours::accent), knob (id::resonance, "Resonance", colours::accent),
                    knob (id::filterEnv, "Envelope", colours::accent, true), knob (id::keyTrack, "Key", colours::accent) };
    ampKnobs = { knob (id::ampAttack, "Attack", colours::osc[1]), knob (id::ampDecay, "Decay", colours::osc[1]),
                 knob (id::ampSustain, "Sustain", colours::osc[1]), knob (id::ampRelease, "Release", colours::osc[1]) };
    filterEnvKnobs = { knob (id::filterAttack, "Attack", colours::accent), knob (id::filterDecay, "Decay", colours::accent),
                       knob (id::filterSustain, "Sustain", colours::accent), knob (id::filterRelease, "Release", colours::accent) };
    lfoKnobs = { knob (id::lfoRate, "Rate", colours::osc[2]), knob (id::lfoDepth, "Depth", colours::osc[2]) };
    voiceKnobs = { knob (id::glide, "Glide", colours::accent), knob (id::unisonVoices, "Unison", colours::accent),
                   knob (id::unisonDetune, "Detune", colours::accent), knob (id::unisonSpread, "Spread", colours::accent),
                   knob (id::velocity, "Velocity", colours::accent), knob (id::bendRange, "Bend", colours::accent) };

    setSize (width, height);
    startTimerHz (30);
}

Editor::~Editor()
{
    setLookAndFeel (nullptr);
}

Knob* Editor::knob (const juce::String& paramId, const juce::String& name, juce::Colour colour, bool bipolar)
{
    auto k = std::make_unique<Knob> (owner.state, paramId, name, colour, bipolar);
    auto* raw = k.get();
    addAndMakeVisible (*raw);
    owned.push_back (std::move (k));
    return raw;
}

void Editor::timerCallback()
{
    owner.readScope (scope.data(), static_cast<int> (scope.size()));

    // The oscillator pictures drift while something plays.
    if (owner.activeVoices > 0)
        animation += 1.0 / 30.0 * 0.4;

    if (presets.getSelectedId() != owner.getCurrentProgram() + 1)
        presets.setSelectedId (owner.getCurrentProgram() + 1, juce::dontSendNotification);

    repaint();
}

void Editor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto header = r.removeFromTop (48);
    header.removeFromLeft (180);
    volume->setBounds (header.removeFromRight (54).withTrimmedTop (-2));
    header.removeFromRight (10);
    scopeArea = header.removeFromRight (230).reduced (0, 4);
    header.removeFromRight (16);
    auto presetRow = header.withSizeKeepingCentre (std::min (320, header.getWidth()), 28);
    previous.setBounds (presetRow.removeFromLeft (28));
    next.setBounds (presetRow.removeFromRight (28));
    presets.setBounds (presetRow.reduced (4, 0));

    r.removeFromTop (8);

    for (auto& o : oscillators)
    {
        o->setBounds (r.removeFromTop (96));
        r.removeFromTop (8);
    }

    auto voice = r.removeFromBottom (78);
    r.removeFromBottom (8);

    auto columns = r;
    filterArea = columns.removeFromLeft (300);
    columns.removeFromLeft (8);
    ampArea = columns.removeFromLeft (200);
    columns.removeFromLeft (8);
    filterEnvArea = columns.removeFromLeft (200);
    columns.removeFromLeft (8);
    lfoArea = columns;
    voiceArea = voice;

    auto layoutKnobs = [] (juce::Rectangle<int> area, const std::vector<Knob*>& knobs)
    {
        const auto w = area.getWidth() / static_cast<int> (knobs.size());

        for (auto* k : knobs)
            k->setBounds (area.removeFromLeft (w));
    };

    auto f = filterArea.reduced (10).withTrimmedTop (22);
    filterType->setBounds (f.removeFromTop (22));
    f.removeFromTop (6);
    layoutKnobs (f.removeFromBottom (62), filterKnobs);
    filterGraph = f.reduced (0, 4);

    auto a = ampArea.reduced (10).withTrimmedTop (22);
    layoutKnobs (a.removeFromBottom (62), ampKnobs);
    ampGraph = a.reduced (0, 4);

    auto e = filterEnvArea.reduced (10).withTrimmedTop (22);
    layoutKnobs (e.removeFromBottom (62), filterEnvKnobs);
    filterEnvGraph = e.reduced (0, 4);

    auto l = lfoArea.reduced (10).withTrimmedTop (22);
    lfoShape->setBounds (l.removeFromTop (22));
    l.removeFromTop (6);
    lfoTarget->setBounds (l.removeFromTop (22));
    layoutKnobs (l.removeFromBottom (62), lfoKnobs);

    auto v = voiceArea.reduced (10, 8).withTrimmedLeft (60);
    voiceMode->setBounds (v.removeFromLeft (180).withSizeKeepingCentre (180, 24));
    v.removeFromLeft (16);
    layoutKnobs (v.withTrimmedTop (0), voiceKnobs);
}

//==============================================================================
void Editor::drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, juce::Colour colour) const
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (area.toFloat(), 8);
    g.setColour (colour);
    g.fillRoundedRectangle (area.toFloat().withSize (3, 12).translated (10, 11), 1.5f);
    g.setColour (colours::fg);
    g.setFont (font (12, true));
    g.drawText (title, area.withTrimmedLeft (20).withHeight (34), juce::Justification::centredLeft);
}

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);

    // The name, with the three oscillator colours.
    auto logo = juce::Rectangle<int> (12, 12, 180, 48);
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (colours::osc[i]);
        g.fillRoundedRectangle (static_cast<float> (logo.getX() + i * 9), logo.getCentreY() - 9.0f + (i == 1 ? -4 : 0), 6, 18 + (i == 1 ? 8 : 0), 3);
    }

    g.setColour (colours::fg);
    g.setFont (font (20, true));
    g.drawText ("TRIAD", logo.withTrimmedLeft (36).withTrimmedBottom (14), juce::Justification::bottomLeft);
    g.setColour (colours::faint);
    g.setFont (font (10));
    g.drawText ("three-oscillator synth", logo.withTrimmedLeft (36).withTrimmedTop (30), juce::Justification::topLeft);

    drawScope (g, scopeArea.toFloat());

    drawPanel (g, filterArea, "Filter", colours::accent);
    drawFilter (g, filterGraph.toFloat());

    drawPanel (g, ampArea, "Amp envelope", colours::osc[1]);
    const juce::String ampIds[4] { id::ampAttack, id::ampDecay, id::ampSustain, id::ampRelease };
    drawEnvelope (g, ampGraph.toFloat(), ampIds, colours::osc[1]);

    drawPanel (g, filterEnvArea, "Filter envelope", colours::accent);
    const juce::String filterIds[4] { id::filterAttack, id::filterDecay, id::filterSustain, id::filterRelease };
    drawEnvelope (g, filterEnvGraph.toFloat(), filterIds, colours::accent);

    drawPanel (g, lfoArea, "LFO", colours::osc[2]);
    drawPanel (g, voiceArea, "Voice", colours::accent);
}

void Editor::drawScope (juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour (colours::screen);
    g.fillRoundedRectangle (area, 5);

    // From a rising zero crossing, so a steady note stands still.
    size_t start = 0;
    for (size_t i = 1; i < scope.size() / 2; ++i)
        if (scope[i - 1] <= 0 && scope[i] > 0) { start = i; break; }

    const auto n = scope.size() / 2;
    float peak = 0.05f;
    for (size_t i = 0; i < n; ++i)
        peak = std::max (peak, std::abs (scope[start + i]));

    juce::Path path;
    const auto inner = area.reduced (6, 5);

    for (size_t i = 0; i < n; ++i)
    {
        const auto p = juce::Point<float> (inner.getX() + inner.getWidth() * static_cast<float> (i) / static_cast<float> (n - 1),
                                           inner.getCentreY() - scope[start + i] / peak * inner.getHeight() / 2);
        i == 0 ? path.startNewSubPath (p) : path.lineTo (p);
    }

    juce::ColourGradient gradient (colours::osc[0], inner.getX(), 0, colours::osc[2], inner.getRight(), 0, false);
    gradient.addColour (0.5, colours::osc[1]);
    g.setGradientFill (gradient);
    g.strokePath (path, juce::PathStrokeType (1.5f));

    g.setColour (colours::faint);
    g.setFont (font (9));
    const auto voices = owner.activeVoices.load();
    g.drawText (voices > 0 ? juce::String (voices) + (voices == 1 ? " voice" : " voices") : "silent", area.reduced (6, 3).toNearestInt(), juce::Justification::topRight);
}

void Editor::drawFilter (juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour (colours::screen);
    g.fillRoundedRectangle (area, 5);

    const auto type = juce::roundToInt (get (id::filterType));
    const auto cutoff = get (id::cutoff);
    const auto k = 2.0f - 1.96f * get (id::resonance);
    const auto xOf = [&] (float hz) { return area.getX() + area.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    const auto yOf = [&] (float db) { return area.getY() + area.getHeight() * (18 - juce::jlimit (-36.0f, 18.0f, db)) / 54.0f; };

    g.setColour (colours::line.withAlpha (0.6f));
    for (auto hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), area.getY() + 2, area.getBottom() - 2);
    g.drawHorizontalLine (juce::roundToInt (yOf (0)), area.getX() + 2, area.getRight() - 2);

    // The state-variable filter's response, analytically.
    juce::Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const auto hz = 20.0f * std::pow (1000.0f, i / 120.0f);
        const auto w = hz / cutoff;
        const auto denominator = std::sqrt ((1 - w * w) * (1 - w * w) + (k * w) * (k * w));
        float gain = 1;

        switch (type)
        {
            case lowpass24: gain = 1 / (denominator * denominator); break;
            case lowpass12: gain = 1 / denominator; break;
            case highpass:  gain = w * w / denominator; break;
            case bandpass:  gain = k * w / denominator; break;
            default:        break;
        }

        const auto p = juce::Point<float> (xOf (hz), yOf (juce::Decibels::gainToDecibels (gain, -60.0f)));
        i == 0 ? curve.startNewSubPath (p) : curve.lineTo (p);
    }

    juce::Path fill (curve);
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.3f), 0, area.getY(), colours::accent.withAlpha (0.02f), 0, area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (type == filterOff ? colours::faint : colours::accent);
    g.strokePath (curve, juce::PathStrokeType (1.8f));
}

void Editor::drawEnvelope (juce::Graphics& g, juce::Rectangle<float> area, const juce::String ids[4], juce::Colour colour) const
{
    g.setColour (colours::screen);
    g.fillRoundedRectangle (area, 5);

    // Times on a square-root scale so short ones still show, a held stretch for the sustain.
    const auto a = std::sqrt (get (ids[0])), d = std::sqrt (get (ids[1])), s = get (ids[2]), r = std::sqrt (get (ids[3]));
    const auto hold = 0.6f;
    const auto total = a + d + hold + r;
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

} // namespace triad
