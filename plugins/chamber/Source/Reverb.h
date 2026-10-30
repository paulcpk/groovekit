#pragma once

#include "Dsp.h"

#include <array>

namespace chamber
{

struct Settings
{
    float size = 0.5f, decay = 2.5f, predelayMs = 10, damping = 7000, lowCut = 80, highCut = 16000, diffusion = 0.7f;
    float modRate = 0.5f, modDepth = 0.3f, early = 0.4f, width = 1, duck = 0, mix = 0.35f;
    bool freeze = false;
};

/** The reverb: pre-delay, early reflections, input diffusion, then a network of eight delay
    lines feeding each other through a Householder matrix, each damped and slowly modulated,
    with the decay set by the time they take to fall 60 dB. The wet signal is filtered, widened,
    ducked under the input and mixed in with equal power. */
class Reverb
{
public:
    static constexpr int lines = 8;

    void prepare (double sampleRate);
    void reset();
    void setSettings (const Settings&);
    void process (float& left, float& right);

    /** The input's level lately, 0-1, for the window. */
    float inputLevel() const    { return follower.value; }

private:
    double sampleRate = 48000;
    Settings s;
    gkp::dsp::DelayLine predelay[2];
    std::array<gkp::dsp::Allpass, 4> diffusers[2];
    std::array<gkp::dsp::DelayLine, lines> network;
    std::array<float, lines> lengths {}, gains {}, damped {};
    std::array<gkp::dsp::OnePole, lines> dampers;
    std::array<double, lines> lfoPhases {};
    gkp::dsp::OnePole lowCut1[2], lowCut2[2], highCut[2];
    gkp::dsp::Follower follower;
    gkp::dsp::Smoothed mixSmoothed, sizeSmoothed;
    float lengthsFor = -1;

    void updateLengths (float size);
};

} // namespace chamber
