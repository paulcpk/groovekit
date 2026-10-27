#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gk::music
{

/** A key: its tonic as a pitch class (0 = C) and whether it's minor (natural minor). */
struct Key
{
    int tonic = 0;
    bool minor = false;

    bool operator== (const Key&) const = default;

    /** The major key with the same notes: C major for A minor. */
    int relativeMajor() const       { return minor ? (tonic + 3) % 12 : tonic; }

    /** Whether a pitch class (0 = C) is in the key's scale. */
    bool contains (int pitchClass) const;

    /** The note in the scale nearest a MIDI note (down on a tie). */
    int snap (int note) const;
};

/** Parses "A minor", "Am", "C#min", "F# major", "Bb", "Ebm" (case-insensitive). */
std::optional<Key> parseKey (const std::string&);

/** "A minor", "F# major": sharps for C#, F# and G#, flats for Eb, Ab and Bb. */
std::string keyName (Key);

/** "C", "C#", ... for a pitch class. */
std::string pitchClassName (int pitchClass);

/** The smallest shift (-5..6 semitones) that puts music in one key into another: by their
    relative majors, so A minor and C major need none. */
int semitonesBetween (Key from, Key to);

/** Whether music in two keys goes together: the same notes (A minor and C major), or a fifth
    apart (one note different). */
bool compatible (Key, Key);

/** A key from how much of each pitch class (0 = C) there is, by correlation with the
    Krumhansl-Kessler key profiles. Confidence 0..1: how clearly it beats the next best key and
    how tonal the profile is; under about 0.3 it's a guess. */
struct KeyEstimate
{
    Key key;
    float confidence = 0;
};

std::optional<KeyEstimate> estimateKey (const std::array<float, 12>& chroma);

/** How much of each pitch class sounds in mono audio, from 55 Hz to 4 kHz (normalised to a
    largest value of 1). All zeros for silence. */
std::array<float, 12> chromaOf (const float* mono, int numSamples, double sampleRate);

} // namespace gk::music
