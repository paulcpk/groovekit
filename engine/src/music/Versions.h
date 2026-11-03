#pragma once

#include "Notes.h"

#include <filesystem>
#include <map>

namespace gk::music
{

/** A version of a hummed line, to offer beside the others: never the raw hum, always on the grid. */
struct MelodyVersion
{
    std::string name;                   // hummed, scale, groove, simpler, half, double, straight, shuffle, answer
    std::string label;                  // in words, for the pick window
    std::vector<PlacedNote> notes;
    int64_t length = 0;                 // ticks, whole bars
    float closeness = 0;                // 0-100: how close to the hum (its rhythm first, then its shape)
    std::optional<float> fit;           // 0-100: how close its rhythm is to the style's figures
};

struct VersionOptions
{
    std::optional<Key> key;
    int count = 6;                      // at most this many, the hummed one first
    int bars = 0;                       // loop every version to this many bars; 0 keeps each one's length

    /** The style's rhythms: each the 16ths its figures play on, over steps 16ths. */
    struct Rhythm
    {
        std::vector<int> onsets;
        int steps = 32;
    };

    std::vector<Rhythm> styleRhythms;

    /** The line read straight, when it was heard as triplets or swung: offered as a version. A
        straight line gets a shuffled version instead. */
    std::optional<Melody> straight;

    /** How often each kind of version (by name) was offered and picked: kinds picked often rise,
        kinds passed over many times are left out. */
    struct Record
    {
        int shown = 0, chosen = 0;
    };

    std::map<std::string, Record> picks;

    /** A part already in the song to answer: its notes and length (ticks), and its name. */
    std::vector<PlacedNote> partner;
    int64_t partnerLength = 0;
    std::string partnerName;
};

/** Versions of a hummed line: as hummed (in key, on the grid), its rhythm with the key's notes
    following its shape (when the hum is off key, the rhythm is what was meant), its notes moved to
    the style's nearest rhythm, simpler (fewer notes, a one- or two-bar loop), at half and double
    speed, and answering a part in the song. hummed is the line fitted and snapped to the key;
    raw the same fitted without snapping (its intervals as sung). The hummed version first, the
    rest by closeness and fit; versions that come out the same are left out. */
std::vector<MelodyVersion> melodyVersions (const Melody& hummed, const Melody& raw, const VersionOptions&);

/** The user's picks among versions, kept in a file: read (nothing if there's none), and one more
    added (the kind chosen, and every kind shown with it). */
std::map<std::string, VersionOptions::Record> readVersionPicks (const std::filesystem::path&);
void addVersionPick (const std::filesystem::path&, const std::string& chosen, const std::vector<std::string>& shown);

} // namespace gk::music
