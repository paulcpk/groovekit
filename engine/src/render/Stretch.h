#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

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
    bool resample = false;          // the speed changes the pitch with it instead
    double semitones = 0;           // pitch shift, length kept

    bool isWholeFile() const        { return start <= 0 && ! end && ! reverse && std::abs (speed - 1) < 1.0e-6 && std::abs (semitones) < 1.0e-6; }
};

/** The part of a file an audio clip plays, prepared offline and cached: cut to start..end,
    reversed, then time-stretched and pitch-shifted with Rubber Band (R3), or resampled to its
    speed and pitch-shifted. Returns the source itself when nothing changes it.

    A stretched result is exactly length / speed long and aligned with the source, which
    real-time stretching in the playback graph doesn't guarantee.
    Throws std::runtime_error if the source can't be read.
*/
std::filesystem::path getClipFile (const std::filesystem::path& source, const ClipSource&, const std::filesystem::path& cacheDir);

/** Audio time-stretched (speed > 1 is shorter) and pitch-shifted with Rubber Band (R3,
    offline): exactly length / speed long, aligned with the input. */
juce::AudioBuffer<float> stretchAudio (const juce::AudioBuffer<float>&, double sampleRate, double speed, double semitones);

/** Audio played faster (speed > 1, higher and shorter) or slower, like a record: exactly
    length / speed long. */
juce::AudioBuffer<float> resampleAudio (const juce::AudioBuffer<float>&, double sampleRate, double speed);

/** Where prepared files are cached unless the caller chooses otherwise:
    GROOVEKIT_CACHE_DIR, or ~/Library/Caches/GrooveKit. */
std::filesystem::path getDefaultCacheDir();

} // namespace gk
