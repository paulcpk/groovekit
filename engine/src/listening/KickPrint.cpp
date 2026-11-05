#include "KickPrint.h"

#include "Dsp.h"
#include "EngineContext.h"
#include "KickSound.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace gk::listening
{

using model::Json;

namespace
{
    constexpr double frameEdgesMs[] = { 0, 5, 12, 25, 45, 70, 100, 150, 220, 300, 400 };
    constexpr double sweepEdgesMs[] = { 3, 12, 25, 45, 70, 100, 150, 220 };
    constexpr float printFloor = -60;
    constexpr double preRoll = 0.02;          // a rebuilt kick starts this long before its hit

    template <typename T>
    T median (std::vector<T> v)
    {
        if (v.empty())
            return {};

        std::nth_element (v.begin(), v.begin() + static_cast<long> (v.size() / 2), v.end());
        return v[v.size() / 2];
    }

    /** Where a hit starts: the first sample within 20 dB of the loudest in its first half second,
        a millisecond early. */
    size_t onsetOf (const std::vector<float>& x, double rate)
    {
        const auto look = std::min (x.size(), static_cast<size_t> (0.5 * rate));
        float peak = 0;

        for (size_t i = 0; i < look; ++i)
            peak = std::max (peak, std::abs (x[i]));

        for (size_t i = 0; i < look; ++i)
            if (std::abs (x[i]) >= 0.1f * peak)
                return i > static_cast<size_t> (0.001 * rate) ? i - static_cast<size_t> (0.001 * rate) : 0;

        return 0;
    }

    /** The pitch of a low-passed signal between two times (seconds): the median of the half
        periods between its zero crossings (placed between samples) centred there, so a low
        body is heard in a stretch shorter than its period. 0 where it's too quiet to tell. */
    float pitchBetween (const std::vector<double>& crossings, const std::vector<float>& low, double rate, double from, double to, double quietPower)
    {
        if (power (low, rate, from, to) < quietPower)
            return 0;

        std::vector<double> hz;

        for (size_t k = 1; k < crossings.size(); ++k)
        {
            const auto centre = (crossings[k] + crossings[k - 1]) / 2 / rate;

            if (centre >= from && centre < to)
                hz.push_back (rate / (2 * (crossings[k] - crossings[k - 1])));
        }

        const auto f = median (hz);
        return f > 20 && f < 1000 ? static_cast<float> (f) : 0.0f;
    }
}

double KickPrint::frameEndMs (size_t frame)
{
    return frameEdgesMs[std::min (frame + 1, numFrames)];
}

double KickPrint::bandHz (size_t band)
{
    return 30 * std::pow (2.0, (static_cast<double> (band) + 0.5) / 4);
}

//==============================================================================
std::optional<KickPrint> printKick (const std::vector<float>& mono, double rate, double seconds)
{
    if (mono.size() < static_cast<size_t> (0.01 * rate))
        return {};

    const auto onset = onsetOf (mono, rate);
    const auto pre = static_cast<size_t> (std::min (static_cast<double> (onset), preRoll * rate));

    // From a little before the hit (the filters settle there) to 400 ms after, silence past its end.
    const auto length = static_cast<size_t> ((preRoll + 0.4) * rate);
    // A hit sooner than the pre-roll starts that much later in it (silence before), so less fits after.
    std::vector<float> x (length, 0.0f);
    const auto begin = onset - pre;
    const auto at = static_cast<size_t> (preRoll * rate) - pre;
    const auto keep = std::min (length - at, mono.size() - begin);
    std::copy_n (mono.begin() + static_cast<long> (begin), keep, x.begin() + static_cast<long> (at));

    const auto t0 = preRoll;
    KickPrint p;
    p.frames = static_cast<int> (std::count_if (std::begin (frameEdgesMs) + 1, std::end (frameEdgesMs), [&] (double ms) { return ms <= seconds * 1000 + 1; }));
    p.cells.assign (KickPrint::numFrames * KickPrint::numBands, printFloor);

    std::vector<float> levels (KickPrint::numFrames * KickPrint::numBands, printFloor);
    float loudest = -200;

    for (size_t b = 0; b < KickPrint::numBands; ++b)
    {
        const auto low = 30 * std::pow (2.0, static_cast<double> (b) / 4), high = 30 * std::pow (2.0, static_cast<double> (b + 1) / 4);

        if (low >= rate * 0.45)
            continue;

        const auto y = band (x, rate, low, high);

        for (size_t f = 0; f < KickPrint::numFrames; ++f)
        {
            const auto level = db (power (y, rate, t0 + frameEdgesMs[f] / 1000, t0 + frameEdgesMs[f + 1] / 1000));
            levels[f * KickPrint::numBands + b] = level;

            if (static_cast<int> (f) < p.frames)
                loudest = std::max (loudest, level);
        }
    }

    if (loudest < -100)
        return {};

    for (size_t i = 0; i < levels.size(); ++i)
        p.cells[i] = std::round (std::max (levels[i] - loudest, printFloor) * 2) / 2;

    // The pitch as it falls, on the low end (the dive starts a few hundred hertz up).
    const auto low = filtered (x, rate, 600, false);
    std::vector<double> crossings;

    for (size_t i = static_cast<size_t> (t0 * rate); i < low.size(); ++i)
        if ((low[i - 1] < 0) != (low[i] < 0))
            crossings.push_back (static_cast<double> (i - 1) + low[i - 1] / (low[i - 1] - low[i]));

    double loudestPower = 0;

    for (size_t f = 0; f + 1 < std::size (sweepEdgesMs); ++f)
        loudestPower = std::max (loudestPower, power (low, rate, t0 + sweepEdgesMs[f] / 1000, t0 + sweepEdgesMs[f + 1] / 1000));

    for (size_t w = 0; w < KickPrint::numSweep; ++w)
    {
        const auto end = sweepEdgesMs[w + 1];
        p.sweep.push_back (end <= seconds * 1000 + 1 ? std::round (pitchBetween (crossings, low, rate, t0 + sweepEdgesMs[w] / 1000, t0 + end / 1000, loudestPower * 1e-4))
                                                     : 0.0f);
    }

    std::vector<float> settled;

    for (size_t w = 3; w < 6; ++w)
        if (p.sweep[w] > 0)
            settled.push_back (p.sweep[w]);

    if (settled.empty())
        for (auto hz : p.sweep)
            if (hz > 0)
                settled = { hz };

    p.bodyHz = median (settled);
    return p;
}

//==============================================================================
PrintComparison comparePrints (const KickPrint& c, const KickPrint& r, bool referenceRumbles)
{
    PrintComparison result;

    if (c.bodyHz > 0 && r.bodyHz > 0)
        result.semitones = 12 * std::log2 (static_cast<double> (c.bodyHz) / r.bodyHz);

    // Tuned: the candidate's bands moved by the semitones it's retuned (a band is three).
    const auto shift = std::clamp (result.semitones, -12.0, 12.0) / 3;

    auto tuned = [&] (size_t frame, size_t b)
    {
        const auto from = static_cast<double> (b) + shift;
        const auto i = static_cast<long> (std::floor (from));
        const auto t = static_cast<float> (from - static_cast<double> (i));

        auto cell = [&] (long k) { return k >= 0 && k < static_cast<long> (KickPrint::numBands) ? c.at (frame, static_cast<size_t> (k)) : printFloor; };
        return cell (i) * (1 - t) + cell (i + 1) * t;
    };

    // The kick's own range counts all through; the mids while the body sounds, the highs only at
    // the click.
    auto weight = [referenceRumbles] (size_t frame, size_t b)
    {
        const auto hz = KickPrint::bandHz (b), ms = KickPrint::frameEndMs (frame);
        const auto w = hz < 250 ? 1.0 : hz < 2000 ? (ms <= 150 ? 0.6 : 0.2) : (ms <= 25 ? 0.6 : 0.1);
        return frame < 4 ? w * 1.5 : referenceRumbles && ms > 100 ? w * 0.2 : w;
    };

    const auto frames = static_cast<size_t> (std::min (c.frames, r.frames));
    double sums[2] = {}, weights[2] = {};

    for (size_t f = 0; f < frames; ++f)
    {
        for (size_t b = 0; b < KickPrint::numBands; ++b)
        {
            const auto x = tuned (f, b), y = r.at (f, b);
            const auto diff = x < -45 && y < -45 ? 0.0 : std::abs (std::max (x, -50.0f) - std::max (y, -50.0f));
            const auto w = weight (f, b);
            sums[f < 4 ? 0 : 1] += w * diff;
            weights[f < 4 ? 0 : 1] += w;
        }
    }

    // About 6 dB apart on average is clearly another kick.
    result.early = weights[0] > 0 ? static_cast<float> (sums[0] / weights[0] / 6) : 1.0f;
    result.body = weights[1] > 0 ? static_cast<float> (sums[1] / weights[1] / 6) : result.early;

    // The dive, both tuned to the body: about 4 semitones apart on average is clearly another.
    double sweep = 0;
    int known = 0;

    for (size_t w = 0; w < std::min (c.sweep.size(), r.sweep.size()); ++w)
    {
        if (c.sweep[w] <= 0 || r.sweep[w] <= 0)
            continue;

        sweep += std::abs (12 * std::log2 (static_cast<double> (c.sweep[w]) / r.sweep[w]) - result.semitones);
        ++known;
    }

    if (known >= 2)
        result.sweep = static_cast<float> (sweep / known / 4);

    return result;
}

//==============================================================================
Json KickPrint::toJson() const
{
    std::vector<int> halves;

    for (auto v : cells)
        halves.push_back (static_cast<int> (std::lround (v * 2)));

    return { { "cells", halves }, { "frames", frames }, { "sweep", sweep }, { "bodyHz", bodyHz } };
}

std::optional<KickPrint> KickPrint::fromJson (const Json& j)
{
    const auto halves = j.value ("cells", std::vector<int>());

    if (halves.size() != numFrames * numBands)
        return {};

    KickPrint p;

    for (auto h : halves)
        p.cells.push_back (static_cast<float> (h) / 2);

    p.frames = std::clamp (j.value ("frames", 0), 0, static_cast<int> (numFrames));
    p.sweep = j.value ("sweep", std::vector<float>());
    p.sweep.resize (numSweep, 0.0f);
    p.bodyHz = j.value ("bodyHz", 0.0f);
    return p;
}

//==============================================================================
float MatchWeights::score (const Costs& costs) const
{
    double sum = 0, total = 0;

    for (size_t i = 0; i < numParts; ++i)
        if (costs[i])
            sum += weights[i] * std::min (*costs[i], 3.0f), total += weights[i];

    return total > 0 ? static_cast<float> (100 * std::exp (-sum / total)) : 0.0f;
}

void MatchWeights::learn (const Costs& chosen, const Costs& passed)
{
    // A pairwise logistic step: the weighted cost of the chosen one should be the lower.
    std::array<float, numParts> diff {};
    double margin = 0, total = 0;

    for (size_t i = 0; i < numParts; ++i)
    {
        if (! chosen[i] || ! passed[i])
            continue;

        diff[i] = std::min (*chosen[i], 3.0f) - std::min (*passed[i], 3.0f);
        margin += weights[i] * diff[i];
        total += weights[i];
    }

    if (total <= 0)
        return;

    const auto wrong = 1 / (1 + std::exp (-4 * margin / total));

    for (size_t i = 0; i < numParts; ++i)
        weights[i] = std::clamp (weights[i] - static_cast<float> (0.15 * wrong) * diff[i], 0.05f, 4.0f);
}

const char* MatchWeights::partName (Part p)
{
    static const char* names[] = { "early", "body", "sweep", "tuning", "measured", "fingerprint" };
    return names[p];
}

Json MatchWeights::toJson() const
{
    Json w = Json::object();

    for (size_t i = 0; i < numParts; ++i)
        w[partName (static_cast<Part> (i))] = weights[i];

    return { { "weights", w }, { "picks", picks } };
}

MatchWeights MatchWeights::fromJson (const Json& j)
{
    MatchWeights m;
    const auto w = j.value ("weights", Json::object());

    for (size_t i = 0; i < numParts; ++i)
        m.weights[i] = std::clamp (w.value (partName (static_cast<Part> (i)), m.weights[i]), 0.05f, 4.0f);

    m.picks = j.value ("picks", 0);
    return m;
}

std::filesystem::path MatchWeights::getDefaultFile()
{
    return EngineContext::getAppDataFolder() / "kick-weights.json";
}

//==============================================================================
std::optional<RebuiltKick> rebuildKick (const juce::AudioBuffer<float>& audio, double rate, const KickTiming& timing)
{
    auto hits = kickHits (audio, rate, timing);

    if (hits.size() < 3)
        return {};

    const auto n = static_cast<size_t> (audio.getNumSamples());
    std::vector<float> mid (n);

    for (size_t i = 0; i < n; ++i)
        mid[i] = 0.5f * (audio.getSample (0, static_cast<int> (i)) + audio.getSample (audio.getNumChannels() > 1 ? 1 : 0, static_cast<int> (i)));

    // As long as the hits are apart, to 400 ms.
    double gap = 0.4;

    for (size_t k = 1; k < hits.size(); ++k)
        gap = std::min (gap, hits[k] - hits[k - 1] - 0.005);

    if (gap < 0.08)
        return {};

    const auto pre = static_cast<int64_t> (preRoll * rate);
    const auto length = static_cast<int64_t> ((preRoll + gap) * rate);
    const auto slack = static_cast<int64_t> (0.01 * rate);

    // A grid can take a slow kick's hit a sixteenth or two late, in its body: each hit moved back
    // to where the rise to its loudest began (the level below 1 kHz under a fifth of that), at
    // most 300 ms earlier and after the hit before.
    {
        const auto below = filtered (mid, rate, 1000, false);
        std::vector<float> envelope (n);
        float e = 0;
        const auto fall = static_cast<float> (std::exp (-1 / (0.003 * rate)));

        for (size_t i = 0; i < n; ++i)
            envelope[i] = e = std::max (std::abs (below[i]), e * fall);

        double previous = -1;

        for (auto& h : hits)
        {
            const auto at = static_cast<int64_t> (h * rate);
            const auto from = std::max ({ int64_t { 0 }, at - static_cast<int64_t> (0.3 * rate), static_cast<int64_t> ((previous + 0.05) * rate) });
            const auto to = std::min<int64_t> (at + static_cast<int64_t> (0.03 * rate), static_cast<int64_t> (n));
            previous = h;

            if (to <= from)
                continue;

            auto peak = from;

            for (auto i = from; i < to; ++i)
                if (envelope[static_cast<size_t> (i)] > envelope[static_cast<size_t> (peak)])
                    peak = i;

            auto start = peak;

            while (start > from && envelope[static_cast<size_t> (start)] > 0.2f * envelope[static_cast<size_t> (peak)])
                --start;

            h = static_cast<double> (start) / rate;
        }
    }

    // Hits whose whole stretch is in the audio.
    hits.erase (std::remove_if (hits.begin(), hits.end(), [&] (double h)
    {
        const auto at = static_cast<int64_t> (h * rate);
        return at - pre - slack < 0 || at - pre + length + slack > static_cast<int64_t> (n);
    }), hits.end());

    if (hits.size() < 3)
        return {};

    // Lined up to the sample on the low end and lower mids (the click's top is often a hat's too):
    // each hit against the one with the middle level.
    const auto guide = filtered (mid, rate, 1000, false);
    const auto bright = band (mid, rate, 1000, 5000);
    const auto low = filtered (mid, rate, 150, false);
    const auto span = static_cast<int64_t> (0.08 * rate);

    auto levelOf = [&] (const std::vector<float>& x, int64_t at, int64_t count)
    {
        double sum = 0;

        for (auto i = at; i < at + count; ++i)
            sum += static_cast<double> (x[static_cast<size_t> (i)]) * x[static_cast<size_t> (i)];

        return sum / static_cast<double> (std::max<int64_t> (1, count));
    };

    std::vector<double> levels;

    for (auto h : hits)
        levels.push_back (levelOf (low, static_cast<int64_t> (h * rate), span));

    auto order = levels;
    std::nth_element (order.begin(), order.begin() + static_cast<long> (order.size() / 2), order.end());
    const auto middle = static_cast<size_t> (std::distance (levels.begin(), std::find (levels.begin(), levels.end(), order[order.size() / 2])));
    const auto anchor = static_cast<int64_t> (hits[middle] * rate);

    struct Hit { int64_t start; float gain; float brightness; };
    std::vector<Hit> lined;

    for (auto h : hits)
    {
        const auto at = static_cast<int64_t> (h * rate);
        int64_t bestLag = 0;
        double best = -1e30;

        for (auto lag = -slack; lag <= slack; ++lag)
        {
            double dot = 0;

            for (int64_t i = -pre / 2; i < span; ++i)
                dot += static_cast<double> (guide[static_cast<size_t> (anchor + i)]) * guide[static_cast<size_t> (at + lag + i)];

            if (dot > best)
                best = dot, bestLag = lag;
        }

        const auto start = at + bestLag;
        const auto gain = std::sqrt (levelOf (low, anchor, span) / std::max (levelOf (low, start, span), 1e-12));
        lined.push_back ({ start, static_cast<float> (std::clamp (gain, 0.25, 4.0)), db (levelOf (bright, start, span) * gain * gain) });
    }

    // Leave out hits much brighter than the quieter ones (a clap or a snare on them).
    std::vector<float> brightness;

    for (auto& h : lined)
        brightness.push_back (h.brightness);

    std::sort (brightness.begin(), brightness.end());
    const auto limit = brightness[brightness.size() / 4] + 3;
    std::vector<Hit> kept;

    for (auto& h : lined)
        if (h.brightness <= limit)
            kept.push_back (h);

    if (kept.size() < 3)
        kept = lined;

    RebuiltKick kick;
    kick.seconds = gap;
    kick.hits = static_cast<int> (kept.size());
    kick.audio.resize (static_cast<size_t> (length));
    std::vector<float> values (kept.size());

    for (int64_t i = 0; i < length; ++i)
    {
        for (size_t k = 0; k < kept.size(); ++k)
            values[k] = mid[static_cast<size_t> (kept[k].start - pre + i)] * kept[k].gain;

        std::nth_element (values.begin(), values.begin() + static_cast<long> (values.size() / 2), values.end());
        kick.audio[static_cast<size_t> (i)] = values[values.size() / 2];
    }

    // Faded at the end, where the next hit would come.
    const auto fade = std::min<int64_t> (static_cast<int64_t> (0.005 * rate), length);

    for (int64_t i = 0; i < fade; ++i)
        kick.audio[static_cast<size_t> (length - 1 - i)] *= static_cast<float> (i) / static_cast<float> (fade);

    return kick;
}

//==============================================================================
MatchWeights::Costs kickMatchCosts (const KickSound& c, const KickSound& r, std::optional<float> fingerprint)
{
    MatchWeights::Costs costs;
    double semitones = 0;

    if (c.print && r.print)
    {
        const auto p = comparePrints (*c.print, *r.print, r.rumbleDb && *r.rumbleDb > -15);
        costs[MatchWeights::early] = p.early;
        costs[MatchWeights::body] = p.body;
        costs[MatchWeights::sweep] = p.sweep;
        semitones = p.semitones;
    }
    else if (c.bodyHz > 0 && r.bodyHz > 0)
    {
        semitones = 12 * std::log2 (c.bodyHz / r.bodyHz);
    }

    // Retuning an octave is clearly another kick; a few semitones hardly.
    costs[MatchWeights::tuning] = static_cast<float> (std::abs (semitones) / 12);

    // The measurements as if it were tuned: their pitch counts in tuning.
    auto tuned = c;

    if (std::abs (semitones) <= 12 && r.bodyHz > 0)
        tuned.bodyHz = r.bodyHz;

    costs[MatchWeights::measured] = static_cast<float> (-std::log (std::max (compareKicks (tuned, r).score, 1.0f) / 100));

    if (fingerprint)
        costs[MatchWeights::fingerprint] = (100 - *fingerprint) / 100 * 1.5f;

    return costs;
}

} // namespace gk::listening
