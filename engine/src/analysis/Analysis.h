#pragma once

#include "model/JsonIO.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <filesystem>
#include <optional>

namespace gk::analysis
{

struct Audio
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0;

    double getDurationSeconds() const { return buffer.getNumSamples() / sampleRate; }
};

/** Throws std::runtime_error if the file can't be read. */
Audio loadAudio (const std::filesystem::path&);

/** A time range in seconds (an empty end means "to the end"), optionally limited to one channel. */
struct Range
{
    double start = 0;
    std::optional<double> end;
    std::optional<int> channel;
};

constexpr float silenceDb = -200.0f;

/** Sample peak over the range's channels, in dBFS. */
float peakDb (const Audio&, Range = {});

/** RMS over the range's channels, in dBFS (a full-scale square wave is 0 dB). */
float rmsDb (const Audio&, Range = {});

/** Integrated loudness per ITU-R BS.1770-4 / EBU R128, in LUFS. Returns silenceDb if everything is gated. */
float integratedLufs (const Audio&);

/** Times (seconds) where the signal rises above thresholdDb after at least minGapSeconds below it. */
std::vector<double> detectOnsets (const Audio&, float thresholdDb = -40.0f, double minGapSeconds = 0.05);

/** Summary used by the CLI and the render tests. */
model::Json analyse (const Audio&);

} // namespace gk::analysis
