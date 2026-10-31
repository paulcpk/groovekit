#include "MasterProfile.h"

#include "listening/Dsp.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace gk::analysis
{

using model::Json;

namespace
{
    constexpr size_t numTone = 29;          // third octaves, 25 Hz to 16 kHz

    float percentile (std::vector<float> v, double p)
    {
        if (v.empty())
            return silenceDb;

        const auto k = static_cast<size_t> (std::clamp (p, 0.0, 1.0) * static_cast<double> (v.size() - 1));
        std::nth_element (v.begin(), v.begin() + static_cast<long> (k), v.end());
        return v[k];
    }

    float round1 (double v)
    {
        return static_cast<float> (std::round (v * 10) / 10);
    }

    /** Mean power in each third octave of the mid and of the sides, from Hann windows overlapping
        by half. */
    std::pair<std::vector<double>, std::vector<double>> thirdOctaves (const std::vector<float>& mid, const std::vector<float>& side, double rate)
    {
        constexpr int order = 13;
        constexpr size_t size = 1 << order;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window (size, juce::dsp::WindowingFunction<float>::hann, false);
        std::vector<double> m (numTone, 0.0), s (numTone, 0.0);
        std::vector<float> buffer (2 * size);
        int frames = 0;

        auto accumulate = [&] (const std::vector<float>& x, size_t at, std::vector<double>& into)
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            std::copy_n (x.begin() + static_cast<long> (at), size, buffer.begin());
            window.multiplyWithWindowingTable (buffer.data(), size);
            fft.performFrequencyOnlyForwardTransform (buffer.data());

            for (size_t bin = 1; bin < size / 2; ++bin)
            {
                const auto hz = static_cast<double> (bin) * rate / static_cast<double> (size);
                const auto k = static_cast<long> (std::lround (3 * std::log2 (hz / 25)));

                if (k >= 0 && k < static_cast<long> (numTone))
                    into[static_cast<size_t> (k)] += static_cast<double> (buffer[bin]) * buffer[bin];
            }
        };

        for (size_t at = 0; at + size <= mid.size(); at += size / 2, ++frames)
        {
            accumulate (mid, at, m);
            accumulate (side, at, s);
        }

        for (size_t k = 0; k < numTone; ++k)
        {
            m[k] /= std::max (frames, 1);
            s[k] /= std::max (frames, 1);
        }

        return { m, s };
    }

    double powerDb (double p)
    {
        return p > 1e-20 ? 10 * std::log10 (p) : -200.0;
    }
}

double MasterProfile::toneHz (size_t band)
{
    return 25 * std::pow (2.0, static_cast<double> (band) / 3);
}

const char* MasterProfile::bandName (size_t band)
{
    static const char* names[] = { "the lows (below 120 Hz)", "the low mids (120 Hz-1 kHz)", "the high mids (1-6 kHz)", "the highs (above 6 kHz)" };
    return names[std::min (band, numBands - 1)];
}

MasterProfile measureMaster (const Audio& audio)
{
    MasterProfile p;
    const auto n = static_cast<size_t> (audio.buffer.getNumSamples());
    const auto rate = audio.sampleRate;

    if (n < static_cast<size_t> (rate))
        return p;

    const auto loudness = measureLoudness (audio);
    p.lufs = round1 (loudness.integrated);
    p.shortTermMax = round1 (loudness.shortTermMax);
    p.range = round1 (loudness.range);
    p.truePeak = round1 (truePeakDb (audio));

    std::vector<float> mid (n), side (n);
    const auto right = audio.buffer.getNumChannels() > 1 ? 1 : 0;

    for (size_t i = 0; i < n; ++i)
    {
        const auto l = audio.buffer.getSample (0, static_cast<int> (i)), r = audio.buffer.getSample (right, static_cast<int> (i));
        mid[i] = 0.5f * (l + r);
        side[i] = 0.5f * (l - r);
    }

    // The tone, and where the sides start.
    const auto [m, s] = thirdOctaves (mid, side, rate);
    double sum = 0;
    int count = 0;

    for (size_t k = 0; k < numTone; ++k)
        if (MasterProfile::toneHz (k) >= 60 && MasterProfile::toneHz (k) <= 10000)
            sum += powerDb (m[k]), ++count;

    const auto average = count > 0 ? sum / count : 0.0;

    for (size_t k = 0; k < numTone; ++k)
        p.tone.push_back (round1 (std::max (powerDb (m[k]) - average, -60.0)));

    for (size_t k = 1; k + 1 < numTone && MasterProfile::toneHz (k) < 500; ++k)
    {
        if (powerDb (s[k]) - powerDb (m[k]) > -15 && powerDb (s[k + 1]) - powerDb (m[k + 1]) > -15)
        {
            p.monoBelowHz = k <= 1 ? 0.0f : static_cast<float> (std::round (MasterProfile::toneHz (k)));
            break;
        }
    }

    // Each band: how it moves, its level, its width.
    for (size_t b = 0; b < MasterProfile::numBands; ++b)
    {
        auto split = [&] (const std::vector<float>& x)
        {
            if (b == 0)
                return listening::filtered (x, rate, MasterProfile::crossovers[0], false);

            if (b == MasterProfile::numBands - 1)
                return listening::filtered (x, rate, MasterProfile::crossovers[b - 1], true);

            return listening::band (x, rate, MasterProfile::crossovers[b - 1], MasterProfile::crossovers[b]);
        };

        const auto bm = split (mid), bs = split (side);
        std::vector<float> levels;

        for (double t = 0; t + 0.05 <= static_cast<double> (n) / rate; t += 0.05)
            levels.push_back (listening::db (listening::power (bm, rate, t, t + 0.05)));

        const auto top = levels.empty() ? silenceDb : *std::max_element (levels.begin(), levels.end());
        levels.erase (std::remove_if (levels.begin(), levels.end(), [&] (float v) { return v < top - 50; }), levels.end());

        // Levels as a full-scale sine reads them (+3 dB on the mean square).
        p.level[b] = round1 (percentile (levels, 0.5) + 3);
        p.punch[b] = round1 (percentile (levels, 0.95) - percentile (levels, 0.5));

        const auto all = static_cast<double> (n) / rate;
        p.width[b] = round1 (std::max (-60.0f, listening::db (listening::power (bs, rate, 0, all)) - listening::db (listening::power (bm, rate, 0, all))));
    }

    return p;
}

//==============================================================================
Json MasterProfile::toJson() const
{
    return { { "lufs", lufs }, { "truePeak", truePeak }, { "peakToLoudness", round1 (peakToLoudness()) }, { "shortTermMax", shortTermMax },
             { "range", range }, { "tone", tone }, { "punch", punch }, { "level", level }, { "width", width }, { "monoBelowHz", monoBelowHz } };
}

MasterProfile MasterProfile::fromJson (const Json& j)
{
    MasterProfile p;
    p.lufs = j.value ("lufs", silenceDb);
    p.truePeak = j.value ("truePeak", silenceDb);
    p.shortTermMax = j.value ("shortTermMax", silenceDb);
    p.range = j.value ("range", 0.0f);
    p.tone = j.value ("tone", std::vector<float>());
    p.punch = j.value ("punch", p.punch);
    p.level = j.value ("level", p.level);
    p.width = j.value ("width", p.width);
    p.monoBelowHz = j.value ("monoBelowHz", 0.0f);
    return p;
}

MasterProfile MasterProfile::average (const std::vector<MasterProfile>& profiles)
{
    if (profiles.empty())
        return {};

    MasterProfile a;
    const auto count = static_cast<float> (profiles.size());
    std::vector<float> lufs, peaks, shortTerm, ranges, monos;
    a.tone.assign (profiles[0].tone.size(), 0.0f);

    for (auto& p : profiles)
    {
        lufs.push_back (p.lufs), peaks.push_back (p.truePeak), shortTerm.push_back (p.shortTermMax), ranges.push_back (p.range), monos.push_back (p.monoBelowHz);

        for (size_t k = 0; k < a.tone.size() && k < p.tone.size(); ++k)
            a.tone[k] += p.tone[k] / count;

        for (size_t b = 0; b < numBands; ++b)
        {
            a.punch[b] += p.punch[b] / count;
            a.level[b] += p.level[b] / count;
            a.width[b] += p.width[b] / count;
        }
    }

    a.lufs = percentile (lufs, 0.5), a.truePeak = percentile (peaks, 0.5), a.shortTermMax = percentile (shortTerm, 0.5);
    a.range = percentile (ranges, 0.5), a.monoBelowHz = percentile (monos, 0.5);

    for (auto& t : a.tone)
        t = round1 (t);

    for (size_t b = 0; b < numBands; ++b)
        a.punch[b] = round1 (a.punch[b]), a.level[b] = round1 (a.level[b]), a.width[b] = round1 (a.width[b]);

    return a;
}

//==============================================================================
std::vector<MasterDifference> compareMasters (const MasterProfile& song, const MasterProfile& reference)
{
    std::vector<MasterDifference> out;

    auto words = [] (float v, const std::string& more, const std::string& less, const char* unit = " dB")
    {
        std::ostringstream s;
        s.setf (std::ios::fixed);
        s.precision (1);
        s << std::abs (v) << unit << " " << (v > 0 ? more : less);
        return s.str();
    };

    out.push_back ({ words (song.lufs - reference.lufs, "louder", "quieter", " LU"), song.lufs - reference.lufs });
    out.push_back ({ "peaks " + words (song.peakToLoudness() - reference.peakToLoudness(), "further above", "closer to", " dB") + " its loudness (denser when closer)",
                     song.peakToLoudness() - reference.peakToLoudness() });

    // The tone in regions a mastering EQ would move.
    struct Region { double from, to; const char* name; };
    static const Region regions[] = { { 20, 60, "in the sub (below 60 Hz)" }, { 60, 150, "in the low end (60-150 Hz)" }, { 150, 400, "in the low mids (150-400 Hz)" },
                                      { 400, 1500, "in the mids (400 Hz-1.5 kHz)" }, { 1500, 5000, "in the presence (1.5-5 kHz)" },
                                      { 5000, 10000, "in the highs (5-10 kHz)" }, { 10000, 20000, "in the air (above 10 kHz)" } };

    for (auto& r : regions)
    {
        double sum = 0;
        int count = 0;

        for (size_t k = 0; k < song.tone.size() && k < reference.tone.size(); ++k)
            if (MasterProfile::toneHz (k) >= r.from && MasterProfile::toneHz (k) < r.to)
                sum += song.tone[k] - reference.tone[k], ++count;

        if (count > 0)
            out.push_back ({ words (static_cast<float> (sum / count), "more", "less") + " " + r.name, static_cast<float> (sum / count) });
    }

    for (size_t b = 0; b < MasterProfile::numBands; ++b)
    {
        const std::string band = MasterProfile::bandName (b);
        out.push_back ({ band + " " + words (song.punch[b] - reference.punch[b], "punchier (less compressed)", "flatter (more compressed)"),
                         song.punch[b] - reference.punch[b] });

        // Widths only where there's something on the sides to hear.
        if (std::max (song.width[b], reference.width[b]) > -30)
            out.push_back ({ band + " " + words (song.width[b] - reference.width[b], "wider", "narrower"), (song.width[b] - reference.width[b]) / 2 });
    }

    if (reference.monoBelowHz > 0 && (song.monoBelowHz == 0 || std::abs (std::log2 (song.monoBelowHz / reference.monoBelowHz)) > 0.5))
        out.push_back ({ "the reference is mono below " + std::to_string (static_cast<int> (reference.monoBelowHz)) + " Hz, the song "
                         + (song.monoBelowHz > 0 ? "below " + std::to_string (static_cast<int> (song.monoBelowHz)) + " Hz" : std::string ("never")),
                         song.monoBelowHz == 0 ? 3.0f : static_cast<float> (3 * std::log2 (reference.monoBelowHz / song.monoBelowHz)) });

    std::stable_sort (out.begin(), out.end(), [] (auto& a, auto& b) { return std::abs (a.size) > std::abs (b.size); });
    return out;
}

} // namespace gk::analysis
