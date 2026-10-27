#pragma once

#include "Keys.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gk::music
{

/** A note heard in audio: seconds, a MIDI pitch with its fraction (60.3 is a little sharp of
    middle C), and how loud, 0..1. */
struct HeardNote
{
    double start = 0, end = 0;
    float pitch = 60;
    float level = 1;
};

/** The pitch every 10 ms of monophonic audio (a voice humming or singing), by YIN: a MIDI
    pitch, or 0 where there's none (silence, breath, consonants). */
struct PitchFrame
{
    double time = 0;
    float pitch = 0;        // MIDI, 0 when unvoiced
    float clarity = 0;      // 0..1: how periodic
    float level = 0;        // RMS
};

std::vector<PitchFrame> trackPitch (const float* mono, int numSamples, double sampleRate,
                                    double minHz = 65, double maxHz = 1100);

/** Notes in a pitch track: a note starts where the voice starts after a gap or a consonant
    ("da"), or where the pitch jumps and stays; its pitch is the middle of its steady part.
    Notes shorter than 70 ms go. */
std::vector<HeardNote> segmentNotes (const std::vector<PitchFrame>&);

/** Where in a recording someone sings or hums a melody rather than speaks: the longest run of
    steady notes (speech glides and keeps no pitch for long). Nothing if there's no such run of
    at least three notes. */
struct Span { double start = 0, end = 0; };
std::optional<Span> findSungPart (const std::vector<PitchFrame>&);

/** The highest note sounding at each moment (a melody out of chords), or the lowest (a bassline). */
std::vector<HeardNote> topLine (std::vector<HeardNote>);
std::vector<HeardNote> bottomLine (std::vector<HeardNote>);

/** The key of notes, weighted by their lengths. */
std::optional<KeyEstimate> keyOfNotes (const std::vector<HeardNote>&);

//==============================================================================
/** A note placed in a song: ticks (960 a beat) from the start of the melody, a MIDI key. */
struct PlacedNote
{
    int64_t start = 0, length = 0;
    int key = 60;
    int velocity = 100;
};

struct FitOptions
{
    double songTempo = 120;

    /** The tempo the notes were played at, when it's known (a reference track's). Otherwise
        it's worked out from the rhythm, preferring the song's tempo, double or half. */
    std::optional<double> sourceTempo;

    /** The song's key: the melody moves (by up to 6 semitones) into it and its notes snap to
        its scale. */
    std::optional<Key> key;
    bool snapToScale = true;

    /** The notes it should sit within: whole octaves move the melody's middle into the range. */
    std::optional<int> lowest, highest;

    /** Beats a step of the grid: 0.25 is 16ths. */
    double grid = 0.25;
};

struct Melody
{
    std::vector<PlacedNote> notes;
    double sourceTempo = 0;             // what the notes were heard at
    std::optional<KeyEstimate> heardKey;
    int transposed = 0;                 // semitones moved, octaves included
    int64_t length = 0;                 // ticks, whole bars of 4/4
};

/** Notes heard in seconds, placed on the song's grid. */
Melody fitMelody (const std::vector<HeardNote>&, const FitOptions&);

} // namespace gk::music
