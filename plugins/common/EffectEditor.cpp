#include "EffectEditor.h"

namespace gkp
{

namespace
{
    constexpr int width = 820, height = 520, padSize = 400;
}

PadEffectEditor::PadEffectEditor (PresetProcessor& p, const juce::String& name, const juce::String& sub, juce::Colour a,
                                  std::vector<KnobSpec> knobSpecs, std::vector<ToggleSpec> toggleSpecs, std::vector<ChoiceSpec> choiceSpecs,
                                  std::vector<juce::String> params, const juce::String& x, const juce::String& y)
    : AudioProcessorEditor (p), owner (p), theme (Theme::dark (a)), accent (a), title (name), subtitle (sub), look (theme), presets (p, theme),
      padParams (std::move (params)), xId (x), yId (y)
{
    setLookAndFeel (&look);
    addAndMakeVisible (presets);
    auto& state = owner.state;

    for (auto& k : knobSpecs)
        addAndMakeVisible (*knobs.emplace_back (std::make_unique<Knob> (state, k.id, k.label, accent, theme, k.bipolar)));

    for (auto& t : toggleSpecs)
    {
        auto& button = toggles.emplace_back (std::make_unique<juce::TextButton> (t.label));
        button->setClickingTogglesState (true);
        toggleAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, t.id, *button));
        addAndMakeVisible (*button);
    }

    for (auto& c : choiceSpecs)
        addAndMakeVisible (*choices.emplace_back (std::make_unique<ChoiceBox> (state, c.id, c.labels)));

    juce::StringArray padNames;

    for (auto& id : padParams)
        padNames.add (state.getParameter (id)->getName (24));

    xChoice = std::make_unique<ChoiceBox> (state, xId, padNames);
    yChoice = std::make_unique<ChoiceBox> (state, yId, padNames);
    addAndMakeVisible (*xChoice);
    addAndMakeVisible (*yChoice);

    rebuildPad();
    setSize (width, height);
    startTimerHz (30);
}

PadEffectEditor::~PadEffectEditor()
{
    setLookAndFeel (nullptr);
}

void PadEffectEditor::rebuildPad()
{
    const auto x = juce::jlimit (0, static_cast<int> (padParams.size()) - 1, juce::roundToInt (get (xId)));
    const auto y = juce::jlimit (0, static_cast<int> (padParams.size()) - 1, juce::roundToInt (get (yId)));

    if (x == padX && y == padY && pad != nullptr)
        return;

    padX = x;
    padY = y;
    pad = std::make_unique<XYPad> (*owner.state.getParameter (padParams[static_cast<size_t> (x)]), *owner.state.getParameter (padParams[static_cast<size_t> (y)]), accent, theme);
    pad->background = [this] (juce::Graphics& g, juce::Rectangle<float> area) { drawPad (g, area); };
    addAndMakeVisible (*pad);
    pad->setBounds (padArea);
}

void PadEffectEditor::timerCallback()
{
    rebuildPad();
    repaint();
}

void PadEffectEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto header = r.removeFromTop (40);
    header.removeFromLeft (200);
    presets.setBounds (header.removeFromLeft (300).withSizeKeepingCentre (300, 28));
    r.removeFromTop (10);

    auto left = r.removeFromLeft (padSize);
    auto assign = left.removeFromBottom (26);
    xChoice->setBounds (assign.removeFromLeft (assign.getWidth() / 2 - 4));
    assign.removeFromRight (0);
    yChoice->setBounds (assign.withTrimmedLeft (8));
    left.removeFromBottom (6);
    statusArea = left.removeFromBottom (18);
    padArea = left;

    if (pad != nullptr)
        pad->setBounds (padArea);

    r.removeFromLeft (12);
    controlsArea = r;
    auto grid = r;
    const int columns = 4, cell = grid.getWidth() / columns, rowHeight = 64;
    int i = 0;

    for (auto& k : knobs)
    {
        k->setBounds (grid.getX() + (i % columns) * cell, grid.getY() + (i / columns) * rowHeight, cell, rowHeight);
        ++i;
    }

    // The choices on one line, the toggles on the next.
    auto below = grid.withTop (grid.getY() + ((i + columns - 1) / columns) * rowHeight + 6);

    if (! choices.empty())
    {
        auto line = below.removeFromTop (24);

        for (auto& c : choices)
        {
            c->setBounds (line.removeFromLeft (std::min (150, (grid.getWidth() - 8 * static_cast<int> (choices.size() - 1)) / static_cast<int> (choices.size()))));
            line.removeFromLeft (8);
        }

        below.removeFromTop (8);
    }

    auto line = below.removeFromTop (24);

    for (auto& t : toggles)
    {
        t->setBounds (line.removeFromLeft (90));
        line.removeFromLeft (8);
    }
}

void PadEffectEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);
    g.setColour (accent);
    g.fillRoundedRectangle (12, 14, 6, 22, 3);
    g.setColour (theme.fg);
    g.setFont (font (20, true));
    g.drawText (title.toUpperCase(), 26, 8, 180, 24, juce::Justification::centredLeft);
    g.setColour (theme.faint);
    g.setFont (font (10));
    g.drawText (subtitle, 27, 32, 200, 14, juce::Justification::centredLeft);

    g.setColour (theme.panel);
    g.fillRoundedRectangle (controlsArea.toFloat().expanded (6, 4), 8);
    drawStatus (g, statusArea);
}

} // namespace gkp
