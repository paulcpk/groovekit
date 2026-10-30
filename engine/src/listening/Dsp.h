#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace gk::listening
{

/** The small measuring tools the hearing shares (arrangements, kicks, styles): band filters
    and levels on a mono signal. Header-only so each use inlines the same arithmetic. */

constexpr float floorDb = -90.0f;

/** A 4th-order Butterworth low- or high-pass, as two biquads. */
inline std::vector<float> filtered (std::vector<float> x, double rate, double hz, bool highPass)
{
    for (auto q : { 0.5412, 1.3066 })
    {
        juce::IIRFilter f;
        f.setCoefficients (highPass ? juce::IIRCoefficients::makeHighPass (rate, hz, q) : juce::IIRCoefficients::makeLowPass (rate, hz, q));
        f.processSamples (x.data(), static_cast<int> (x.size()));
    }

    return x;
}

/** A band of it: high-passed at lowHz, then low-passed at highHz (kept under the Nyquist
    frequency, for bands near the top of a low sample rate). */
inline std::vector<float> band (const std::vector<float>& x, double rate, double lowHz, double highHz)
{
    return filtered (filtered (x, rate, lowHz, true), rate, std::min (highHz, rate * 0.45), false);
}

/** Mean square between two times (seconds). */
inline double power (const std::vector<float>& x, double rate, double from, double to)
{
    const auto n = static_cast<int64_t> (x.size());
    const auto a = std::clamp (static_cast<int64_t> (from * rate), int64_t { 0 }, n);
    const auto b = std::clamp (static_cast<int64_t> (to * rate), int64_t { 0 }, n);

    if (b <= a)
        return 0;

    double sum = 0;

    for (auto i = a; i < b; ++i)
        sum += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];

    return sum / static_cast<double> (b - a);
}

/** A power in dB, floored for silence. */
inline float db (double p)
{
    return p > 1e-12 ? static_cast<float> (10.0 * std::log10 (p)) : floorDb;
}

} // namespace gk::listening
