#pragma once

#include "model/JsonIO.h"

#include <optional>
#include <string>
#include <vector>

namespace gk::listening
{

/** How a whole track unfolds, bar by bar: the beat grid, which parts play in each bar, the mix's
    energy, the sections, and the changes that keep it alive. Heard from its stems. */
struct Arrangement
{
    /** The parts heard, in score order. Kick and rumble come from the drums' and bass's low end
        (rumble: the low end between the kicks), hats and percussion from the drums' highs and
        mids, bass, music and vocal from their own stems. */
    static const std::vector<std::string>& partNames();

    double tempo = 0;
    double downbeat = 0;            // seconds to the first bar's first beat
    double seconds = 0;             // the track's length
    int bars = 0;

    /** A part's level in each bar (dB) and whether it plays there. */
    struct Lane
    {
        std::vector<float> level;
        std::vector<bool> on;
    };

    std::map<std::string, Lane> parts;

    /** The mix in each bar: loudness (dB), the low end below 120 Hz (dB), brightness (above 4 kHz
        against the whole, dB) and density (hits a beat in the highs). */
    std::vector<float> loudness, low, brightness, density;

    /** Bars from 0, end excluded. role: intro, groove, build, break, drop or outro. */
    struct Section
    {
        int start = 0, end = 0;
        std::string role;
    };

    std::vector<Section> sections;

    /** Something that changes inside a section: a part out for a bar, a filter opening. */
    struct Change
    {
        int bar = 0;
        std::string part, what;
    };

    std::vector<Change> changes;

    double barSeconds() const   { return tempo > 0 ? 4 * 60 / tempo : 0; }

    /** The parts playing in most of a section's bars, in score order. */
    std::vector<std::string> partsIn (const Section&) const;

    model::Json toJson() const;
    static Arrangement fromJson (const model::Json&);

    /** The arrangement as a score: the sections with their bars, energy and parts, then the
        changes. For the agent and for people. */
    std::string describe() const;
};

/** A whole track's stems, mono, at one rate. */
struct TrackStems
{
    double sampleRate = 44100;
    std::vector<float> mix, drums, bass, other, vocals;
};

/** Hears how a track is arranged from its stems: the tempo (unless known) and the bar grid from
    the kick, then each bar's parts and energy, the sections and the changes. */
Arrangement hearArrangement (const TrackStems&, std::optional<double> knownTempo = {});

} // namespace gk::listening
