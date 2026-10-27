#pragma once

#include <optional>
#include <vector>

namespace gk::listening
{

/** The timing feel of a stretch of drums, from its onsets. */
struct Groove
{
    double tempo = 0;               // BPM
    std::optional<double> swing;    // like the song's swing: 0 straight, 1 a dotted-16th feel; unknown without off-beat 16ths
    double density = 0;             // onsets per beat
};

/** The tempo (BPM, 70 to 190) of some stretches of the same music, from their onsets (seconds
    from each stretch's start, each stretch up to seconds long); 0 with too few onsets. */
double estimateTempo (const std::vector<std::vector<double>>& stretches, double seconds);

/** Measures the groove of onsets (seconds) over a stretch of seconds. With a known tempo (a
    render's) only the swing and density are measured; otherwise the tempo is found between
    70 and 190 BPM. */
Groove measureGroove (const std::vector<double>& onsets, double seconds, std::optional<double> knownTempo = {});

} // namespace gk::listening
