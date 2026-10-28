#pragma once

#include "SampleCache.h"

#include <optional>
#include <string>

namespace gk::sampler
{

/** Effects worked into a sample once, before it plays (see the model's SampleProcessing), with
    the layer's transient shaping. */
struct Processing
{
    std::optional<float> trimDb;
    bool removeDc = false;
    float stretchSemitones = 0, stretchLength = 1;
    float pitchBend = 0;
    float ringMix = 0, ringHz = 440;
    float eq = 0;
    float cutoff = 20000, resonance = 0;
    float boostDb = 0;
    bool clip = false;
    float punch = 0, tail = 0;
    float reverb = 0;
    bool largeReverb = false;
    float stereoDelayMs = 0;
    bool fadeStereo = false;

    /** Changes nothing. */
    bool isNeutral() const;

    /** The same for the same settings, for caching. */
    std::string key() const;
};

/** The sample with the processing worked in. Its timeScale says how much longer the sound got
    (stretched, bent), not counting a reverb tail added after it. */
std::shared_ptr<Sample> process (const Sample&, const Processing&);

} // namespace gk::sampler
