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

/** Reads a file, or its first maxSeconds. Throws std::runtime_error if it can't be read. */
Audio loadAudio (const std::filesystem::path&, std::optional<double> maxSeconds = {});

/** A time range in seconds (an empty end means "to the end"), optionally limited to one channel. */
struct Range
{
    double start = 0;
    std::optional<double> end;
    std::optional<int> channel;
};

constexpr float silenceDb = -200.0f;

/** A copy of the range (all channels unless the range names one). */
Audio slice (const Audio&, Range);

/** Sample peak over the range's channels, in dBFS. */
float peakDb (const Audio&, Range = {});

/** RMS over the range's channels, in dBFS (a full-scale square wave is 0 dB). */
float rmsDb (const Audio&, Range = {});

struct LoudnessStats
{
    float integrated = silenceDb;       // LUFS, BS.1770-4 gating
    float momentaryMax = silenceDb;     // loudest 400 ms
    float shortTermMax = silenceDb;     // loudest 3 s
    float range = 0;                    // loudness range (LRA, EBU Tech 3342) in LU
};

LoudnessStats measureLoudness (const Audio&);

/** Integrated loudness per ITU-R BS.1770-4 / EBU R128, in LUFS. Returns silenceDb if everything is
    gated. Cheaper than measureLoudness when the other statistics aren't needed. */
float integratedLufs (const Audio&);

/** Peak including inter-sample peaks (4x oversampled), in dBTP. */
float truePeakDb (const Audio&);

struct SpectrumBand
{
    double fromHz, toHz;
    float db;       // share of the total energy, in dB (0 dB would be everything in this band)
};

/** The ten bands the balance is measured in, low to high (Hz). */
const std::vector<std::pair<double, double>>& bandEdges();

/** Energy in ten bands from 20 Hz to 20 kHz, of the mono mix, relative to the total. */
std::vector<SpectrumBand> spectralBalance (const Audio&);

/** The same bands as absolute energy (mean power in dB), so different files can be compared. */
std::vector<SpectrumBand> bandEnergies (const Audio&);

/** Both band measurements from one pass over the file. */
struct Bands
{
    std::vector<SpectrumBand> energies;     // as bandEnergies gives them
    std::vector<SpectrumBand> balance;      // as spectralBalance gives them
};

Bands measureBands (const Audio&);

/** What a detailed analysis measures, for callers that need the numbers as well as the report. */
struct Measurements
{
    LoudnessStats loudness;
    float truePeakDb = silenceDb;
    Bands bands;
};

Measurements measure (const Audio&);

struct StereoStats
{
    float correlation = 1;      // 1 mono, 0 unrelated, negative out of phase
    float lowCorrelation = 1;   // the same below 150 Hz, where it should stay near 1
    float sideToMidDb = silenceDb;
};

StereoStats measureStereo (const Audio&);

/** Times (seconds) where the signal rises above thresholdDb after at least minGapSeconds below it. */
std::vector<double> detectOnsets (const Audio&, float thresholdDb = -40.0f, double minGapSeconds = 0.05);

/** Summary used by the CLI, the API and the render tests. Detailed adds true peak, loudness
    statistics, spectral balance and stereo measurements. */
model::Json analyse (const Audio&, bool detailed = false);

/** The detailed summary from measurements taken already. */
model::Json analyse (const Audio&, const Measurements&);

} // namespace gk::analysis
