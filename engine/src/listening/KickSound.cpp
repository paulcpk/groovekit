#include "KickSound.h"

#include "analysis/Analysis.h"
#include "library/SampleFeatures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <sstream>

namespace gk::listening
{

using model::Json;

namespace
{
    constexpr float floorDb = -90.0f;
    constexpr double bandEdges[] = { 30, 60, 120, 250, 500, 1000, 2000, 4000, 12000 };
    constexpr size_t numBands = std::size (bandEdges) - 1;

    std::vector<float> filtered (std::vector<float> x, double rate, double hz, bool highPass)
    {
        for (auto q : { 0.5412, 1.3066 })
        {
            juce::IIRFilter f;
            f.setCoefficients (highPass ? juce::IIRCoefficients::makeHighPass (rate, hz, q) : juce::IIRCoefficients::makeLowPass (rate, hz, q));
            f.processSamples (x.data(), static_cast<int> (x.size()));
        }

        return x;
    }

    std::vector<float> band (const std::vector<float>& x, double rate, double lowHz, double highHz)
    {
        return filtered (filtered (x, rate, lowHz, true), rate, std::min (highHz, rate * 0.45), false);
    }

    double power (const std::vector<float>& x, double rate, double from, double to)
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

    float db (double p)
    {
        return p > 1e-12 ? static_cast<float> (10.0 * std::log10 (p)) : floorDb;
    }

    template <typename T>
    T median (std::vector<T> v)
    {
        if (v.empty())
            return {};

        std::nth_element (v.begin(), v.begin() + static_cast<long> (v.size() / 2), v.end());
        return v[v.size() / 2];
    }

    double round (double v, double step)
    {
        return std::round (v / step) * step;
    }

    const char* bandName (size_t i)
    {
        static const char* names[] = { "40 Hz", "85 Hz", "170 Hz", "350 Hz", "700 Hz", "1.4 kHz", "3 kHz", "7 kHz" };
        return names[std::min (i, numBands - 1)];
    }
}

std::string noteName (double hz)
{
    if (hz <= 0)
        return "?";

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto midi = static_cast<int> (std::lround (69 + 12 * std::log2 (hz / 440.0)));
    return std::string (names[((midi % 12) + 12) % 12]) + std::to_string (midi / 12 - 1);
}

//==============================================================================
std::optional<KickSound> measureKick (const juce::AudioBuffer<float>& audio, double rate, KickTiming timing)
{
    const auto n = static_cast<size_t> (audio.getNumSamples());

    if (n < static_cast<size_t> (0.05 * rate))
        return {};

    std::vector<float> mid (n), side (n, 0.0f);

    for (size_t i = 0; i < n; ++i)
    {
        const auto l = audio.getSample (0, static_cast<int> (i));
        const auto r = audio.getSample (audio.getNumChannels() > 1 ? 1 : 0, static_cast<int> (i));
        mid[i] = 0.5f * (l + r);
        side[i] = 0.5f * (l - r);
    }

    const auto low = filtered (mid, rate, 150, false);
    const auto mids = band (mid, rate, 150, 1500);
    const auto high = band (mid, rate, 2000, 8000);
    std::vector<std::vector<float>> bands;

    for (size_t b = 0; b < numBands; ++b)
        bands.push_back (band (mid, rate, bandEdges[b], bandEdges[b + 1]));

    // The hits: given, or the strong ones in the low end.
    auto peakNear = [&] (double t)
    {
        float peak = 0;

        for (auto i = static_cast<int64_t> (t * rate); i < static_cast<int64_t> ((t + 0.04) * rate) && i < static_cast<int64_t> (n); ++i)
            if (i >= 0)
                peak = std::max (peak, std::abs (low[static_cast<size_t> (i)]));

        return peak;
    };

    auto hits = timing.hits;

    if (hits.empty())
    {
        analysis::Audio a;
        a.sampleRate = rate;
        a.buffer.setSize (1, static_cast<int> (n));
        a.buffer.copyFrom (0, 0, low.data(), static_cast<int> (n));
        hits = library::detectTransients (a);

        // A one-shot starts with its hit, whatever the detector makes of the first milliseconds.
        if (hits.empty() || hits.front() > 0.03)
            hits.insert (hits.begin(), 0.0);
    }

    std::sort (hits.begin(), hits.end());
    float strongest = 0;

    for (auto t : hits)
        strongest = std::max (strongest, peakNear (t));

    if (strongest < 1e-4f)
        return {};

    std::vector<double> strong;

    for (auto t : hits)
        if (peakNear (t) >= strongest * 0.25f && (strong.empty() || t - strong.back() > 0.08))
            strong.push_back (t);

    if (strong.size() > 64)
        strong.resize (64);

    const auto seconds = static_cast<double> (n) / rate;
    std::vector<double> bodies, attacks, decays, widths;
    std::vector<float> clicks, grits, rumbles, rumbleGrits;
    std::vector<std::vector<float>> spectra (numBands);

    for (size_t k = 0; k < strong.size(); ++k)
    {
        const auto h = strong[k];
        const auto next = k + 1 < strong.size() ? strong[k + 1] : seconds;
        const auto end = std::min (next, h + timing.beat.value_or (0.8));

        // The peak of the low end, and how long until it's 12 dB down (a 5 ms window).
        const auto from = static_cast<int64_t> (h * rate);
        auto peakAt = from;
        float peak = 0;

        for (auto i = from; i < static_cast<int64_t> ((h + 0.04) * rate) && i < static_cast<int64_t> (n); ++i)
            if (i >= 0 && std::abs (low[static_cast<size_t> (i)]) > peak)
                peak = std::abs (low[static_cast<size_t> (i)]), peakAt = i;

        attacks.push_back (std::max (0.0, (static_cast<double> (peakAt) / rate - h) * 1000));
        const auto peakPower = power (low, rate, static_cast<double> (peakAt) / rate - 0.0025, static_cast<double> (peakAt) / rate + 0.0025);
        double decayed = end;

        for (double t = static_cast<double> (peakAt) / rate; t < end; t += 0.0025)
        {
            if (db (power (low, rate, t, t + 0.005)) < db (peakPower) - 12)
            {
                decayed = t;
                break;
            }
        }

        decays.push_back ((decayed - static_cast<double> (peakAt) / rate) * 1000);

        // The body's note, from the low end's zero crossings.
        const auto a = static_cast<int64_t> ((h + 0.04) * rate), b = std::min (static_cast<int64_t> (std::min (h + 0.14, end) * rate), static_cast<int64_t> (n));
        int crossings = 0;

        for (auto i = a + 1; i < b; ++i)
            crossings += (low[static_cast<size_t> (i - 1)] < 0) != (low[static_cast<size_t> (i)] < 0) ? 1 : 0;

        if (b - a > static_cast<int64_t> (0.03 * rate))
        {
            const auto hz = crossings / 2.0 / (static_cast<double> (b - a) / rate);

            if (hz > 25 && hz < 250)
                bodies.push_back (hz);
        }

        const auto body = power (low, rate, h, h + 0.06);
        clicks.push_back (db (power (high, rate, h, h + 0.02)) - db (body));
        grits.push_back (db (power (mids, rate, h + 0.01, h + 0.12)) - db (power (low, rate, h + 0.01, h + 0.12)));

        if (timing.beat)
        {
            const auto r0 = h + 0.45 * *timing.beat, r1 = std::min (h + 0.9 * *timing.beat, next);

            if (r1 - r0 > 0.02)
            {
                const auto between = power (low, rate, r0, r1);
                rumbles.push_back (db (between) - db (body));
                rumbleGrits.push_back (db (power (mids, rate, r0, r1)) - db (between));
            }
        }

        widths.push_back (std::sqrt (power (side, rate, h, h + 0.2) / std::max (power (mid, rate, h, h + 0.2), 1e-12)));

        for (size_t i = 0; i < numBands; ++i)
            spectra[i].push_back (db (power (bands[i], rate, h, std::min (end, h + 0.25))));
    }

    KickSound k;
    k.hits = static_cast<int> (strong.size());
    k.bodyHz = round (median (bodies), 0.1);
    k.attackMs = round (median (attacks), 0.1);
    k.decayMs = round (median (decays), 1);
    k.clickDb = static_cast<float> (round (median (clicks), 0.1));
    k.gritDb = static_cast<float> (round (median (grits), 0.1));
    k.width = static_cast<float> (round (std::clamp (median (widths), 0.0, 1.0), 0.01));

    // Only the kick's own decay between the kicks (more than 24 dB under the hit) is no rumble:
    // rumbles sit 5 to 15 dB under it.
    if (! rumbles.empty() && median (rumbles) > -24)
    {
        k.rumbleDb = static_cast<float> (round (median (rumbles), 0.1));
        k.rumbleGritDb = static_cast<float> (round (median (rumbleGrits), 0.1));
    }

    for (auto& s : spectra)
        k.spectrum.push_back (median (s));

    const auto top = *std::max_element (k.spectrum.begin(), k.spectrum.end());

    for (auto& s : k.spectrum)
        s = static_cast<float> (round (std::max (s - top, -60.0f), 0.1));

    // Where it plays in the bar, from all the hits.
    if (timing.beat && timing.downbeat)
    {
        const auto step = *timing.beat / 4;
        const auto bars = std::max (1, static_cast<int> (std::lround ((seconds - *timing.downbeat) / (16 * step))));
        std::array<int, 16> counts {};

        for (auto t : strong)
        {
            const auto s = static_cast<long> (std::lround ((t - *timing.downbeat) / step));
            ++counts[static_cast<size_t> (((s % 16) + 16) % 16)];
        }

        for (auto c : counts)
            k.pattern += c * 2 >= bars ? 'x' : '.';
    }

    return k;
}

//==============================================================================
Json KickSound::toJson() const
{
    Json j { { "bodyHz", bodyHz }, { "attackMs", attackMs }, { "decayMs", decayMs }, { "clickDb", clickDb }, { "gritDb", gritDb },
             { "width", width }, { "spectrum", spectrum }, { "hits", hits } };

    if (rumbleDb)       j["rumbleDb"] = *rumbleDb;
    if (rumbleGritDb)   j["rumbleGritDb"] = *rumbleGritDb;
    if (! pattern.empty()) j["pattern"] = pattern;

    return j;
}

KickSound KickSound::fromJson (const Json& j)
{
    KickSound k;
    k.bodyHz = j.value ("bodyHz", 0.0);
    k.attackMs = j.value ("attackMs", 0.0);
    k.decayMs = j.value ("decayMs", 0.0);
    k.clickDb = j.value ("clickDb", -60.0f);
    k.gritDb = j.value ("gritDb", -60.0f);
    k.width = j.value ("width", 0.0f);
    k.spectrum = j.value ("spectrum", std::vector<float>());
    k.hits = j.value ("hits", 0);
    k.pattern = j.value ("pattern", std::string());

    if (j.contains ("rumbleDb"))     k.rumbleDb = j["rumbleDb"].get<float>();
    if (j.contains ("rumbleGritDb")) k.rumbleGritDb = j["rumbleGritDb"].get<float>();

    return k;
}

std::string KickSound::describe() const
{
    std::ostringstream out;
    out.setf (std::ios::fixed);
    out.precision (0);

    if (bodyHz > 0)
        out << "body " << bodyHz << " Hz (" << noteName (bodyHz) << "), ";

    out << "rings " << decayMs << " ms, attack ";
    out.precision (1);
    out << attackMs << " ms, click " << clickDb << " dB, grit " << gritDb << " dB";

    if (rumbleDb)
        out << ", rumble " << *rumbleDb << " dB under the hit (grit " << rumbleGritDb.value_or (0) << " dB)";

    if (width > 0.15f)
        out << ", width " << width;

    if (! pattern.empty())
        out << ", plays " << pattern;

    return out.str();
}

//==============================================================================
Json KickComparison::toJson() const
{
    return { { "score", std::round (score) }, { "differences", differences }, { "notes", notes } };
}

KickComparison compareKicks (const KickSound& c, const KickSound& r)
{
    struct Cost { double weight, cost; std::string words; };
    std::vector<Cost> costs;
    KickComparison result;
    auto& d = result.differences;
    d = Json::object();

    auto signedWords = [] (double v, const char* unit, const char* more, const char* less, int decimals = 0)
    {
        std::ostringstream out;
        out.setf (std::ios::fixed);
        out.precision (decimals);
        out << std::abs (v) << unit << " " << (v > 0 ? more : less);
        return out.str();
    };

    if (c.bodyHz > 0 && r.bodyHz > 0)
    {
        const auto st = 12 * std::log2 (c.bodyHz / r.bodyHz);
        d["semitones"] = round (st, 0.1);
        costs.push_back ({ 1.5, std::abs (st) / 3, signedWords (st, " semitones", "higher", "lower", 1) + " (" + noteName (c.bodyHz) + " against " + noteName (r.bodyHz) + ")" });
    }

    if (c.decayMs > 0 && r.decayMs > 0)
    {
        const auto ms = c.decayMs - r.decayMs;
        d["decayMs"] = round (ms, 1);
        costs.push_back ({ 1.5, std::abs (std::log2 (c.decayMs / r.decayMs)), "the body rings " + signedWords (ms, " ms", "longer", "shorter") });
    }

    d["attackMs"] = round (c.attackMs - r.attackMs, 0.1);
    costs.push_back ({ 0.5, std::abs (c.attackMs - r.attackMs) / 10, "a " + signedWords (c.attackMs - r.attackMs, " ms", "slower", "faster", 1) + " attack" });

    d["clickDb"] = round (c.clickDb - r.clickDb, 0.1);
    costs.push_back ({ 1.0, std::abs (c.clickDb - r.clickDb) / 10.0, signedWords (c.clickDb - r.clickDb, " dB", "more", "less", 1) + " click" });

    d["gritDb"] = round (c.gritDb - r.gritDb, 0.1);
    costs.push_back ({ 1.0, std::abs (c.gritDb - r.gritDb) / 10.0, signedWords (c.gritDb - r.gritDb, " dB", "more", "less", 1) + " grit (distortion)" });

    if (r.rumbleDb && c.rumbleDb)
    {
        d["rumbleDb"] = round (*c.rumbleDb - *r.rumbleDb, 0.1);
        costs.push_back ({ 1.0, std::abs (*c.rumbleDb - *r.rumbleDb) / 10.0, "the rumble " + signedWords (*c.rumbleDb - *r.rumbleDb, " dB", "louder", "quieter", 1) });

        if (r.rumbleGritDb && c.rumbleGritDb)
        {
            d["rumbleGritDb"] = round (*c.rumbleGritDb - *r.rumbleGritDb, 0.1);
            costs.push_back ({ 0.7, std::abs (*c.rumbleGritDb - *r.rumbleGritDb) / 10.0,
                               "the rumble " + signedWords (*c.rumbleGritDb - *r.rumbleGritDb, " dB", "grittier", "cleaner", 1) });
        }
    }
    else if (r.rumbleDb && ! c.rumbleDb && *r.rumbleDb > -24)
    {
        std::ostringstream out;
        out.setf (std::ios::fixed);
        out.precision (0);
        out << "no rumble of its own: the reference's sits " << -*r.rumbleDb << " dB under the hit (make it from the kick: reverb, distortion and a low-pass, ducked by the kick)";
        result.notes.push_back (out.str());
    }

    // The kick's own bands, up to 2 kHz: above, hats and claps over a reference's stretch (or in a
    // drum loop) would be counted as the kick's. The click covers the top.
    if (c.spectrum.size() == r.spectrum.size() && ! c.spectrum.empty())
    {
        double sum = 0;
        size_t worst = 0;
        auto bands = Json::array();
        const auto kickBands = std::min<size_t> (c.spectrum.size(), 6);

        for (size_t i = 0; i < kickBands; ++i)
        {
            const auto diff = c.spectrum[i] - r.spectrum[i];
            bands.push_back (round (diff, 0.1));
            sum += std::abs (diff);

            if (std::abs (diff) > std::abs (c.spectrum[worst] - r.spectrum[worst]))
                worst = i;
        }

        d["spectrumDb"] = bands;
        const auto worstDiff = c.spectrum[worst] - r.spectrum[worst];
        costs.push_back ({ 1.2, sum / static_cast<double> (kickBands) / 6, signedWords (worstDiff, " dB", "more", "less", 1) + " around " + bandName (worst) });
    }

    d["width"] = round (c.width - r.width, 0.01);
    costs.push_back ({ 0.3, std::abs (c.width - r.width) / 0.3, c.width > r.width ? "wider" : "narrower" });

    double weighted = 0, total = 0;

    for (auto& cost : costs)
        weighted += cost.weight * std::min (cost.cost, 3.0), total += cost.weight;

    result.score = static_cast<float> (100 * std::exp (-weighted / std::max (total, 1e-9)));

    // The differences that matter, the biggest first.
    std::sort (costs.begin(), costs.end(), [] (auto& x, auto& y) { return x.weight * x.cost > y.weight * y.cost; });
    std::vector<std::string> words;

    for (auto& cost : costs)
        if (cost.cost > 0.3 && words.size() < 3)
            words.push_back (cost.words);

    result.notes.insert (result.notes.begin(), words.begin(), words.end());
    return result;
}

} // namespace gk::listening
