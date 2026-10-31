#pragma once

#include "Voices.h"
#include "model/Project.h"

namespace gk::listening
{

struct StyleProfile;

/** What a style's references say about their voices: which roles play and how many voices at
    once, the figures each role plays (moved to C: each track's tonic to C), how long a figure
    lasts before it changes and how, and how the roles pair. Built from the style's ticked maps
    heard with voices. */
struct VoiceLibrary
{
    std::string style;
    int tracks = 0;

    struct Figure
    {
        int bars = 1;
        std::vector<GridNote> notes;    // in C, steps from its start
        float share = 0;                // of the role's bars, with this rhythm
        int tracks = 0;
    };

    struct Role
    {
        std::string name;
        int tracks = 0;
        int lowest = 0, highest = 0;    // in C, the medians
        float notesPerBar = 0, chord = 1;
        int runBars = 0;                // how long a figure lasts, the median
        std::vector<std::pair<std::string, float>> changes;    // how figures change, with shares
        std::vector<Figure> figures;    // the commonest rhythms first, each with its notes
    };

    std::vector<Role> roles;

    /** Two roles where both play: b answers a when answers is over half. */
    struct Pair
    {
        std::string a, b;
        int count = 0;                  // tracks where both play
        float together = 0, fills = 0, answers = 0;
        int answerAfter = 0;
        std::vector<std::pair<int, float>> intervals;
    };

    std::vector<Pair> pairs;

    /** Voices playing at once in each kind of section: the median and the most. */
    struct AtOnce
    {
        std::string section;
        float median = 0;
        int most = 0;
    };

    std::vector<AtOnce> atOnce;

    const Role* role (const std::string&) const;
    const Pair* pair (const std::string& a, const std::string& b) const;   // either order

    model::Json toJson() const;
    std::string describe() const;
};

VoiceLibrary buildVoiceLibrary (const StyleProfile&);

//==============================================================================
/** A song's own voices, from its notes: each pitched channel (not the drums) through the
    playlist, as a heard track's voices are. */
struct SongVoices
{
    TrackVoices voices;
    std::vector<std::string> channels;      // each voice's channel
    int bars = 0;
};

SongVoices songVoices (const model::Project&);

/** Ranked problems of a song's voices against a style's: too many at once, parts doubling where
    the style interlocks, a part nobody answers where the style answers it, a figure that never
    changes, notes out of the key, a busier part than the style's. Most important first. */
std::vector<std::string> checkVoices (const SongVoices&, const VoiceLibrary&, std::optional<music::Key> songKey);

//==============================================================================
struct WriteOptions
{
    std::string role;
    std::optional<music::Key> key;                  // the song's: figures go into it
    int bars = 0;                                   // 0: the figure's own length
    std::vector<GridNote> partner;                  // a part it goes with, steps from the start of its bars
    int partnerBars = 0;
    std::string partnerRole;
    std::string relation;                           // answer, interlock or together; empty for the style's way
    const VoiceLibrary* notesFrom = nullptr;        // another style's notes and register (rhythms from the first)
    int variation = 0;                              // which of the style's figures to start from
    int count = 3;
};

/** A part written from a style's figures, never a copy: its rhythm from the style's figures for
    the role (or a partner's, played together), its notes moved into the song's key, varied, and
    fitted to a partner (in its gaps, or answering it after its phrase). */
struct WrittenVoice
{
    std::vector<GridNote> notes;
    int bars = 1;
    std::string relation;
    std::string from;       // what it was made from, in words
};

std::vector<WrittenVoice> writeVoices (const VoiceLibrary& rhythmsFrom, const WriteOptions&);

} // namespace gk::listening
