#pragma once

#include <cmath>
#include <filesystem>
#include <optional>

namespace gk
{

/** How an audio clip plays its file. */
struct ClipSource
{
    double start = 0;               // seconds into the file
    std::optional<double> end;      // seconds into the file; unset is its end
    bool reverse = false;           // the part between start and end, backwards
    double speed = 1;               // > 1 plays faster, pitch kept (a 120 BPM loop at 128/120 fits a 128 BPM song)
    double semitones = 0;           // pitch shift, length kept

    bool isWholeFile() const        { return start <= 0 && ! end && ! reverse && std::abs (speed - 1) < 1.0e-6 && std::abs (semitones) < 1.0e-6; }
};

/** The part of a file an audio clip plays, prepared offline and cached: cut to start..end,
    reversed, then time-stretched and pitch-shifted with Rubber Band (R3). Returns the source
    itself when nothing changes it.

    A stretched result is exactly length / speed long and aligned with the source, which
    real-time stretching in the playback graph doesn't guarantee.
    Throws std::runtime_error if the source can't be read.
*/
std::filesystem::path getClipFile (const std::filesystem::path& source, const ClipSource&, const std::filesystem::path& cacheDir);

/** Where prepared files are cached unless the caller chooses otherwise:
    GROOVEKIT_CACHE_DIR, or ~/Library/Caches/GrooveKit. */
std::filesystem::path getDefaultCacheDir();

} // namespace gk
