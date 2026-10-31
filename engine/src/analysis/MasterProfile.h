#pragma once

#include "Analysis.h"
#include "model/JsonIO.h"

#include <array>

namespace gk::analysis
{

/** What a master sounds like, measured so two can be compared whatever their level: how loud and
    how dense it is, its tonal balance in third octaves, how much each of four bands still moves
    (its punch), how wide each band is, and where the bass turns mono. Measured on a drop (a
    stretch where everything plays), so a song and a reference are compared like for like. */
struct MasterProfile
{
    /** The four bands a multiband compressor would split it into: below 120 Hz, to 1 kHz, to 6 kHz,
        above. */
    static constexpr std::array<double, 3> crossovers { 120, 1000, 6000 };
    static constexpr size_t numBands = 4;

    float lufs = silenceDb;                 // integrated
    float truePeak = silenceDb;             // dBTP
    float shortTermMax = silenceDb;         // the loudest 3 s
    float range = 0;                        // loudness range, LU

    /** Third octaves from 25 Hz to 16 kHz, dB against the balance's own average from 60 Hz to
        10 kHz, so only its shape counts. */
    std::vector<float> tone;

    /** Each band's 50 ms levels: the loud ones (95th percentile) against the typical (median), dB.
        Less is more compressed. */
    std::array<float, numBands> punch {};

    /** Each band's typical 50 ms level (median, dBFS), for setting a compressor's threshold. */
    std::array<float, numBands> level {};

    /** Each band's sides against its middle, dB (-60 is mono). */
    std::array<float, numBands> width {};

    /** The third octave the sides start in: below it the master is mono (0 if it never is). */
    float monoBelowHz = 0;

    float peakToLoudness() const { return truePeak - lufs; }

    static double toneHz (size_t band);     // a third octave's centre
    static const char* bandName (size_t band);

    model::Json toJson() const;
    static MasterProfile fromJson (const model::Json&);

    /** The average of several (the loudness and peaks by their median), for a style's references. */
    static MasterProfile average (const std::vector<MasterProfile>&);
};

MasterProfile measureMaster (const Audio&);

/** The profile's parts in words, the biggest differences first: "2.5 dB more around 60 Hz",
    "the lows 3 dB punchier", "narrower above 6 kHz". Each with its size, so a caller can weigh
    them against a tolerance. */
struct MasterDifference
{
    std::string what;
    float size = 0;     // in dB or LU, positive: the song has more of it than the reference
};

std::vector<MasterDifference> compareMasters (const MasterProfile& song, const MasterProfile& reference);

} // namespace gk::analysis
