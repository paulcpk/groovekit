#include "Sequencer.h"

namespace rhythm
{

void addSequencerParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const std::vector<Row>& rows, bool flam)
{
    using Attributes = juce::AudioParameterFloatAttributes;
    juce::StringArray patternNames, rowNames;

    for (int i = 1; i <= numPatterns; ++i)
        patternNames.add (juce::String (i));

    for (auto& row : rows)
        rowNames.add (row.name);

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::seq, 1 }, "Seq", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::pattern, 1 }, "Pattern", patternNames, 0));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id::lastStep, 1 }, "Last Step", 1, numSteps, numSteps));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::scale, 1 }, "Scale", scaleNames, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::shuffle, 1 }, "Shuffle", juce::NormalisableRange<float> (0, 1), 0.0f,
                                                             Attributes().withStringFromValueFunction ([] (float v, int)
                                                             {
                                                                 // As the swing of each pair of steps: 50% straight, 75% the most.
                                                                 return juce::String (juce::roundToInt (50 + v * 25)) + "%";
                                                             })));

    if (flam)
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::flam, 1 }, "Flam", juce::NormalisableRange<float> (5, 40), 18.0f,
                                                                 Attributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::editRow, 1 }, "Instrument", rowNames, 0));
}

//==============================================================================
PatternStore::PatternStore (const std::vector<Row>& rows)
{
    for (auto& row : rows)
        codes.add (row.code);

    for (auto& pattern : bits)
        for (auto& row : pattern)
            row = 0;
}

bool PatternStore::get (int pattern, int row, int step) const
{
    return valid (pattern, row) && (bits[static_cast<size_t> (pattern)][static_cast<size_t> (row)].load (std::memory_order_relaxed) >> step & 1) != 0;
}

void PatternStore::set (int pattern, int row, int step, bool on)
{
    if (! valid (pattern, row) || ! juce::isPositiveAndBelow (step, numSteps))
        return;

    auto& word = bits[static_cast<size_t> (pattern)][static_cast<size_t> (row)];
    const auto mask = 1u << step;
    word.store (on ? word.load() | mask : word.load() & ~mask);
    ++version;
}

uint32_t PatternStore::getRow (int pattern, int row) const
{
    return valid (pattern, row) ? bits[static_cast<size_t> (pattern)][static_cast<size_t> (row)].load (std::memory_order_relaxed) : 0;
}

void PatternStore::setRow (int pattern, int row, uint32_t value)
{
    if (! valid (pattern, row))
        return;

    bits[static_cast<size_t> (pattern)][static_cast<size_t> (row)].store (value & 0xffff);
    ++version;
}

void PatternStore::clear (int pattern)
{
    for (int row = 0; row < maxRows; ++row)
        setRow (pattern, row, 0);
}

void PatternStore::copy (int from, int to)
{
    if (from == to || ! valid (from, 0) || ! valid (to, 0))
        return;

    for (int row = 0; row < maxRows; ++row)
        setRow (to, row, getRow (from, row));

    names[static_cast<size_t> (to)] = names[static_cast<size_t> (from)];
}

juce::String PatternStore::getName (int pattern) const
{
    return valid (pattern, 0) ? names[static_cast<size_t> (pattern)] : juce::String();
}

void PatternStore::setName (int pattern, const juce::String& name)
{
    if (valid (pattern, 0))
        names[static_cast<size_t> (pattern)] = name;

    ++version;
}

void PatternStore::load (const std::vector<FactoryPattern>& factory)
{
    for (int p = 0; p < numPatterns; ++p)
    {
        clear (p);
        setName (p, {});

        if (p >= static_cast<int> (factory.size()))
            continue;

        const auto& f = factory[static_cast<size_t> (p)];
        setName (p, f.name);

        for (auto& [code, steps] : f.rows)
        {
            const auto row = indexOf (code);
            const juce::String text (steps);
            jassert (row >= 0);

            for (int s = 0; s < numSteps && s < text.length(); ++s)
                set (p, row, s, text[s] == 'x' || text[s] == 'X');
        }
    }
}

juce::ValueTree PatternStore::toTree() const
{
    // Readable as text: each row as its 16 steps, "x" or ".".
    juce::ValueTree tree (treeType);

    for (int p = 0; p < numPatterns; ++p)
    {
        juce::ValueTree pattern ("Pattern");
        pattern.setProperty ("index", p, nullptr);
        pattern.setProperty ("name", names[static_cast<size_t> (p)], nullptr);

        for (int row = 0; row < codes.size(); ++row)
        {
            juce::String steps;

            for (int s = 0; s < numSteps; ++s)
                steps << (get (p, row, s) ? "x" : ".");

            pattern.setProperty (codes[row], steps, nullptr);
        }

        tree.appendChild (pattern, nullptr);
    }

    return tree;
}

void PatternStore::fromTree (const juce::ValueTree& tree)
{
    for (auto pattern : tree)
    {
        const int p = pattern.getProperty ("index", -1);

        if (! valid (p, 0))
            continue;

        clear (p);
        setName (p, pattern.getProperty ("name").toString());

        for (int row = 0; row < codes.size(); ++row)
        {
            const auto steps = pattern.getProperty (codes[row]).toString();

            for (int s = 0; s < numSteps && s < steps.length(); ++s)
                set (p, row, s, steps[s] == 'x');
        }
    }
}

//==============================================================================
double Sequencer::stepBeats (int scale)
{
    switch (scale)
    {
        case 1:  return 1.0 / 3;
        case 2:  return 1.0 / 6;
        case 3:  return 0.125;
        default: return 0.25;
    }
}

void Sequencer::reset()
{
    lastStep = std::numeric_limits<int64_t>::min();
    lastPpq = -1;
    currentStep = -1;
}

void Sequencer::run (bool playing, double ppq, double bpm, int numSamples, double sampleRate, const Settings& s, std::vector<Step>& steps)
{
    steps.clear();

    if (! playing || bpm <= 0 || sampleRate <= 0)
    {
        reset();
        return;
    }

    // A jump back (a loop, or the host moved) plays its steps again.
    if (ppq < lastPpq - 1e-6)
        lastStep = std::numeric_limits<int64_t>::min();

    lastPpq = ppq;

    const auto beats = stepBeats (s.scale);
    const auto length = juce::jlimit (1, numSteps, s.lastStep);
    const auto samplesPerBeat = sampleRate * 60 / bpm;
    const auto end = ppq + numSamples / samplesPerBeat;
    const auto swing = s.shuffle * beats * 0.5;     // 75% swing at most: the off step half a step late
    const auto halfSample = 0.5 / samplesPerBeat;    // block edges from the host are not exact

    for (auto n = static_cast<int64_t> (std::floor ((ppq - swing) / beats)) - 1; ; ++n)
    {
        const auto step = static_cast<int> (((n % length) + length) % length);
        const auto t = static_cast<double> (n) * beats + (step % 2 == 1 ? swing : 0);

        if (static_cast<double> (n) * beats >= end)
            break;

        if (t < ppq - halfSample || t >= end - halfSample || n <= lastStep)
            continue;

        steps.push_back ({ juce::jlimit (0, numSamples - 1, juce::roundToInt ((t - ppq) * samplesPerBeat)), step });
        lastStep = n;
    }

    const auto now = static_cast<int64_t> (std::floor (end / beats));
    currentStep = static_cast<int> (((now % length) + length) % length);
}

} // namespace rhythm
