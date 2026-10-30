#include "Reverb.h"

namespace chamber
{

using namespace gkp::dsp;

namespace
{
    // Mutually prime-ish lengths in milliseconds, spread over a few octaves so no mode rings out.
    constexpr float baseMs[Reverb::lines] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 61.9f, 71.3f, 79.7f };
    constexpr float diffuserMs[4] { 4.76f, 3.58f, 12.73f, 9.30f };
    constexpr float earlyMs[6] { 7, 11, 17, 23, 31, 41 };
    constexpr float earlyGain[6] { 0.8f, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f };
}

void Reverb::prepare (double rate)
{
    sampleRate = rate;

    for (int c = 0; c < 2; ++c)
    {
        predelay[c].prepare (static_cast<int> (rate * 0.35));

        for (size_t i = 0; i < 4; ++i)
            diffusers[c][i].prepare (static_cast<int> (diffuserMs[i] * (1 + c * 0.07f) * 0.001f * rate), 0.5f);
    }

    for (size_t i = 0; i < lines; ++i)
    {
        network[i].prepare (static_cast<int> (baseMs[i] * 2.2f * 0.001f * rate) + 64);
        lfoPhases[i] = i / static_cast<double> (lines);
    }

    follower.prepare (rate, 5, 250);
    mixSmoothed.prepare (rate, 30);
    sizeSmoothed.prepare (rate, 120);
    sizeSmoothed.snap (s.size);
    mixSmoothed.snap (s.mix);
    lengthsFor = s.size;
    updateLengths (s.size);
    reset();
}

void Reverb::reset()
{
    for (auto& p : predelay) p.clear();
    for (auto& side : diffusers) for (auto& a : side) a.line.clear();
    for (auto& n : network) n.clear();
    for (auto& d : dampers) d.reset();
    for (auto& v : damped) v = 0;
    for (int c = 0; c < 2; ++c) { lowCut1[c].reset(); lowCut2[c].reset(); highCut[c].reset(); }
}

void Reverb::updateLengths (float size)
{
    const auto scale = 0.4f + 1.6f * juce::jlimit (0.0f, 1.0f, size);

    for (size_t i = 0; i < lines; ++i)
    {
        lengths[i] = baseMs[i] * scale * 0.001f * static_cast<float> (sampleRate);
        // Down 60 dB after `decay` seconds, whatever the line's length.
        gains[i] = s.freeze ? 1.0f : std::pow (10.0f, -3.0f * lengths[i] / static_cast<float> (sampleRate) / std::max (0.05f, s.decay));
    }
}

void Reverb::setSettings (const Settings& settings)
{
    s = settings;
    mixSmoothed.set (s.mix);
    sizeSmoothed.set (s.size);
    updateLengths (lengthsFor < 0 ? s.size : lengthsFor);

    for (auto& d : dampers)
        d.set (sampleRate, s.freeze ? 20000.0f : s.damping);

    for (int c = 0; c < 2; ++c)
    {
        lowCut1[c].set (sampleRate, s.lowCut);
        lowCut2[c].set (sampleRate, s.lowCut);
        highCut[c].set (sampleRate, s.highCut);

        for (auto& a : diffusers[c])
            a.gain = 0.3f + 0.45f * s.diffusion;
    }
}

void Reverb::process (float& left, float& right)
{
    const auto dryL = left, dryR = right;
    const auto level = follower.process (0.5f * (std::abs (left) + std::abs (right)));
    const auto size = sizeSmoothed.next();

    if (std::abs (size - lengthsFor) > 0.002f)
    {
        lengthsFor = size;
        updateLengths (size);
    }

    // Pre-delay, then the early reflections and the diffused input.
    const auto pre = static_cast<float> (s.predelayMs * 0.001 * sampleRate) + 1;
    predelay[0].push (s.freeze ? 0.0f : left);
    predelay[1].push (s.freeze ? 0.0f : right);
    float in[2], earlyOut[2] {};

    for (int c = 0; c < 2; ++c)
    {
        in[c] = predelay[c].read (pre);

        for (size_t t = 0; t < 6; ++t)
            earlyOut[c] += predelay[c].read (pre + earlyMs[t] * (0.5f + size) * (1 + c * 0.11f) * 0.001f * static_cast<float> (sampleRate)) * earlyGain[t];

        earlyOut[c] *= 0.25f;
        auto x = in[c];

        for (auto& a : diffusers[c])
            x = a.process (x);

        in[c] = x;
    }

    // The network: read each line (swaying a little), damp it, feed the mix of all back in.
    std::array<float, lines> out {};
    float sum = 0;

    for (size_t i = 0; i < lines; ++i)
    {
        lfoPhases[i] = wrap (lfoPhases[i] + s.modRate / sampleRate);
        const auto sway = static_cast<float> (std::sin (twoPi * lfoPhases[i])) * s.modDepth * 1.5f * 0.001f * static_cast<float> (sampleRate);
        out[i] = dampers[i].lowpass (network[i].read (lengths[i] + sway));
        sum += out[i];
    }

    const auto reflect = 2.0f / lines * sum;

    for (size_t i = 0; i < lines; ++i)
        network[i].push (in[i % 2] * 0.5f + (out[i] - reflect) * gains[i]);

    float wetL = 0, wetR = 0;

    for (size_t i = 0; i < lines; ++i)
        (i % 2 == 0 ? wetL : wetR) += out[i];

    wetL = wetL * 0.35f + earlyOut[0] * s.early;
    wetR = wetR * 0.35f + earlyOut[1] * s.early;

    for (int c = 0; c < 2; ++c)
    {
        auto& w = c == 0 ? wetL : wetR;
        w = lowCut2[c].highpass (lowCut1[c].highpass (w));
        w = highCut[c].lowpass (w);
    }

    // Width, ducking and the mix.
    const auto mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * s.width;
    wetL = mid + side;
    wetR = mid - side;
    const auto duckGain = 1 - s.duck * juce::jlimit (0.0f, 1.0f, level * 4);
    const auto mix = mixSmoothed.next();
    const auto wetGain = std::sin (mix * juce::MathConstants<float>::halfPi) * duckGain;
    const auto dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
    left = dryL * dryGain + wetL * wetGain;
    right = dryR * dryGain + wetR * wetGain;
}

} // namespace chamber
