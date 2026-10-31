#pragma once

#include "Session.h"
#include "Steps.h"
#include "analysis/MasterProfile.h"
#include "render/Render.h"

#include <map>

namespace tracktion { inline namespace engine { class Engine; } }

namespace gk
{

/** A stretch of a reference track to master like: its drop. */
struct MasterReference
{
    std::filesystem::path file;
    double from = 0, to = 0;        // seconds
    std::string title;
};

struct MatchMasterOptions
{
    /** One track's drop, or several (a style's): their profiles averaged. */
    std::vector<MasterReference> references;

    std::optional<double> targetLufs;           // the reference's loudness unless given
    double ceilingDb = -1;
    double maxEqDb = 6;                         // no EQ band moves further than this

    /** The song's stretch to match, in ticks; its loudest sixteen bars by default. */
    RenderOptions render;
    ExecuteOptions execute;

    /** Where the references' profiles are kept between runs, and the A/B files go. */
    std::filesystem::path cacheFolder;

    /** The drums', bass's and music's levels against the mix (dB), when the listening models are
        here to split it: then what mastering can't fix is said first. */
    std::function<std::optional<std::map<std::string, float>> (const juce::AudioBuffer<float>&, double sampleRate)> partLevels;
};

/** Masters the song like a reference: measures the reference's drop and the song's (loudness and
    density, tonal balance, each band's punch and width, where the bass turns mono), then sets a
    chain on master to close the gap: an EQ ("match-eq", moves capped at maxEqDb, and at 2 dB
    below 250 Hz when the song has no bass part to shape), a multiband compressor that also sets
    each band's width, between half and one and a half times ("match-dynamics"), mono bass where
    the reference has it ("match-stereo") and the master limiter, before any limiter or after the
    master's own effects. Renders the stretch, measures and adjusts, up to five renders, and applies the chain
    as one undoable change. Returns the profiles before and after against the reference's, what
    still differs, the chain, level-matched files to compare by ear (before, after and the
    reference at the same loudness), and the parts' balance against the reference's. */
std::shared_ptr<Steps> matchMasterSteps (tracktion::Engine&, Session&, MatchMasterOptions);

/** Where a track's drop is from its map's arrangement (seconds): the loudest drop section, at most
    sixteen bars of it; else the loudest sixteen bars. */
std::optional<std::pair<double, double>> dropOf (const std::vector<std::string>& roles, const std::vector<std::pair<int, int>>& sections,
                                                 const std::vector<float>& barLoudness, double downbeat, double barSeconds);

inline constexpr const char* matchEqId = "match-eq";
inline constexpr const char* matchDynamicsId = "match-dynamics";
inline constexpr const char* matchStereoId = "match-stereo";

} // namespace gk
