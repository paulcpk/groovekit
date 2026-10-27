#include "Groove.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace gk::listening
{

namespace
{
    /** How strongly the onsets lock to a grid of this period: 1 when every onset is on it. */
    double lock (const std::vector<double>& onsets, double period)
    {
        std::complex<double> sum;

        for (auto t : onsets)
            sum += std::polar (1.0, 2.0 * std::numbers::pi * t / period);

        return std::abs (sum) / static_cast<double> (onsets.size());
    }
}

double estimateTempo (const std::vector<std::vector<double>>& stretches, double seconds)
{
    // Autocorrelation of each stretch's onsets on a 10 ms grid, scored at a beat and its
    // multiples (so the beat wins over the bar and the 8th), summed over the stretches with each
    // weighing the same.
    constexpr double step = 0.01;
    std::vector<std::vector<double>> trains;

    for (auto& onsets : stretches)
    {
        if (onsets.size() < 4)
            continue;

        std::vector<double> train (static_cast<size_t> (seconds / step) + 1, 0.0);

        for (auto t : onsets)
            if (auto i = static_cast<size_t> (t / step); i < train.size())
                train[i] = 1.0 / static_cast<double> (onsets.size());

        trains.push_back (std::move (train));
    }

    if (trains.empty())
        return 0;

    auto correlation = [&] (const std::vector<double>& train, double lagSeconds)
    {
        const auto lag = static_cast<size_t> (std::lround (lagSeconds / step));
        double sum = 0;

        for (size_t i = 0; i + lag < train.size(); ++i)
            sum += train[i] * (train[i + lag] + 0.5 * ((i + lag > 0 ? train[i + lag - 1] : 0.0) + (i + lag + 1 < train.size() ? train[i + lag + 1] : 0.0)));

        return sum;
    };

    auto score = [&] (double bpm)
    {
        const auto beat = 60.0 / bpm;
        double sum = 0;

        for (auto& train : trains)
            sum += correlation (train, beat) + 0.5 * correlation (train, 2 * beat) + 0.25 * correlation (train, 4 * beat);

        return sum;
    };

    double best = 0, bestScore = -1;

    for (double bpm = 70; bpm <= 190; bpm += 0.5)
    {
        // Dance music mostly sits here; outside it a tempo has to be clearly better.
        const auto s = score (bpm) * (bpm < 100 || bpm > 160 ? 0.85 : 1.0);

        if (s > bestScore)
            bestScore = s, best = bpm;
    }

    // Half time looks as regular as the beat when every 16th has a hit: below 95 BPM, double
    // time wins unless it's clearly worse. Above 160, three quarters of it (a dotted-8th pulse
    // mistaken for the beat) wins the same way.
    if (best < 95 && score (2 * best) >= 0.7 * score (best))
        best *= 2;
    else if (best > 160 && score (0.75 * best) >= 0.8 * score (best))
        best *= 0.75;

    // Refine against the onsets themselves.
    double refined = best, bestLock = -1;

    for (double bpm = best - 1.0; bpm <= best + 1.0; bpm += 0.05)
    {
        double l = 0;

        for (auto& onsets : stretches)
            if (! onsets.empty())
                l += lock (onsets, 60.0 / bpm / 4.0);

        if (l > bestLock)
            bestLock = l, refined = bpm;
    }

    return std::round (refined * 10.0) / 10.0;
}

Groove measureGroove (const std::vector<double>& onsets, double seconds, std::optional<double> knownTempo)
{
    Groove g;

    if (onsets.size() < 8 || seconds <= 0)
        return g;

    g.tempo = knownTempo ? *knownTempo : estimateTempo ({ onsets }, seconds);

    if (g.tempo <= 0)
        return g;

    const auto sixteenth = 60.0 / g.tempo / 4.0, eighth = 2.0 * sixteenth;
    g.density = static_cast<double> (onsets.size()) / (seconds / (60.0 / g.tempo));

    // Where an onset falls within its 8th (0..1) for a grid starting at phase.
    auto within = [&] (double t, double phase) { return std::fmod (std::fmod (t - phase, eighth) + eighth, eighth) / eighth; };
    auto onBeat = [] (double r) { return r < 0.06 || r > 0.94; };
    auto offBeat = [] (double r) { return r >= 0.45 && r < 0.8; };  // halfway or later: swing only delays

    // The grid that puts the most onsets either on an 8th or on a (possibly late) 16th after it.
    // Taking the late ones as off-beats tells a swung pattern from one that's early everywhere.
    double phase = 0;
    int bestCount = -1;

    for (int step = 0; step < 96; ++step)
    {
        const auto candidate = eighth * step / 96.0;
        int count = 0;

        for (auto t : onsets)
            if (const auto r = within (t, candidate); onBeat (r) || offBeat (r))
                ++count;

        if (count > bestCount)
            bestCount = count, phase = candidate;
    }

    // Centre the grid on the on-beat onsets.
    std::complex<double> onSum;

    for (auto t : onsets)
        if (const auto r = within (t, phase); onBeat (r))
            onSum += std::polar (1.0, 2.0 * std::numbers::pi * r);

    phase += std::arg (onSum) / (2.0 * std::numbers::pi) * eighth;

    // Where the off-beat 16ths land within their 8th: halfway when straight, later with swing.
    // Taken from where they bunch up, so stray onsets (ghost notes, tails) don't drag it around.
    std::vector<double> offBeats;

    for (auto t : onsets)
        if (const auto r = within (t, phase); offBeat (r))
            offBeats.push_back (r);

    constexpr double width = 0.035;
    double peak = 0;
    size_t peakCount = 0;

    for (double centre = 0.47; centre <= 0.78; centre += 0.005)
    {
        const auto count = static_cast<size_t> (std::count_if (offBeats.begin(), offBeats.end(), [&] (double r) { return std::abs (r - centre) <= width; }));

        if (count > peakCount)
            peakCount = count, peak = centre;
    }

    // A real bunch: several onsets, and clearly more than an even spread would put there.
    const auto even = static_cast<double> (offBeats.size()) * (2 * width) / 0.35;

    if (peakCount >= 4 && static_cast<double> (peakCount) >= 1.5 * even)
    {
        double sum = 0;
        int count = 0;

        for (auto r : offBeats)
            if (std::abs (r - peak) <= width)
                sum += r, ++count;

        g.swing = std::clamp ((sum / count - 0.5) * 4.0, 0.0, 1.0);
    }

    return g;
}

} // namespace gk::listening
