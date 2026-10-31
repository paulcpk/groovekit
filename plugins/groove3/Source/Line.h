#pragma once

#include "GrooveKitMachine.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace groove3
{

/** A step of the line is a sixteenth (at 960 ticks per quarter note). */
constexpr int stepTicks = 240;

/** A step as the step section shows it: a note (on a key, maybe accented, maybe sliding into the
    next note), a tie (the note before held on through it), or a rest. */
struct Step
{
    enum Time : uint8_t { rest, note, tie };

    int key = 36;
    Time time = rest;
    bool accent = false, slide = false;

    bool operator== (const Step&) const = default;
};

using Line = std::vector<Step>;

/** The notes that start on the line's steps from fromStep up to toStep: a note's gate closes
    halfway through its last step (as the original's did); a tie holds it through the step; a
    slide holds it into the next note's start (overlapping it, which plays as a slide). An
    accented note has a velocity of 127, others 100. */
std::vector<gkmachine::Note> notesOf (const Line&, int fromStep, int toStep);

/** The steps notes make (the opposite of notesOf): a note on a step's start is a note there (the
    loudest, if several start together), the steps it holds on through are ties, and one lasting
    past the next note's start slides into it. Notes off the steps don't show. */
Line lineOf (const std::vector<gkmachine::Note>&, int lengthSteps);

/** The step whose note sounds at a step (the step itself, or the note it's tied to), or -1. */
int noteStart (const Line&, int step);

/** A line as text, a step to a word: "C2" a note, with "A" accented and "S" sliding ("D#2AS"),
    "-" a tie, "." a rest. */
juce::String toText (const Line&);
Line fromText (const juce::String&);

/** The lines the step section comes with. */
struct FactoryLine
{
    const char* name;
    const char* steps;
};

const std::vector<FactoryLine>& factoryLines();

} // namespace groove3
