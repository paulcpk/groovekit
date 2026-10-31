#pragma once

#include "Arrangement.h"
#include "NoteTranscriber.h"
#include "Separator.h"
#include "model/JsonIO.h"

#include <array>

namespace gk::listening
{

/** A note heard in a stem, with what its sound is like where it starts, so notes of different
    instruments in the same register can be told apart. */
struct SoundedNote
{
    music::HeardNote note;

    /** The note's first 90 ms: the strength of its first eight harmonics (dB under the strongest), how much of its energy is
        above its eighth harmonic (dB against the whole), and how far it falls from its start to
        its end (dB, 0 for a held note). */
    std::array<float, 8> harmonics {};
    float air = 0, fall = 0;

    model::Json toJson() const;
};

/** Every note in a stem (mono or stereo at rate), with its sound. */
std::vector<SoundedNote> hearNotes (NoteTranscriber&, const juce::AudioBuffer<float>& stem, double rate, double minNoteSeconds = 0.06);

/** The notes of a bassline, one at a time: the note model hears little under 60 Hz and runs
    repeated notes together, so a bass is tracked by its pitch (YIN), and a note starts at each
    attack (in its level or, for a distorted bass whose level hardly moves, its spectrum) or
    where the pitch moves and stays. */
std::vector<SoundedNote> hearBass (const juce::AudioBuffer<float>& stem, double rate);

//==============================================================================
/** A note on a track's 16th grid: steps from its first bar's start (16 a bar), how many steps
    it's held, its key and how loud it is (1-127). */
struct GridNote
{
    int step = 0, steps = 1;
    int key = 60;
    int velocity = 100;

    bool operator== (const GridNote&) const = default;
};

/** One melodic part of a track heard through it: a bassline, a stab, a lead, an arp, a pad, a
    pitched kick. Its notes are kept as runs: a figure (one, two or four bars) and the bars it
    repeats over, from which every bar can be played back. */
struct Voice
{
    std::string role;           // bass, kick, stab, chords, lead, arp, pluck, pad
    std::string part;           // the stem it's heard in: bass, music or drums
    std::string sound;          // in words, from its harmonics: "full, saw-like", "hollow, square-like", ...
    int lowest = 0, highest = 0;
    float level = 0;            // 0..1, against the loudest note of its stem
    float chord = 1;            // notes started together, on average
    float notesPerBar = 0;      // where it plays

    struct Run
    {
        int from = 0, to = 0;               // bars, end excluded
        int bars = 1;                       // the figure's length
        std::vector<GridNote> notes;        // steps from the figure's start
        std::string change;                 // how it differs from the run before: "new", "varied", "notes", "transposed +5", "thinner", "fuller", "as bars 9-16"
        int variations = 0;                 // repeats that differed (fills)

        model::Json toJson() const;
        static Run fromJson (const model::Json&);
    };

    std::vector<Run> runs;

    /** The notes it plays in a bar (steps from that bar's start), from its runs. */
    std::vector<GridNote> notesIn (int bar) const;
    bool playsIn (int bar) const;

    model::Json toJson() const;
    static Voice fromJson (const model::Json&);
};

/** How two voices go together where both play: on how many of their steps they play together,
    how many of the second's notes fill the first's gaps, whether the second answers the first
    (plays in the gaps after the first's phrases), and the intervals between them. */
struct Pairing
{
    int a = 0, b = 0;           // voices
    int bars = 0;               // bars both play
    float together = 0;         // shared onsets against all onsets of both
    float fills = 0;            // b's onsets where a has none
    bool answers = false;
    int answerAfter = 0;        // 16ths from the end of a's phrase to b's
    std::vector<std::pair<int, float>> intervals;   // semitones b's notes are over a's (mod 12), with their shares

    model::Json toJson() const;
    static Pairing fromJson (const model::Json&);
};

struct TrackVoices
{
    std::vector<Voice> voices;
    std::vector<Pairing> pairs;
    std::optional<music::KeyEstimate> key;

    /** How many voices play in a bar. */
    int playingIn (int bar) const;

    model::Json toJson() const;
    static TrackVoices fromJson (const model::Json&);
};

/** Every melodic voice of a track from its stems and its arrangement: the bass from the bass
    stem, the pitched parts of the music stem told apart by their sound, register and note
    lengths, and a kick that changes pitch; each through the track as runs of figures, and how
    they pair. Blocks for seconds. */
TrackVoices hearVoices (NoteTranscriber&, const Arrangement&, const Separator::Stems&);

/** Notes on the grid of a track of so many bars as runs: the figure's length (1, 2 or 4 bars)
    from how the bars repeat; a run for each stretch it plays within a section (from the
    arrangement: bars where sections start), split where its figure changes, its figure the
    notes at least half its repeats have. */
std::vector<Voice::Run> foldRuns (const std::vector<GridNote>&, int bars, const std::vector<int>& sectionStarts = {});

/** How each pair of voices goes together, both ways round. */
std::vector<Pairing> pairVoices (const std::vector<Voice>&, int bars);

/** A role from how a voice plays. */
std::string roleOf (const std::string& part, int medianKey, float chord, float notesPerBar, float medianSteps);

/** The voices as text, for the agent: each with its role, range and runs as bars of 16ths, and
    how the pairs go together. */
std::string describeVoices (const TrackVoices&);

/** A figure as a row of 16ths: a note name starts a note, - holds it, . is a rest; notes
    together joined by +; | between bars. */
std::string figureText (const std::vector<GridNote>&, int bars);

} // namespace gk::listening
