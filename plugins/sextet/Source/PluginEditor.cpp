#include "PluginEditor.h"

namespace sextet
{

namespace
{
    constexpr int width = 1120, height = 980;
    const juce::Colour acid (0xff9dff3c), magenta (0xffe85ed6), cyan (0xff4fd6e8), orange (0xffffa44d), violet (0xffa98bff), rose (0xffff6b8b);
    const juce::Colour oscColours[numOscs] { acid, cyan, magenta, orange, violet, rose };
    const juce::Colour envColours[3] { acid, cyan, magenta };
    const juce::Colour lfoColours[3] { orange, violet, rose };
}

/** One oscillator: on, its wave (morphing and warping as set, drifting while notes play), and
    its knobs; what modulates it, where it goes, and sync. */
struct Editor::OscCard : public juce::Component
{
    OscCard (Editor& e, int n) : editor (e), number (n), colour (oscColours[n - 1]), on ("On"), sync ("Sync")
    {
        auto& state = editor.owner.state;
        on.setClickingTogglesState (true);
        sync.setClickingTogglesState (true);
        onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::osc (n, "On"), on);
        syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::osc (n, "Sync"), sync);
        addAndMakeVisible (on);

        if (n > 1)
            addAndMakeVisible (sync);

        modSource = std::make_unique<gkp::ChoiceBox> (state, id::osc (n, "ModSource"), modSourceNames);
        addAndMakeVisible (*modSource);
        route = std::make_unique<gkp::Choice> (*state.getParameter (id::osc (n, "Route")), juce::StringArray { "F1", "F2", "1+2", "Dry" }, colour, editor.theme);
        addAndMakeVisible (*route);

        const std::tuple<const char*, const char*, bool> items[] {
            { "Morph", "Shape", false }, { "Warp", "Warp", false }, { "Coarse", "Coarse", true }, { "Fine", "Fine", true }, { "Level", "Level", false },
            { "Pan", "Pan", true }, { "Unison", "Unison", false }, { "Detune", "Detune", false }, { "Fm", "FM", false }, { "Ring", "Ring", false },
        };

        for (auto [suffix, label, bipolar] : items)
            addAndMakeVisible (*knobs.emplace_back (std::make_unique<gkp::Knob> (state, id::osc (n, suffix), label, colour, editor.theme, bipolar)));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto top = r.removeFromTop (24);
        top.removeFromLeft (42);
        on.setBounds (top.removeFromLeft (34).reduced (0, 2));
        top.removeFromLeft (3);
        wave = top.removeFromLeft (70);
        top.removeFromLeft (3);
        modSource->setBounds (top.removeFromLeft (64).reduced (0, 1));
        top.removeFromLeft (3);
        route->setBounds (top.removeFromLeft (94).reduced (0, 2));
        top.removeFromLeft (3);
        sync.setBounds (top.removeFromLeft (34).reduced (0, 2));
        r.removeFromTop (2);
        auto first = r.removeFromTop (r.getHeight() / 2);
        const auto w = first.getWidth() / 5;

        for (size_t i = 0; i < 5; ++i)
            knobs[i]->setBounds (first.removeFromLeft (w));

        for (size_t i = 5; i < 10; ++i)
            knobs[i]->setBounds (r.removeFromLeft (w));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& state = editor.owner.state;
        const bool active = state.getRawParameterValue (id::osc (number, "On"))->load() >= 0.5f;
        gkp::drawPanel (g, getLocalBounds(), "Osc " + juce::String (number), active ? colour : editor.theme.faint, editor.theme);

        // The wave, drifting while notes play.
        const auto area = wave.toFloat();
        gkp::drawScreen (g, area, editor.theme);
        const auto morphPosition = state.getRawParameterValue (id::osc (number, "Morph"))->load();
        const auto warp = state.getRawParameterValue (id::osc (number, "Warp"))->load();
        const auto inner = area.reduced (4, 4);
        juce::Path path;

        for (int k = 0; k <= 96; ++k)
        {
            const auto t = k / 96.0;
            auto phase = gkp::dsp::wrap (t * 2 + editor.animation * (number * 0.21));
            const auto w = 0.5 - 0.45 * warp;
            phase = phase < w ? phase / (2 * w) : 0.5 + (phase - w) / (2 * (1 - w));
            const auto v = gkp::dsp::morph (morphPosition, phase, 0.0, w);
            const auto p = juce::Point<float> (inner.getX() + static_cast<float> (t) * inner.getWidth(), inner.getCentreY() - v * inner.getHeight() / 2 * 0.9f);
            k == 0 ? path.startNewSubPath (p) : path.lineTo (p);
        }

        g.setColour (active ? colour : editor.theme.faint);
        g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    Editor& editor;
    const int number;
    const juce::Colour colour;
    juce::TextButton on, sync;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment, syncAttachment;
    std::unique_ptr<gkp::ChoiceBox> modSource;
    std::unique_ptr<gkp::Choice> route;
    std::vector<std::unique_ptr<gkp::Knob>> knobs;
    juce::Rectangle<int> wave;
};

/** One line of the matrix: a source, a target and how much. */
struct Editor::ModSlot : public juce::Component
{
    ModSlot (Editor& e, int m) : amountAttachment (e.owner.state, id::mod (m, "Amount"), amount)
    {
        auto& state = e.owner.state;
        source = std::make_unique<gkp::ChoiceBox> (state, id::mod (m, "Source"), sourceNames);
        target = std::make_unique<gkp::ChoiceBox> (state, id::mod (m, "Target"), targetNames);
        amount.setSliderStyle (juce::Slider::LinearHorizontal);
        amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        amount.setColour (juce::Slider::trackColourId, acid.withAlpha (0.6f));
        amount.setColour (juce::Slider::backgroundColourId, e.theme.control);
        amount.setColour (juce::Slider::thumbColourId, e.theme.fg);
        amount.setDoubleClickReturnValue (true, 0);
        addAndMakeVisible (*source);
        addAndMakeVisible (*target);
        addAndMakeVisible (amount);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 1);
        source->setBounds (r.removeFromLeft (118));
        r.removeFromLeft (4);
        target->setBounds (r.removeFromLeft (148));
        r.removeFromLeft (6);
        amount.setBounds (r);
    }

    std::unique_ptr<gkp::ChoiceBox> source, target;
    juce::Slider amount;
    juce::AudioProcessorValueTreeState::SliderAttachment amountAttachment;
};

//==============================================================================
Editor::Editor (Processor& p)
    : AudioProcessorEditor (p), owner (p), theme (gkp::Theme::dark (acid)), look (theme), presets (p, theme)
{
    setLookAndFeel (&look);
    addAndMakeVisible (presets);
    volume = std::make_unique<gkp::Knob> (owner.state, id::gain, "Volume", acid, theme);
    addAndMakeVisible (*volume);

    for (int n = 1; n <= numOscs; ++n)
        addAndMakeVisible (*oscs.emplace_back (std::make_unique<OscCard> (*this, n)));

    for (int m = 1; m <= numMods; ++m)
        addAndMakeVisible (*mods.emplace_back (std::make_unique<ModSlot> (*this, m)));

    auto& state = owner.state;

    for (int k = 0; k < 2; ++k)
    {
        const auto c = k == 0 ? cyan : magenta;
        filterType[k] = std::make_unique<gkp::ChoiceBox> (state, id::filter (k + 1, "Type"), filterTypeNames);
        addAndMakeVisible (*filterType[k]);
        filterKnobs[k] = { knob (id::filter (k + 1, "Cutoff"), "Cutoff", c), knob (id::filter (k + 1, "Res"), "Res", c), knob (id::filter (k + 1, "Drive"), "Drive", c),
                           knob (id::filter (k + 1, "Env"), k == 0 ? "Env 2" : "Env 3", c, true), knob (id::filter (k + 1, "Key"), "Key", c) };
    }

    routing = std::make_unique<gkp::Choice> (*state.getParameter (id::filterRouting), juce::StringArray { "Par", "Ser" }, acid, theme);
    addAndMakeVisible (*routing);
    balance = knob (id::filterBalance, "Balance", acid, true);

    const char* envIds[3] { "amp", "env2", "env3" };

    for (int k = 0; k < 3; ++k)
    {
        const auto c = envColours[k];
        envKnobs[k] = { knob (id::env (envIds[k], "Attack"), "A", c), knob (id::env (envIds[k], "Decay"), "D", c), knob (id::env (envIds[k], "Sustain"), "S", c),
                        knob (id::env (envIds[k], "Release"), "R", c), knob (id::env (envIds[k], "Curve"), "Curve", c) };
    }

    for (int k = 0; k < 3; ++k)
    {
        const auto c = lfoColours[k];
        lfoShape[k] = std::make_unique<gkp::Choice> (*state.getParameter (id::lfo (k + 1, "Shape")), juce::StringArray { "Sin", "Tri", "Saw", "Sqr", "S&H", "Drf" }, c, theme);
        addAndMakeVisible (*lfoShape[k]);
        lfoSync[k] = std::make_unique<gkp::ChoiceBox> (state, id::lfo (k + 1, "Sync"), lfoSyncNames());
        addAndMakeVisible (*lfoSync[k]);
        lfoKnobs[k] = { knob (id::lfo (k + 1, "Rate"), "Rate", c) };
    }

    driveType = std::make_unique<gkp::Choice> (*state.getParameter (id::driveType), driveNames, orange, theme);
    addAndMakeVisible (*driveType);
    delaySync = std::make_unique<gkp::ChoiceBox> (state, id::delaySync, gkp::dsp::divisionNames());
    addAndMakeVisible (*delaySync);
    pingPong = std::make_unique<juce::TextButton> ("Ping-pong");
    pingPong->setClickingTogglesState (true);
    pingPongAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id::delayPingPong, *pingPong);
    addAndMakeVisible (*pingPong);
    fxKnobs = { knob (id::drive, "Drive", orange), knob (id::chorusRate, "Ch rate", violet), knob (id::chorusDepth, "Ch depth", violet), knob (id::chorusMix, "Chorus", violet),
                knob (id::reverbSize, "Size", rose), knob (id::reverbDamp, "Damp", rose), knob (id::reverbMix, "Reverb", rose) };
    fxKnobs2 = { knob (id::delayFeedback, "Feedback", cyan), knob (id::delayTone, "Tone", cyan), knob (id::delayMix, "Delay", cyan) };

    voiceMode = std::make_unique<gkp::Choice> (*state.getParameter (id::voiceMode), voiceModeNames, acid, theme);
    addAndMakeVisible (*voiceMode);
    voiceKnobs = { knob (id::glide, "Glide", acid), knob (id::velocity, "Velocity", acid), knob (id::bendRange, "Bend", acid) };

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

    if (owner.activeVoices > 0)
        animation += 1.0 / 30.0 * 0.4;

    repaint();
}

void Editor::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto header = r.removeFromTop (48);
    header.removeFromLeft (190);
    volume->setBounds (header.removeFromRight (56).withTrimmedTop (-2));
    header.removeFromRight (8);
    scopeArea = header.removeFromRight (220).reduced (0, 4);
    header.removeFromRight (12);
    presets.setBounds (header.withSizeKeepingCentre (std::min (360, header.getWidth()), 28));

    r.removeFromTop (8);
    const auto cardWidth = (r.getWidth() - 16) / 3;

    for (int row = 0; row < 2; ++row)
    {
        auto line = r.removeFromTop (150);

        for (int col = 0; col < 3; ++col)
        {
            oscs[static_cast<size_t> (row * 3 + col)]->setBounds (line.removeFromLeft (cardWidth));
            line.removeFromLeft (8);
        }

        r.removeFromTop (8);
    }

    auto row = r.removeFromTop (130);
    filterArea[0] = row.removeFromLeft (270);
    row.removeFromLeft (8);
    filterArea[1] = row.removeFromLeft (270);
    row.removeFromLeft (8);
    routingArea = row.removeFromLeft (118);
    row.removeFromLeft (8);
    envArea[0] = row;

    r.removeFromTop (8);
    row = r.removeFromTop (130);
    envArea[1] = row.removeFromLeft (270);
    row.removeFromLeft (8);
    envArea[2] = row.removeFromLeft (270);
    row.removeFromLeft (8);
    const auto lfoWidth = (row.getWidth() - 16) / 3;

    for (int k = 0; k < 3; ++k)
    {
        lfoArea[k] = row.removeFromLeft (lfoWidth);
        row.removeFromLeft (8);
    }

    r.removeFromTop (8);
    row = r.removeFromTop (150);
    fxArea = row.removeFromLeft (row.getWidth() - 208);
    row.removeFromLeft (8);
    voiceArea = row;

    r.removeFromTop (8);
    matrixArea = r;

    for (int k = 0; k < 2; ++k)
    {
        auto f = filterArea[k].reduced (10).withTrimmedTop (22);
        auto top = f.removeFromTop (22);
        filterType[k]->setBounds (top.removeFromLeft (110));
        top.removeFromLeft (6);
        filterGraph[k] = top.withHeight (22);
        f.removeFromTop (4);
        gkp::layoutRow (f, filterKnobs[k]);
    }

    auto ro = routingArea.reduced (8).withTrimmedTop (22);
    routing->setBounds (ro.removeFromTop (22));
    ro.removeFromTop (2);
    balance->setBounds (ro);

    for (int k = 0; k < 3; ++k)
    {
        auto e = envArea[k].reduced (10).withTrimmedTop (22);
        envGraph[k] = e.removeFromRight (k == 0 ? 180 : 90).reduced (4, 2);
        gkp::layoutRow (e, envKnobs[k]);
    }

    for (int k = 0; k < 3; ++k)
    {
        auto l = lfoArea[k].reduced (8).withTrimmedTop (22);
        lfoShape[k]->setBounds (l.removeFromTop (20));
        l.removeFromTop (3);
        auto bottom = l;
        lfoKnobs[k][0]->setBounds (bottom.removeFromLeft (60));
        bottom.removeFromLeft (4);
        lfoSync[k]->setBounds (bottom.removeFromTop (22));
        lfoGraph[k] = bottom.withTrimmedTop (4);
    }

    auto fx = fxArea.reduced (10).withTrimmedTop (22);
    auto first = fx.removeFromTop (fx.getHeight() / 2);
    driveType->setBounds (first.removeFromLeft (100).withSizeKeepingCentre (100, 22));
    first.removeFromLeft (6);
    gkp::layoutRow (first, fxKnobs);
    auto second = fx;
    auto delayTop = second.removeFromLeft (106);
    delaySync->setBounds (delayTop.removeFromTop (22));
    delayTop.removeFromTop (4);
    pingPong->setBounds (delayTop.removeFromTop (22));
    second.removeFromLeft (6);
    gkp::layoutRow (second.removeFromLeft (3 * 70), fxKnobs2);

    auto v = voiceArea.reduced (10).withTrimmedTop (22);
    voiceMode->setBounds (v.removeFromTop (22));
    v.removeFromTop (4);
    gkp::layoutRow (v, voiceKnobs);

    auto m = matrixArea.reduced (10).withTrimmedTop (24);
    const auto columnWidth = (m.getWidth() - 12) / 2;
    const auto rowHeight = m.getHeight() / 5;

    for (size_t i = 0; i < mods.size(); ++i)
    {
        const auto col = static_cast<int> (i / 5), line = static_cast<int> (i % 5);
        mods[i]->setBounds (m.getX() + col * (columnWidth + 12), m.getY() + line * rowHeight, columnWidth, rowHeight);
    }
}

//==============================================================================
void Editor::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);

    // The name: six bars in the oscillators' colours.
    for (int i = 0; i < numOscs; ++i)
    {
        g.setColour (oscColours[i]);
        g.fillRoundedRectangle (14.0f + i * 7, 22.0f - (i % 2) * 4, 5, 14 + (i % 2) * 8, 2);
    }

    g.setColour (theme.fg);
    g.setFont (gkp::font (20, true));
    g.drawText ("SEXTET", 62, 10, 140, 26, juce::Justification::centredLeft);
    g.setColour (theme.faint);
    g.setFont (gkp::font (10));
    g.drawText ("six-oscillator synth", 63, 36, 140, 14, juce::Justification::centredLeft);

    gkp::drawScope (g, scopeArea.toFloat(), scope.data(), static_cast<int> (scope.size()), acid, theme);
    g.setColour (theme.faint);
    g.setFont (gkp::font (9));
    const auto voices = owner.activeVoices.load();
    g.drawText (voices > 0 ? juce::String (voices) + (voices == 1 ? " voice" : " voices") : "silent", scopeArea.reduced (6, 3), juce::Justification::topRight);

    for (int k = 0; k < 2; ++k)
    {
        const auto c = k == 0 ? cyan : magenta;
        gkp::drawPanel (g, filterArea[k], "Filter " + juce::String (k + 1), c, theme);
        const auto type = juce::roundToInt (get (id::filter (k + 1, "Type")));
        const int drawn = type == lowpass24 ? 1 : type == lowpass12 ? 2 : type == highpass12 || type == highpass24 ? 3 : type == bandpass ? 4 : type == notch ? 5 : 0;
        gkp::drawFilterResponse (g, filterGraph[k].toFloat(), drawn, get (id::filter (k + 1, "Cutoff")), get (id::filter (k + 1, "Res")), c, theme);
    }

    gkp::drawPanel (g, routingArea, "Routing", acid, theme);
    const char* envNames[3] { "Amp envelope", "Envelope 2", "Envelope 3" };
    const char* envIds[3] { "amp", "env2", "env3" };

    for (int k = 0; k < 3; ++k)
    {
        gkp::drawPanel (g, envArea[k], envNames[k], envColours[k], theme);
        gkp::drawEnvelope (g, envGraph[k].toFloat(), get (id::env (envIds[k], "Attack")), get (id::env (envIds[k], "Decay")), get (id::env (envIds[k], "Sustain")),
                           get (id::env (envIds[k], "Release")), envColours[k], theme);
    }

    for (int k = 0; k < 3; ++k)
    {
        gkp::drawPanel (g, lfoArea[k], "LFO " + juce::String (k + 1), lfoColours[k], theme);
        drawLfo (g, lfoGraph[k].toFloat(), k);
    }

    gkp::drawPanel (g, fxArea, "Drive, chorus, delay, reverb", orange, theme);
    gkp::drawPanel (g, voiceArea, "Voice", acid, theme);
    gkp::drawPanel (g, matrixArea, "Modulation matrix", acid, theme);
}

void Editor::drawLfo (juce::Graphics& g, juce::Rectangle<float> area, int k) const
{
    gkp::drawScreen (g, area, theme);
    const auto shape = juce::roundToInt (get (id::lfo (k + 1, "Shape")));
    const auto inner = area.reduced (4, 4);
    juce::Path path;
    juce::Random random (k + 1);
    float held = 0;

    for (int i = 0; i <= 64; ++i)
    {
        const auto phase = i / 32.0;
        const auto t = phase - std::floor (phase);
        float v = 0;

        if (i % 32 == 0)
            held = random.nextFloat() * 2 - 1;

        switch (shape)
        {
            case 1:  v = static_cast<float> (t < 0.5 ? 4 * t - 1 : 3 - 4 * t); break;
            case 2:  v = static_cast<float> (2 * t - 1); break;
            case 3:  v = t < 0.5 ? 1.0f : -1.0f; break;
            case 4:  v = held; break;
            case 5:  v = static_cast<float> (std::sin (gkp::dsp::twoPi * t) * 0.6 + std::sin (gkp::dsp::twoPi * t * 2.3) * 0.3); break;
            default: v = static_cast<float> (std::sin (gkp::dsp::twoPi * t)); break;
        }

        const auto p = juce::Point<float> (inner.getX() + inner.getWidth() * i / 64.0f, inner.getCentreY() - v * inner.getHeight() / 2 * 0.85f);
        i == 0 ? path.startNewSubPath (p) : path.lineTo (p);
    }

    g.setColour (lfoColours[k]);
    g.strokePath (path, juce::PathStrokeType (1.4f));

    // Where it is right now.
    const auto value = owner.lfoValues[static_cast<size_t> (k)];
    g.setColour (lfoColours[k].withAlpha (0.8f));
    g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ inner.getRight() - 4, inner.getCentreY() - value * inner.getHeight() / 2 * 0.85f }));
}

} // namespace sextet
