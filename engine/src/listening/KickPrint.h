#pragma once

#include "model/JsonIO.h"

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

namespace gk::listening
{

/** A kick's whole sound over its first 400 ms, to compare kicks the way they're heard rather than
    by a few numbers: its level in quarter-octave bands over ten stretches of time (finer at the
    hit, where the click and the pitch dive are), against its own loudest, and the pitch of its
    body as it falls. Measured from where the hit starts, so a library one-shot and a kick rebuilt
    from a reference track line up. */
struct KickPrint
{
    static constexpr size_t numBands = 35;      // quarter octaves from 30 Hz to 13 kHz
    static constexpr size_t numFrames = 10;     // ms: 0-5, 5-12, 12-25, 25-45, 45-70, 70-100, 100-150, 150-220, 220-300, 300-400
    static constexpr size_t numSweep = 7;       // ms: 3-12, 12-25, 25-45, 45-70, 70-100, 100-150, 150-220

    std::vector<float> cells;                   // frame by frame, each band's dB against the loudest, -60 the floor
    int frames = 0;                             // frames heard: fewer when the next kick came before 400 ms
    std::vector<float> sweep;                   // the body's pitch (Hz) in each stretch, 0 where it can't be heard
    float bodyHz = 0;                           // where the pitch settles, 45 to 150 ms in

    float at (size_t frame, size_t band) const  { return cells[frame * numBands + band]; }

    static double frameEndMs (size_t frame);
    static double bandHz (size_t band);         // the band's centre

    model::Json toJson() const;
    static std::optional<KickPrint> fromJson (const model::Json&);
};

/** The print of a kick in mono audio, from its first hit's start; seconds is how much of it
    belongs to the hit (the next one may come before 400 ms). None in silence. */
std::optional<KickPrint> printKick (const std::vector<float>& mono, double sampleRate, double seconds = 0.4);

/** How far apart two prints are once the candidate is tuned to the reference's body (the sampler
    retunes a kick, so its pitch alone counts little). Each cost is about 1 where they're clearly
    apart: the level in the bands at the hit (the first 45 ms: click, attack and the start of the
    dive) and over the body after it, weighted to the kick's own range (the mids and highs later
    on are more often a reference's hats than its kick), and the pitch dive. With referenceRumbles,
    the reference's body after 100 ms is mostly its rumble's and counts less. */
struct PrintComparison
{
    double semitones = 0;                       // the candidate's body against the reference's
    float early = 0, body = 0;
    std::optional<float> sweep;
};

PrintComparison comparePrints (const KickPrint& candidate, const KickPrint& reference, bool referenceRumbles = false);

/** How much each part of a kick's match counts, learned from the user's picks: each pick ranks the
    chosen kick before the ones passed over. */
struct MatchWeights
{
    enum Part { early, body, sweep, tuning, measured, fingerprint, numParts };

    /** Each part's cost (about 1 clearly apart), where known. */
    using Costs = std::array<std::optional<float>, numParts>;

    std::array<float, numParts> weights { 1.5f, 1.5f, 0.4f, 0.4f, 0.3f, 0.3f };
    int picks = 0;

    /** 0-100, 100 the same. */
    float score (const Costs&) const;

    /** One step towards ranking chosen before passed. */
    void learn (const Costs& chosen, const Costs& passed);

    static const char* partName (Part);

    model::Json toJson() const;
    static MatchWeights fromJson (const model::Json&);

    /** <app data>/kick-weights.json */
    static std::filesystem::path getDefaultFile();
};

} // namespace gk::listening
