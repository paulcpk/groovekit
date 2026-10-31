#include "Line.h"

namespace groove3
{

namespace
{
    constexpr int normalVelocity = 100, accentedVelocity = 127, accentFrom = 110;

    const char* const keyNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

std::vector<gkmachine::Note> notesOf (const Line& line, int fromStep, int toStep)
{
    std::vector<gkmachine::Note> notes;
    const auto length = static_cast<int> (line.size());

    for (auto s = std::max (0, fromStep); s < std::min (toStep, length); ++s)
    {
        const auto& step = line[static_cast<size_t> (s)];

        if (step.time != Step::note)
            continue;

        auto end = s + 1;

        while (end < length && line[static_cast<size_t> (end)].time == Step::tie)
            ++end;

        const bool intoNote = end < length && line[static_cast<size_t> (end)].time == Step::note;
        auto ticks = (end - s) * stepTicks;

        if (step.slide)
            ticks += intoNote ? stepTicks / 4 : 0;
        else
            ticks -= stepTicks / 2;

        notes.push_back ({ step.key, s * stepTicks, ticks, step.accent ? accentedVelocity : normalVelocity });
    }

    return notes;
}

Line lineOf (const std::vector<gkmachine::Note>& notes, int lengthSteps)
{
    Line line (static_cast<size_t> (std::max (0, lengthSteps)));

    // The note on each step: the loudest starting there (then the lowest).
    std::vector<const gkmachine::Note*> onStep (line.size(), nullptr);

    for (auto& n : notes)
    {
        if (n.start % stepTicks != 0 || n.start < 0 || n.start / stepTicks >= lengthSteps)
            continue;

        auto& slot = onStep[static_cast<size_t> (n.start / stepTicks)];

        if (slot == nullptr || n.velocity > slot->velocity || (n.velocity == slot->velocity && n.key < slot->key))
            slot = &n;
    }

    for (int s = 0; s < lengthSteps; ++s)
    {
        const auto* n = onStep[static_cast<size_t> (s)];

        if (n == nullptr)
            continue;

        auto next = s + 1;

        while (next < lengthSteps && onStep[static_cast<size_t> (next)] == nullptr)
            ++next;

        const auto end = n->start + n->length;
        const bool slide = next < lengthSteps && end > next * stepTicks;
        const auto held = (end + stepTicks - 1) / stepTicks - s - 1;
        const auto ties = slide ? next - s - 1 : std::clamp (held, 0, next - s - 1);

        line[static_cast<size_t> (s)] = { n->key, Step::note, n->velocity >= accentFrom, slide };

        for (int t = 1; t <= ties; ++t)
            line[static_cast<size_t> (s + t)].time = Step::tie;
    }

    return line;
}

int noteStart (const Line& line, int step)
{
    for (auto s = std::min (step, static_cast<int> (line.size()) - 1); s >= 0; --s)
    {
        const auto time = line[static_cast<size_t> (s)].time;

        if (time == Step::note)
            return s;

        if (time == Step::rest)
            return -1;
    }

    return -1;
}

juce::String toText (const Line& line)
{
    juce::StringArray words;

    for (auto& step : line)
    {
        if (step.time == Step::note)
            words.add (juce::String (keyNames[step.key % 12]) + juce::String (step.key / 12 - 1) + (step.accent ? "A" : "") + (step.slide ? "S" : ""));
        else
            words.add (step.time == Step::tie ? "-" : ".");
    }

    return words.joinIntoString (" ");
}

Line fromText (const juce::String& text)
{
    Line line;

    for (auto word : juce::StringArray::fromTokens (text, " ", ""))
    {
        word = word.trim();

        if (word.isEmpty())
            continue;

        Step step;

        if (word == "-")
        {
            step.time = line.empty() ? Step::rest : Step::tie;
        }
        else if (word != ".")
        {
            // A key, its octave (maybe below zero), then the flags.
            auto letters = word.substring (0, word[1] == '#' ? 2 : 1).toUpperCase();
            auto rest = word.substring (letters.length());
            auto digits = rest.initialSectionContainingOnly ("-0123456789");
            auto flags = rest.substring (digits.length()).toUpperCase();
            auto index = -1;

            for (int k = 0; k < 12; ++k)
                if (letters == keyNames[k])
                    index = k;

            if (index >= 0 && digits.isNotEmpty())
            {
                step.key = juce::jlimit (0, 127, (digits.getIntValue() + 1) * 12 + index);
                step.time = Step::note;
                step.accent = flags.containsChar ('A');
                step.slide = flags.containsChar ('S');
            }
        }

        line.push_back (step);
    }

    return line;
}

const std::vector<FactoryLine>& factoryLines()
{
    static const std::vector<FactoryLine> lines {
        { "Acid Roll",   "C2 C2 C3A C2 D#2S F2 C2 C3 C2 G2S G#2 C2 C3A C2 A#1S C2" },
        { "Squelch",     "C2A - C2 . C3S C2 . D#2A C2 . F2S F#2 G2 . C2A C3" },
        { "Night Shift", "A1A . A1 A2S . A1 C2 . A1A . E2S D2 A1 . G1 A1" },
        { "Bounce",      "C2 C3 C2 C3 D#2 D#3 C2 C3 F2 F3 C2 C3 G2S F2 D#2 C3A" },
        { "Warehouse",   "F1A F1 F2S F1 . F1 G#1 F1 A#1A F1 C2S F1 . F1 D#2 F1" },
        { "Tide",        "C2 - - C2A D#2 - G2S F2 - - C2 C2A A#1 - C2S D#2" },
        { "Hypnotic",    "E1A E1 E2 E1 E1 G1S E1 E2A E1 E1 D2 E1 B1S E1 E2 E1" },
        { "Spiral",      "C2 D#2 G2 C3A A#2S G2 D#2 C2 C2A D#2 F2S G2 A#2 G2 F2 D#2S" },
        { "Off Beat",    ". C2A . C2 . C2A . C3S . C2A . C2 . D#2A . F2S" },
        { "Rubber",      "G1A - G2S G1 . G1 A#1 C2S G1 - G1A . F2S G1 D2 G1" },
        { "Acid Rain",   "D2 D2 D3A D2 D2 F2 D2 D3A D2 C3S D2 D2 G2A D2 A2S D2" },
        { "Low Rider",   "A#0A - A#0 A#1S A#0 . C1 C#1 A#0A - A#0 F1S D#1 . A#0 A#1A" },
        { "Snake",       "C2 C#2S D2 C2 C2A C#2S D2 D#2 C2 C2A F#2S G2 C2 C#2 C2A C3" },
        { "Lift",        "C2A C3 G2 C3 C2 C3A G2 C3 D#2A D#3 A#2 D#3 F2 F3A C3S D#3" },
        { "Stomp",       "C1A . C2 . C1A . C2S A#1 C1A . C2 . C1A D#1S F1 G1" },
        { "Wobble",      "F2A - - - F2S G#2 - - F2A - - - C3S A#2 G#2S G2" },
    };

    return lines;
}

} // namespace groove3
