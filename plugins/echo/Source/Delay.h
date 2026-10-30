#pragma once

#include "Dsp.h"

#include <array>

namespace echo
{

struct Settings
{
    int mode = 0;
    float timeSeconds = 0.375f, ratio = 1, feedback = 0.45f, crossfeed = 0, lowCut = 120, highCut = 6000, saturation = 0.2f;
    float wow = 0.15f, wowRate = 0.5f, diffusion = 0, duck = 0, width = 1, mix = 0.3f;
    bool freeze = false;
};

/** The delay: a line each side read at a time that glides when changed (and sways with the wow),
    the repeats filtered, saturated and smeared on their way round; stereo, bouncing between the
    sides, or two times. The wet signal is widened, ducked under the input and mixed in. */
class Delay
{
public:
    void prepare (double sampleRate);
    void reset();
    void setSettings (const Settings&);
    void process (float& left, float& right);

    float inputLevel() const    { return follower.value; }

private:
    double sampleRate = 48000;
    Settings s;
    gkp::dsp::DelayLine lines[2];
    gkp::dsp::Smoothed times[2], mixSmoothed;
    gkp::dsp::OnePole lowCut[2], highCut[2];
    std::array<gkp::dsp::Allpass, 2> diffusers[2];
    double wowPhase = 0;
    gkp::dsp::Follower follower;
    gkp::dsp::DcBlocker dc[2];

    float roundTrip (float x, int side);
};

} // namespace echo
