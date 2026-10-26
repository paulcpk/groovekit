#pragma once

#include "analysis/Analysis.h"

namespace gk::library
{

/** What a sample sounds like, measured from its audio. */
struct SampleFeatures
{
    double durationSeconds = 0;
    double sampleRate = 0;
    int channels = 0;

    float peakDb = analysis::silenceDb;
    float rmsDb = analysis::silenceDb;
    std::optional<float> integratedLufs;    // only for files of 400 ms or more

    double attackMs = 0;                    // start to loudest point
    double decayMs = 0;                     // loudest point until 30 dB quieter (or the end)

    double centroidHz = 0;                  // spectral centre of mass: low for kicks, high for hats

    /** Share of spectral energy: sub < 60 Hz, low < 250 Hz, mid < 2 kHz, high above. Sums to 1. */
    struct Bands
    {
        float sub = 0, low = 0, mid = 0, high = 0;
    } bands;

    /** Fundamental, when the sample has a clear one. */
    struct Pitch
    {
        double hz;
        int key;            // nearest MIDI note
        double cents;       // offset from that note, -50..50
        float confidence;   // 0..1
    };

    std::optional<Pitch> pitch;

    int onsetCount = 0;
    std::optional<double> estimatedTempo;   // for loops: from onsets, checked against bar-length candidates
};

/** Analyses up to maxSeconds of the audio. */
SampleFeatures analyseSample (const analysis::Audio&, double maxSeconds = 30);

/** Times (seconds) where the level jumps, even within a dense mix: drum hits in a loop or a recording. */
std::vector<double> detectTransients (const analysis::Audio&, double resolutionSeconds = 0.001);

/** Fundamental frequency of the mono mix in [start, start + window) using YIN. */
std::optional<SampleFeatures::Pitch> detectPitch (const analysis::Audio&, int startSample, int windowSamples);

model::Json toJson (const SampleFeatures&);

} // namespace gk::library
