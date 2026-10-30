#include "Delay.h"

namespace echo
{

using namespace gkp::dsp;

namespace
{
    constexpr float maxSeconds = 4.0f;
    constexpr float diffuserMs[2] { 5.3f, 7.9f };
}

void Delay::prepare (double rate)
{
    sampleRate = rate;

    for (int c = 0; c < 2; ++c)
    {
        lines[c].prepare (static_cast<int> (rate * (maxSeconds + 0.1)));
        times[c].prepare (rate, 80);
        times[c].snap (static_cast<float> (s.timeSeconds * rate));
        lowCut[c].set (rate, s.lowCut);
        highCut[c].set (rate, s.highCut);

        for (size_t i = 0; i < 2; ++i)
            diffusers[c][i].prepare (static_cast<int> (diffuserMs[i] * (1 + c * 0.13f) * 0.001f * rate), 0.4f);
    }

    mixSmoothed.prepare (rate, 30);
    mixSmoothed.snap (s.mix);
    follower.prepare (rate, 5, 200);
    reset();
}

void Delay::reset()
{
    for (auto& l : lines) l.clear();
    for (auto& side : diffusers) for (auto& a : side) a.line.clear();
    for (int c = 0; c < 2; ++c) { lowCut[c].reset(); highCut[c].reset(); }
}

void Delay::setSettings (const Settings& settings)
{
    s = settings;
    const auto left = juce::jlimit (0.001f, maxSeconds, s.timeSeconds);
    const auto right = juce::jlimit (0.001f, maxSeconds, s.mode == 2 ? s.timeSeconds * s.ratio : s.timeSeconds);
    times[0].set (static_cast<float> (left * sampleRate));
    times[1].set (static_cast<float> (right * sampleRate));
    mixSmoothed.set (s.mix);

    for (int c = 0; c < 2; ++c)
    {
        lowCut[c].set (sampleRate, s.lowCut);
        highCut[c].set (sampleRate, s.highCut);

        for (auto& a : diffusers[c])
            a.gain = 0.6f * s.diffusion;
    }
}

float Delay::roundTrip (float x, int side)
{
    // What a repeat goes through on its way round: the filters, the tape, the smear.
    x = lowCut[side].highpass (x);
    x = highCut[side].lowpass (x);

    if (s.saturation > 0 || s.freeze)
    {
        const auto drive = 1 + s.saturation * 4;
        x = std::tanh (x * drive) / std::tanh (std::min (3.0f, drive)) * (1 - 0.15f * s.saturation);
    }

    if (s.diffusion > 0)
        for (auto& a : diffusers[side])
            x = a.process (x);

    return x;
}

void Delay::process (float& left, float& right)
{
    const auto dryL = left, dryR = right;
    const auto level = follower.process (0.5f * (std::abs (left) + std::abs (right)));
    wowPhase = wrap (wowPhase + s.wowRate / sampleRate);
    const auto wowSamples = s.wow * 6 * 0.001f * static_cast<float> (sampleRate);
    float wet[2];

    for (int c = 0; c < 2; ++c)
    {
        const auto sway = static_cast<float> (std::sin (twoPi * wrap (wowPhase + c * 0.3))) * wowSamples
                        + static_cast<float> (std::sin (twoPi * wrap (wowPhase * 7.3 + c * 0.5))) * wowSamples * 0.15f;
        wet[c] = lines[c].read (times[c].next() + sway);
    }

    const auto feedback = s.freeze ? 1.0f : s.feedback;
    const auto inL = s.freeze ? 0.0f : left, inR = s.freeze ? 0.0f : right;
    float push[2];

    switch (s.mode)
    {
        case 1:     // ping-pong: in on the left, each repeat crossing over
            push[0] = (inL + inR) * 0.5f + roundTrip (wet[1], 0) * feedback;
            push[1] = roundTrip (wet[0], 1) * feedback;
            break;
        case 2:     // dual: two times, each side its own
            push[0] = inL + roundTrip (wet[0], 0) * feedback;
            push[1] = inR + roundTrip (wet[1], 1) * feedback;
            break;
        default:    // stereo, with some of each side feeding the other
            push[0] = inL + roundTrip (wet[0] * (1 - s.crossfeed) + wet[1] * s.crossfeed, 0) * feedback;
            push[1] = inR + roundTrip (wet[1] * (1 - s.crossfeed) + wet[0] * s.crossfeed, 1) * feedback;
            break;
    }

    for (int c = 0; c < 2; ++c)
        lines[c].push (dc[c].process (push[c]));

    const auto mid = 0.5f * (wet[0] + wet[1]), side = 0.5f * (wet[0] - wet[1]) * s.width;
    wet[0] = mid + side;
    wet[1] = mid - side;
    const auto duckGain = 1 - s.duck * juce::jlimit (0.0f, 1.0f, level * 4);
    const auto mix = mixSmoothed.next();
    const auto wetGain = std::sin (mix * juce::MathConstants<float>::halfPi) * duckGain;
    const auto dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
    left = dryL * dryGain + wet[0] * wetGain;
    right = dryR * dryGain + wet[1] * wetGain;
}

} // namespace echo
