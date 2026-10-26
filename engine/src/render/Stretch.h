#pragma once

#include <filesystem>

namespace gk
{

/** A time-stretched copy of an audio file, pitch kept, rendered offline with Rubber Band (R3) and cached.

    speed > 1 plays faster (a 120 BPM loop at speed 128/120 fits a 128 BPM song).
    The result is exactly length / speed long and aligned with the source, which
    real-time stretching in the playback graph doesn't guarantee.
    Throws std::runtime_error if the source can't be read.
*/
std::filesystem::path getStretchedFile (const std::filesystem::path& source, double speed, const std::filesystem::path& cacheDir);

/** Where stretched files are cached unless the caller chooses otherwise:
    GROOVEKIT_CACHE_DIR, or ~/Library/Caches/GrooveKit. */
std::filesystem::path getDefaultCacheDir();

} // namespace gk
