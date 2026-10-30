#include "PluginEditor.h"

namespace quartet
{

namespace
{
    constexpr int width = 1000, height = 716;
    const juce::Colour amber (0xffe9a23b), lcdInk (0xffffc46b), lcdDim (0xff7a5a2a), lcdGlass (0xff1c1408);
    const juce::Colour opColours[4] { juce::Colour (0xffe9a23b), juce::Colour (0xffd8c27a), juce::Colour (0xff9fc8c0), juce::Colour (0xffc9a0d0) };
}

/** One operator: its wave, ratio, fine, level, velocity and key scaling, and its envelope. */
struct Editor::OpStrip : public juce::Component
{
    OpStrip (Editor& e, int n) : editor (e), number (n), colour (opColours[n - 1])
    {
        auto& state = editor.owner.state;
        wave = std::make_unique<gkp::Choice> (*state.getParameter (id::op (n, "Wave")), waveShort, colour, editor.theme);
        addAndMakeVisible (*wave);

        const std::pair<const char*, const char*> top[] { { "Ratio", "Ratio" }, { "Fine", "Fine" }, { "Level", "Level" }, { "Velocity", "Vel" }, { "KeyScale", "Scale" } };
        const std::pair<const char*, const char*> bottom[] { { "Attack", "A" }, { "Decay", "D" }, { "Sustain", "S" }, { "Release", "R" } };

        for (auto [suffix, label] : top)
        {
            const bool bipolar = juce::String (suffix) == "Fine" || juce::String (suffix) == "KeyScale";
            addAndMakeVisible (*knobs.emplace_back (std::make_unique<gkp::Knob> (state, id::op (n, suffix), label, colour, editor.theme, bipolar)));
        }

        for (auto [suffix, label] : bottom)
            addAndMakeVisible (*envelope.emplace_back (std::make_unique<gkp::Knob> (state, id::op (n, suffix), label, colour, editor.theme)));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6).withTrimmedTop (22);
        wave->setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        auto first = r.removeFromTop (r.getHeight() / 2);
        const auto w = first.getWidth() / 5;

        for (auto& k : knobs)
            k->setBounds (first.removeFromLeft (w));

        auto second = r;
        auto graphArea = second.removeFromRight (second.getWidth() / 3);
        graph = graphArea.reduced (2, 6).toFloat();
        const auto w2 = second.getWidth() / 4;

        for (auto& k : envelope)
            k->setBounds (second.removeFromLeft (w2));
    }

    void paint (juce::Graphics& g) override
    {
        gkp::drawPanel (g, getLocalBounds(), "Op " + juce::String (number), colour, editor.theme);
        const auto n = number;
        const auto& state = editor.owner.state;
        auto value = [&] (const char* what) { return state.getRawParameterValue (id::op (n, what))->load(); };
        gkp::drawEnvelope (g, graph, value ("Attack"), value ("Decay"), value ("Sustain"), value ("Release"), colour, editor.theme);

        // Whether it's heard or only modulates, in this algorithm.
        const auto& wiring = algorithms[static_cast<size_t> (juce::jlimit (0, 7, juce::roundToInt (state.getRawParameterValue (id::algorithm)->load())))];
        const bool carrier = wiring.carriers & (1 << (n - 1));
        g.setColour (editor.theme.faint);
        g.setFont (gkp::font (10));
        g.drawText (carrier ? "carrier" : "modulator", getLocalBounds().reduced (10, 6).withHeight (20), juce::Justification::topRight);
    }

    Editor& editor;
    const int number;
    const juce::Colour colour;
    std::unique_ptr<gkp::Choice> wave;
    std::vector<std::unique_ptr<gkp::Knob>> knobs, envelope;
    juce::Rectangle<float> graph;
};

//==============================================================================
Editor::Editor (Processor& p)
    : AudioProcessorEditor (p), owner (p), theme (gkp::Theme::hardwarePanel (amber)), look (theme), presets (p, theme), keyboard (p.keyboardState, theme)
{
    setLookAndFeel (&look);
    addAndMakeVisible (presets);

    volume = std::make_unique<gkp::Knob> (owner.state, id::gain, "Volume", amber, theme);
    addAndMakeVisible (*volume);

    for (int n = 1; n <= 4; ++n)
        addAndMakeVisible (*ops.emplace_back (std::make_unique<OpStrip> (*this, n)));

    auto& state = owner.state;
    auto choice = [&] (std::unique_ptr<gkp::Choice>& into, const juce::String& paramId, juce::StringArray labels, juce::Colour c)
    {
        into = std::make_unique<gkp::Choice> (*state.getParameter (paramId), std::move (labels), c, theme);
        addAndMakeVisible (*into);
    };
    auto box = [&] (std::unique_ptr<gkp::ChoiceBox>& into, const juce::String& paramId, const juce::StringArray& labels)
    {
        into = std::make_unique<gkp::ChoiceBox> (state, paramId, labels);
        addAndMakeVisible (*into);
    };

    choice (vaShape, id::vaShape, vaShapeNames, opColours[1]);
    choice (filterType, id::filterType, { "Off", "LP24", "LP12", "HP", "BP" }, amber);
    choice (lfoShape, id::lfoShape, { "Sin", "Tri", "Saw", "Sqr", "Rnd" }, opColours[2]);
    choice (chorusMode, id::chorusMode, chorusNames, opColours[3]);
    choice (voiceMode, id::voiceMode, voiceModeNames, amber);
    box (algorithm, id::algorithm, algorithmNames());
    box (chord, id::chord, chordNames);
    box (lfoTarget, id::lfoTarget, lfoTargetNames);

    vaKnobs = { knob (id::vaCoarse, "Coarse", opColours[1], true), knob (id::vaFine, "Fine", opColours[1], true), knob (id::vaWidth, "Width", opColours[1]),
                knob (id::vaSub, "Sub", opColours[1]), knob (id::vaUnison, "Unison", opColours[1]), knob (id::vaDetune, "Detune", opColours[1]) };
    mixKnobs = { knob (id::fmLevel, "FM", amber), knob (id::vaLevel, "Analogue", opColours[1]) };
    filterKnobs = { knob (id::cutoff, "Cutoff", amber), knob (id::resonance, "Reso", amber), knob (id::filterEnv, "Env", amber, true), knob (id::keyTrack, "Key", amber) };
    ampKnobs = { knob (id::ampAttack, "A", opColours[2]), knob (id::ampDecay, "D", opColours[2]), knob (id::ampSustain, "S", opColours[2]), knob (id::ampRelease, "R", opColours[2]) };
    filterEnvKnobs = { knob (id::filterAttack, "A", amber), knob (id::filterDecay, "D", amber), knob (id::filterSustain, "S", amber), knob (id::filterRelease, "R", amber) };
    feedback = knob (id::feedback, "Feedback", amber);
    chordKnobs = { knob (id::chordSpread, "Spread", opColours[3]), knob (id::chordDetune, "Detune", opColours[3]) };
    lfoKnobs = { knob (id::lfoRate, "Rate", opColours[2]), knob (id::lfoDepth, "Depth", opColours[2]) };
    chorusKnobs = { knob (id::chorusDepth, "Depth", opColours[3]), knob (id::drive, "Drive", amber), knob (id::tone, "Tone", amber, true) };
    voiceKnobs = { knob (id::glide, "Glide", amber), knob (id::velocity, "Velocity", amber), knob (id::bendRange, "Bend", amber) };

    keyboard.setAvailableRange (36, 84);
    keyboard.setOctaveForMiddleC (4);
    addAndMakeVisible (keyboard);

    setSize (width, height);
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

void Editor::timerCallback()
{
    owner.readScope (scope.data(), static_cast<int> (scope.size()));
    repaint();
}

void Editor::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto header = r.removeFromTop (56);
    header.removeFromLeft (200);
    lcd = header.removeFromLeft (360).reduced (0, 2);
    header.removeFromLeft (12);
    volume->setBounds (header.removeFromRight (60).withTrimmedTop (-2));
    header.removeFromRight (8);
    presets.setBounds (header.withSizeKeepingCentre (header.getWidth(), 28));

    r.removeFromTop (8);
    auto opsRow = r.removeFromTop (192);
    const auto opWidth = (opsRow.getWidth() - 3 * 8) / 4;

    for (auto& o : ops)
    {
        o->setBounds (opsRow.removeFromLeft (opWidth));
        opsRow.removeFromLeft (8);
    }

    r.removeFromTop (8);
    auto row2 = r.removeFromTop (150);
    vaArea = row2.removeFromLeft (250);
    row2.removeFromLeft (8);
    filterArea = row2.removeFromLeft (250);
    row2.removeFromLeft (8);
    ampArea = row2.removeFromLeft (170);
    row2.removeFromLeft (8);
    filterEnvArea = row2.removeFromLeft (170);
    row2.removeFromLeft (8);
    mixArea = row2;

    r.removeFromTop (8);
    auto row3 = r.removeFromTop (138);
    algorithmArea = row3.removeFromLeft (200);
    row3.removeFromLeft (8);
    chordArea = row3.removeFromLeft (190);
    row3.removeFromLeft (8);
    lfoArea = row3.removeFromLeft (200);
    row3.removeFromLeft (8);
    chorusArea = row3.removeFromLeft (200);
    row3.removeFromLeft (8);
    voiceArea = row3;

    r.removeFromTop (8);
    keyboardArea = r;
    keyboard.setBounds (keyboardArea.reduced (10, 8));
    keyboard.setKeyWidth (keyboard.getWidth() / 28.0f);

    auto va = vaArea.reduced (10).withTrimmedTop (22);
    vaShape->setBounds (va.removeFromTop (22));
    va.removeFromTop (4);
    gkp::layoutRow (va.removeFromTop (62), { vaKnobs[0], vaKnobs[1], vaKnobs[2] });
    gkp::layoutRow (va, { vaKnobs[3], vaKnobs[4], vaKnobs[5] });

    auto f = filterArea.reduced (10).withTrimmedTop (22);
    filterType->setBounds (f.removeFromTop (22));
    f.removeFromTop (4);
    gkp::layoutRow (f.removeFromBottom (62), filterKnobs);
    filterGraph = f.reduced (0, 3);

    auto a = ampArea.reduced (10).withTrimmedTop (22);
    gkp::layoutRow (a.removeFromBottom (62), ampKnobs);
    ampGraph = a.reduced (0, 3);

    auto e = filterEnvArea.reduced (10).withTrimmedTop (22);
    gkp::layoutRow (e.removeFromBottom (62), filterEnvKnobs);
    filterEnvGraph = e.reduced (0, 3);

    gkp::layoutRow (mixArea.reduced (6).withTrimmedTop (26), mixKnobs);

    auto al = algorithmArea.reduced (10).withTrimmedTop (22);
    algorithm->setBounds (al.removeFromTop (24));
    al.removeFromTop (4);
    feedback->setBounds (al.withSizeKeepingCentre (70, al.getHeight()));

    auto c = chordArea.reduced (10).withTrimmedTop (22);
    chord->setBounds (c.removeFromTop (24));
    c.removeFromTop (4);
    gkp::layoutRow (c, chordKnobs);

    auto l = lfoArea.reduced (10).withTrimmedTop (22);
    lfoShape->setBounds (l.removeFromTop (20));
    l.removeFromTop (4);
    auto lfoBottom = l;
    lfoTarget->setBounds (lfoBottom.removeFromLeft (90).withHeight (24).translated (0, 18));
    lfoBottom.removeFromLeft (4);
    gkp::layoutRow (lfoBottom, lfoKnobs);

    auto ch = chorusArea.reduced (10).withTrimmedTop (22);
    chorusMode->setBounds (ch.removeFromTop (22));
    ch.removeFromTop (4);
    gkp::layoutRow (ch, chorusKnobs);

    auto v = voiceArea.reduced (10).withTrimmedTop (22);
    voiceMode->setBounds (v.removeFromTop (22));
    v.removeFromTop (4);
    gkp::layoutRow (v, voiceKnobs);
}

//==============================================================================
void Editor::paint (juce::Graphics& g)
{
    // A brushed panel with its screws.
    g.setGradientFill (juce::ColourGradient (theme.bg.brighter (0.08f), 0, 0, theme.bg.darker (0.1f), 0, static_cast<float> (height), false));
    g.fillAll();
    g.setColour (juce::Colours::white.withAlpha (0.025f));

    for (int y = 0; y < height; y += 3)
        g.drawHorizontalLine (y, 0, static_cast<float> (width));

    for (auto corner : { juce::Point<float> (14, 14), { width - 14.0f, 14 }, { 14, height - 14.0f }, { width - 14.0f, height - 14.0f } })
    {
        g.setColour (juce::Colour (0xff1b1c1f));
        g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (corner));
        g.setColour (juce::Colour (0xff5a5c62));
        g.drawEllipse (juce::Rectangle<float> (8, 8).withCentre (corner), 1);
        g.drawLine (corner.x - 3, corner.y - 2, corner.x + 3, corner.y + 2, 1.2f);
    }

    g.setColour (theme.fg);
    g.setFont (gkp::font (24, true));
    g.drawText ("QUARTET", 22, 14, 180, 28, juce::Justification::centredLeft);
    g.setColour (theme.dim);
    g.setFont (gkp::font (10));
    g.drawText ("FM chord synthesizer", 23, 40, 180, 14, juce::Justification::centredLeft);

    drawLcd (g);

    gkp::drawPanel (g, vaArea, "Analogue layer", opColours[1], theme);
    gkp::drawPanel (g, filterArea, "Filter", amber, theme);
    gkp::drawFilterResponse (g, filterGraph.toFloat(), juce::roundToInt (get (id::filterType)), get (id::cutoff), get (id::resonance), amber, theme);
    gkp::drawPanel (g, ampArea, "Amp envelope", opColours[2], theme);
    gkp::drawEnvelope (g, ampGraph.toFloat(), get (id::ampAttack), get (id::ampDecay), get (id::ampSustain), get (id::ampRelease), opColours[2], theme);
    gkp::drawPanel (g, filterEnvArea, "Filter envelope", amber, theme);
    gkp::drawEnvelope (g, filterEnvGraph.toFloat(), get (id::filterAttack), get (id::filterDecay), get (id::filterSustain), get (id::filterRelease), amber, theme);
    gkp::drawPanel (g, mixArea, "Mix", amber, theme);
    gkp::drawPanel (g, algorithmArea, "Algorithm", amber, theme);
    gkp::drawPanel (g, chordArea, "Chord memory", opColours[3], theme);
    gkp::drawPanel (g, lfoArea, "LFO", opColours[2], theme);
    gkp::drawPanel (g, chorusArea, "Chorus, drive, tone", opColours[3], theme);
    gkp::drawPanel (g, voiceArea, "Voice", amber, theme);

    // The keyboard's bed.
    g.setColour (juce::Colour (0xff1a1b1e));
    g.fillRoundedRectangle (keyboardArea.toFloat(), 6);
}

void Editor::drawLcd (juce::Graphics& g) const
{
    const auto area = lcd.toFloat();
    g.setColour (juce::Colour (0xff0f0f11));
    g.fillRoundedRectangle (area.expanded (3), 6);
    g.setGradientFill (juce::ColourGradient (lcdGlass.brighter (0.15f), area.getX(), area.getY(), lcdGlass, area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, 4);
    g.setColour (lcdDim.withAlpha (0.3f));

    for (float y = area.getY() + 2; y < area.getBottom(); y += 2)
        g.drawHorizontalLine (static_cast<int> (y), area.getX() + 2, area.getRight() - 2);

    auto inner = area.reduced (10, 6);
    auto diagram = inner.removeFromRight (120);
    const auto program = owner.getProgramName (owner.getCurrentProgram());
    const auto colon = program.indexOf (": ");
    const auto name = colon > 0 ? program.substring (colon + 2) : program;
    const auto category = colon > 0 ? program.substring (0, colon) : juce::String ("Sound");

    g.setColour (lcdInk);
    g.setFont (gkp::font (17, true));
    g.drawText (name.toUpperCase(), inner.withHeight (22).toNearestInt(), juce::Justification::centredLeft);
    g.setFont (gkp::font (10));
    g.setColour (lcdDim.brighter (0.5f));
    const auto alg = juce::jlimit (0, 7, juce::roundToInt (get (id::algorithm)));
    const auto chordIndex = juce::jlimit (0, chordNames.size() - 1, juce::roundToInt (get (id::chord)));
    const auto voices = owner.activeVoices.load();
    g.drawText (category.toUpperCase() + "   ALG " + juce::String (alg + 1) + "   CHORD " + chordNames[chordIndex].toUpperCase()
                    + (voices > 0 ? "   " + juce::String (voices) + " VOICE" + (voices == 1 ? "" : "S") : ""),
                inner.withTrimmedTop (26).toNearestInt(), juce::Justification::centredLeft);

    drawAlgorithm (g, diagram);
}

void Editor::drawAlgorithm (juce::Graphics& g, juce::Rectangle<float> area) const
{
    // The operators as boxes: carriers along the bottom, modulators stacked above what they feed.
    const auto& wiring = algorithms[static_cast<size_t> (juce::jlimit (0, 7, juce::roundToInt (get (id::algorithm))))];
    std::array<int, 4> level {};     // 0 for carriers, 1 above them...

    for (int pass = 0; pass < 4; ++pass)
        for (int o = 0; o < 4; ++o)
            for (int m = 0; m < 4; ++m)
                if (wiring.modulators[static_cast<size_t> (o)] & (1 << m))
                    level[static_cast<size_t> (m)] = std::max (level[static_cast<size_t> (m)], level[static_cast<size_t> (o)] + 1);

    std::array<int, 4> column {};
    int carriersSeen = 0;

    for (int o = 0; o < 4; ++o)
    {
        if (wiring.carriers & (1 << o))
            column[static_cast<size_t> (o)] = carriersSeen++;
    }

    // A modulator sits over the operator it feeds (the first one, if several).
    for (int pass = 0; pass < 4; ++pass)
        for (int o = 0; o < 4; ++o)
            for (int m = 0; m < 4; ++m)
                if (wiring.modulators[static_cast<size_t> (o)] & (1 << m))
                    column[static_cast<size_t> (m)] = column[static_cast<size_t> (o)];

    const auto maxLevel = *std::max_element (level.begin(), level.end());
    const auto cell = 14.0f, gapX = 12.0f, gapY = 4.0f;
    const auto columns = std::max (1, carriersSeen);
    const auto originX = area.getCentreX() - (columns * cell + (columns - 1) * gapX) / 2;
    const auto bottom = area.getBottom() - 2;
    auto at = [&] (int o)
    {
        return juce::Rectangle<float> (originX + column[static_cast<size_t> (o)] * (cell + gapX), bottom - cell - level[static_cast<size_t> (o)] * (cell + gapY), cell, cell);
    };

    // Several modulators over one operator share its column, side by side.
    for (int o = 0; o < 4; ++o)
    {
        int siblings = 0, index = 0;

        for (int m = 0; m < 4; ++m)
            if (column[static_cast<size_t> (m)] == column[static_cast<size_t> (o)] && level[static_cast<size_t> (m)] == level[static_cast<size_t> (o)])
            {
                if (m < o) ++index;
                ++siblings;
            }

        auto r = at (o);

        if (siblings > 1)
            r = r.translated ((index - (siblings - 1) / 2.0f) * (cell + 2), 0);

        for (int m = 0; m < 4; ++m)
            if (wiring.modulators[static_cast<size_t> (o)] & (1 << m))
            {
                g.setColour (lcdDim.brighter (0.4f));
                g.drawLine (r.getCentreX(), r.getY(), r.getCentreX(), r.getY() - gapY, 1.2f);
            }

        const bool carrier = wiring.carriers & (1 << o);
        g.setColour (carrier ? lcdInk : lcdDim.brighter (0.6f));
        g.fillRoundedRectangle (r, 2);
        g.setColour (lcdGlass);
        g.setFont (gkp::font (9, true));
        g.drawText (juce::String (o + 1), r.toNearestInt(), juce::Justification::centred);
    }

    juce::ignoreUnused (maxLevel);
    g.setColour (lcdDim.brighter (0.3f));
    g.setFont (gkp::font (8));
    g.drawText ("4 feedback", area.withHeight (10).toNearestInt(), juce::Justification::centredRight);
}

} // namespace quartet
