#include "Arrangement.h"

#include "Dsp.h"
#include "Groove.h"
#include "analysis/Analysis.h"
#include "library/SampleFeatures.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numeric>
#include <sstream>

namespace gk::listening
{

using model::Json;

const std::vector<std::string>& Arrangement::partNames()
{
    static const std::vector<std::string> names { "kick", "rumble", "bass", "hats", "percussion", "music", "vocal" };
    return names;
}

namespace
{
    constexpr float comesInWithin = 12.0f;  // a part comes in within this of its usual level,
    constexpr float leavesUnder = 18.0f;    // and goes when it's this far under (no flicker in between)
    constexpr float absentUnder = 35.0f;    // a part this far under the mix isn't in the track

    std::vector<double> onsets (const std::vector<float>& x, double rate)
    {
        analysis::Audio audio;
        audio.sampleRate = rate;
        audio.buffer.setSize (1, static_cast<int> (x.size()));
        audio.buffer.copyFrom (0, 0, x.data(), static_cast<int> (x.size()));
        return library::detectTransients (audio);
    }

    float percentile (std::vector<float> v, double q)
    {
        if (v.empty())
            return floorDb;

        const auto k = static_cast<size_t> (std::clamp (q, 0.0, 1.0) * static_cast<double> (v.size() - 1));
        std::nth_element (v.begin(), v.begin() + static_cast<long> (k), v.end());
        return v[k];
    }

    float mean (const std::vector<float>& v, int from, int to)
    {
        from = std::max (from, 0);
        to = std::min (to, static_cast<int> (v.size()));

        if (to <= from)
            return 0;

        return std::accumulate (v.begin() + from, v.begin() + to, 0.0f) / static_cast<float> (to - from);
    }

    /** The low end's peaks: its loudest moments within 100 ms either side (seconds), each weighed
        by how far it stands above a floor 40 dB under the loudest. Mostly the kicks: a rumble
        swelling into each kick peaks at the kick too. */
    std::pair<std::vector<double>, std::vector<float>> lowPeaks (const std::vector<float>& low, double rate)
    {
        const auto hop = std::max<size_t> (1, static_cast<size_t> (0.005 * rate));
        std::vector<float> env;

        for (size_t i = 0; i + hop <= low.size(); i += hop)
        {
            double sum = 0;

            for (size_t j = i; j < i + hop; ++j)
                sum += static_cast<double> (low[j]) * low[j];

            env.push_back (db (sum / static_cast<double> (hop)));
        }

        std::vector<double> times;
        std::vector<float> weights;

        if (env.empty())
            return {};

        const auto floor = *std::max_element (env.begin(), env.end()) - 40;
        constexpr int radius = 20;

        for (int i = radius; i + radius < static_cast<int> (env.size()); ++i)
        {
            const auto e = env[static_cast<size_t> (i)];

            if (e <= floor || e < *std::max_element (env.begin() + (i - radius), env.begin() + (i + radius + 1)))
                continue;

            times.push_back (static_cast<double> (i) * static_cast<double> (hop) / rate);
            weights.push_back (e - floor);
        }

        return { times, weights };
    }

    /** How well the peaks land on a grid of beats: each one's weight, more the closer it is to a
        beat, none from 25 ms off. */
    double alignment (const std::vector<double>& times, const std::vector<float>& weights, double period, double phase)
    {
        constexpr double reach = 0.025;
        double sum = 0;

        for (size_t i = 0; i < times.size(); ++i)
        {
            auto d = std::fmod (times[i] - phase + 1000 * period, period);
            d = std::min (d, period - d);
            sum += d < reach ? weights[i] * (1 - d / reach) : 0.0;
        }

        return sum;
    }

    /** The tempo and phase whose grid lines the peaks up best, near a rough tempo (within 1.5 %):
        over a whole track, a tempo 0.1 BPM off drifts a quarter of a beat in a hundred bars. */
    std::pair<double, double> fitGrid (const std::vector<double>& times, const std::vector<float>& weights, double rough)
    {
        double bestTempo = rough, bestPhase = 0, best = -1;

        auto tryTempo = [&] (double bpm)
        {
            const auto period = 60.0 / bpm;

            for (double phase = 0; phase < period; phase += 0.004)
            {
                const auto s = alignment (times, weights, period, phase);

                if (s > best)
                    best = s, bestTempo = bpm, bestPhase = phase;
            }
        };

        for (double bpm = rough * 0.985; bpm <= rough * 1.015; bpm += 0.02)
            tryTempo (bpm);

        const auto coarse = bestTempo;

        for (double bpm = coarse - 0.03; bpm <= coarse + 0.03; bpm += 0.002)
            tryTempo (bpm);

        return { std::round (bestTempo * 100) / 100, bestPhase };
    }

    /** Where in the beat the hits land: the phase (seconds) the hardest of them fall on, within
        25 ms. Each hit counts by how sharply it jumps (weights, dB): a kick hits, a rumble pumping
        between the kicks only swells, though it can make as many onsets. */
    double beatPhase (const std::vector<double>& hits, const std::vector<float>& weights, double period)
    {
        double best = 0;
        double most = -1;

        for (double phase = 0; phase < period; phase += 0.005)
        {
            double sum = 0;

            for (size_t i = 0; i < hits.size(); ++i)
            {
                const auto d = std::fmod (hits[i] - phase + 1000 * period, period);
                sum += d < 0.025 || d > period - 0.025 ? weights[i] : 0.0;
            }

            if (sum > most)
                most = sum, best = phase;
        }

        // Centred on the hits it caught.
        double offset = 0;
        int n = 0;

        for (auto t : hits)
        {
            auto d = std::fmod (t - best + 1000 * period, period);

            if (d > period / 2)
                d -= period;

            if (std::abs (d) < 0.025)
                offset += d, ++n;
        }

        return n > 0 ? best + offset / n : best;
    }

    std::string timeOf (double seconds)
    {
        const auto s = static_cast<int> (std::round (seconds));
        return std::to_string (s / 60) + ":" + (s % 60 < 10 ? "0" : "") + std::to_string (s % 60);
    }

    float round1 (float v)
    {
        return std::round (v * 10) / 10;
    }
}

std::vector<std::string> Arrangement::partsIn (const Section& s) const
{
    std::vector<std::string> list;

    for (auto& name : partNames())
    {
        auto it = parts.find (name);

        if (it == parts.end())
            continue;

        int count = 0;

        for (int b = s.start; b < s.end && b < static_cast<int> (it->second.on.size()); ++b)
            count += it->second.on[static_cast<size_t> (b)] ? 1 : 0;

        if (count * 2 >= s.end - s.start && count > 0)
            list.push_back (name);
    }

    return list;
}

//==============================================================================
Arrangement hearArrangement (const TrackStems& stems, std::optional<double> knownTempo)
{
    const auto rate = stems.sampleRate;
    Arrangement a;
    a.seconds = static_cast<double> (stems.mix.size()) / rate;

    // The tempo and the beat from the kick (the drums' low end), or the whole drums without one.
    const auto kickLow = filtered (stems.drums, rate, 120, false);
    auto kicks = onsets (kickLow, rate);

    if (kicks.size() < 16)
        kicks = onsets (stems.drums.empty() ? stems.mix : stems.drums, rate);

    a.tempo = knownTempo.value_or (0.0);

    if (a.tempo <= 0)
        a.tempo = estimateTempo ({ kicks }, a.seconds);

    if (a.tempo <= 0 || a.seconds < 8)
        return a;

    // The grid, fitted to the low end's peaks over the whole track; with too few of them, from
    // the onsets.
    double phase = 0;

    if (const auto [times, weights] = lowPeaks (kickLow, rate); times.size() >= 16)
    {
        std::tie (a.tempo, phase) = fitGrid (times, weights, a.tempo);

        // A kick peaks a few milliseconds after it starts: the grid goes on its onsets.
        const auto beat = 60.0 / a.tempo;
        std::vector<double> early;

        for (auto t : kicks)
        {
            auto d = std::fmod (t - phase + 1000 * beat, beat);

            if (d > beat / 2)
                d -= beat;

            if (d > -0.03 && d < 0.01)
                early.push_back (d);
        }

        if (early.size() >= 8)
        {
            std::nth_element (early.begin(), early.begin() + static_cast<long> (early.size() / 2), early.end());
            phase += early[early.size() / 2];
        }
    }
    else
    {
        std::vector<float> jumps;

        for (auto t : kicks)
            jumps.push_back (std::max (0.0f, db (power (kickLow, rate, t, t + 0.03)) - db (power (kickLow, rate, t - 0.04, t - 0.005))));

        phase = beatPhase (kicks, jumps, 60.0 / a.tempo);
    }

    const auto period = 60.0 / a.tempo;
    const auto barSeconds = 4 * period;

    // The parts' signals.
    std::vector<float> lowEnd (stems.drums.size());

    for (size_t i = 0; i < lowEnd.size(); ++i)
        lowEnd[i] = stems.drums[i] + (i < stems.bass.size() ? stems.bass[i] : 0.0f);

    lowEnd = filtered (std::move (lowEnd), rate, 120, false);
    const auto hats = filtered (stems.drums, rate, 6000, true);
    const auto percussion = band (stems.drums, rate, 500, 4000);
    const auto mixLow = filtered (stems.mix, rate, 120, false);
    const auto mixHigh = filtered (stems.mix, rate, 4000, true);
    const auto musicHigh = filtered (stems.other, rate, 2000, true);
    const auto hatHits = onsets (hats, rate);

    // Which beat starts the bar: the one where the music changes most often.
    {
        std::vector<float> novelty;
        std::vector<float> previous;

        for (double t = phase; t + period <= a.seconds; t += period)
        {
            const std::vector<float> f { db (power (lowEnd, rate, t, t + period)), db (power (hats, rate, t, t + period)),
                                         db (power (percussion, rate, t, t + period)), db (power (stems.other, rate, t, t + period)) };
            float change = 0;

            for (size_t i = 0; i < f.size() && ! previous.empty(); ++i)
                change += std::min (std::abs (f[i] - previous[i]), 20.0f);

            novelty.push_back (change);
            previous = f;
        }

        int bestOffset = 0;
        float bestScore = -1;

        for (int offset = 0; offset < 4; ++offset)
        {
            float score = 0;

            for (size_t k = static_cast<size_t> (offset); k < novelty.size(); k += 4)
                score += novelty[k];

            if (score > bestScore)
                bestScore = score, bestOffset = offset;
        }

        a.downbeat = std::fmod (phase + bestOffset * period, barSeconds);

        // A grid a hair before the start begins there, not a bar later.
        if (a.downbeat > barSeconds - 0.03)
            a.downbeat = std::max (0.0, a.downbeat - barSeconds);
    }

    const auto span = a.seconds - a.downbeat;
    a.bars = static_cast<int> (span / barSeconds) + (std::fmod (span, barSeconds) > barSeconds / 2 ? 1 : 0);

    if (a.bars < 1)
        return a;

    // Each bar's levels.
    std::map<std::string, std::vector<float>> levels;
    std::vector<float> kickContrast, kickAttack, musicBrightness;
    std::vector<bool> kickPumps;

    for (int b = 0; b < a.bars; ++b)
    {
        const auto from = a.downbeat + b * barSeconds, to = from + barSeconds;
        double kickOn = 0, kickOff = 0, rumble = 0;
        float attack = 0;

        for (int beat = 0; beat < 4; ++beat)
        {
            const auto t = from + beat * period;
            kickOn += power (kickLow, rate, t, t + 0.06);
            kickOff += power (kickLow, rate, t + 0.45 * period, t + 0.9 * period);
            rumble += power (lowEnd, rate, t + 0.45 * period, t + 0.9 * period);

            // How sharply the low end jumps on the beat: a kick hits even where a rumble fills
            // the space between the beats almost as loud.
            attack += db (power (kickLow, rate, t, t + 0.03)) - db (power (kickLow, rate, t - 0.04, t - 0.005));
        }

        kickAttack.push_back (attack / 4);

        // The low end's shape over the beat: a kick peaks in its first 60 ms and the low end dips
        // well under it after, even where a rumble swells back up into the next kick.
        {
            constexpr int bins = 20;
            std::array<double, bins> shape {};

            for (int beat = 0; beat < 4; ++beat)
                for (int j = 0; j < bins; ++j)
                {
                    const auto t = from + beat * period + j * period / bins;
                    shape[static_cast<size_t> (j)] += power (kickLow, rate, t, t + period / bins);
                }

            std::array<float, bins> level {};

            for (int j = 0; j < bins; ++j)
                level[static_cast<size_t> (j)] = db (shape[static_cast<size_t> (j)] / 4);

            const auto early = static_cast<int> (std::ceil (0.06 / (period / bins)));
            const auto first = *std::max_element (level.begin(), level.begin() + early);
            const auto rest = *std::max_element (level.begin() + early, level.end());
            const auto [low, high] = std::minmax_element (level.begin(), level.end());
            kickPumps.push_back (first >= rest - 1 && *high - *low >= 6);
        }

        levels["kick"].push_back (db (kickOn / 4));
        levels["rumble"].push_back (db (rumble / 4));
        levels["bass"].push_back (db (power (stems.bass, rate, from, to)));
        levels["hats"].push_back (db (power (hats, rate, from, to)));
        levels["percussion"].push_back (db (power (percussion, rate, from, to)));
        levels["music"].push_back (db (power (stems.other, rate, from, to)));
        levels["vocal"].push_back (db (power (stems.vocals, rate, from, to)));
        kickContrast.push_back (db (kickOn / 4) - db (kickOff / 4));
        musicBrightness.push_back (db (power (musicHigh, rate, from, to)) - levels["music"].back());

        const auto loud = db (power (stems.mix, rate, from, to));
        a.loudness.push_back (round1 (loud));
        a.low.push_back (round1 (db (power (mixLow, rate, from, to))));
        a.brightness.push_back (round1 (db (power (mixHigh, rate, from, to)) - loud));

        const auto hitsInBar = std::count_if (hatHits.begin(), hatHits.end(), [&] (double t) { return t >= from && t < to; });
        a.density.push_back (round1 (static_cast<float> (hitsInBar) / 4));
    }

    // Where each part plays: within reach of its usual level, in a track it's really in. A kick
    // also stands out on the beat; without that it's the rumble alone.
    const auto mixRef = percentile (a.loudness, 0.9);

    for (auto& name : Arrangement::partNames())
    {
        auto& lane = a.parts[name];
        lane.level = levels[name];
        const auto ref = percentile (lane.level, 0.9);
        const bool inTrack = ref > mixRef - absentUnder && ref > -65;
        lane.on.resize (static_cast<size_t> (a.bars));

        bool playing = false;

        for (int b = 0; b < a.bars; ++b)
        {
            const auto level = lane.level[static_cast<size_t> (b)];
            playing = inTrack && level > ref - (playing ? leavesUnder : comesInWithin);
            bool on = playing;

            if (name == "kick")
                on = on && (kickContrast[static_cast<size_t> (b)] > 4 || kickAttack[static_cast<size_t> (b)] > 6 || kickPumps[static_cast<size_t> (b)]);

            lane.on[static_cast<size_t> (b)] = on;
        }

        for (auto& l : lane.level)
            l = round1 (l);
    }

    // Sections: runs of four-bar blocks with the same parts, none shorter than eight bars.
    {
        const auto& names = Arrangement::partNames();
        const int blocks = (a.bars + 3) / 4;
        std::vector<uint32_t> signature (static_cast<size_t> (blocks), 0);

        for (int k = 0; k < blocks; ++k)
        {
            const int from = k * 4, to = std::min (a.bars, from + 4);

            for (size_t p = 0; p < names.size(); ++p)
            {
                int count = 0;

                for (int b = from; b < to; ++b)
                    count += a.parts[names[p]].on[static_cast<size_t> (b)] ? 1 : 0;

                if (count * 2 >= to - from && count > 0)
                    signature[static_cast<size_t> (k)] |= 1u << p;
            }
        }

        struct Run { int start, end; uint32_t parts; };
        std::vector<Run> runs;

        for (int k = 0; k < blocks; ++k)
        {
            if (! runs.empty() && runs.back().parts == signature[static_cast<size_t> (k)])
                runs.back().end = k + 1;
            else
                runs.push_back ({ k, k + 1, signature[static_cast<size_t> (k)] });
        }

        auto distance = [] (uint32_t x, uint32_t y) { return std::popcount (x ^ y); };

        for (;;)
        {
            auto shortest = std::min_element (runs.begin(), runs.end(), [] (auto& x, auto& y) { return x.end - x.start < y.end - y.start; });

            if (runs.size() < 2 || shortest->end - shortest->start >= 2)
                break;

            // Into the neighbour it's most like (the one before when they tie).
            const auto i = static_cast<size_t> (shortest - runs.begin());
            const bool intoPrevious = i > 0 && (i + 1 >= runs.size() || distance (runs[i - 1].parts, runs[i].parts) <= distance (runs[i + 1].parts, runs[i].parts));
            auto& into = intoPrevious ? runs[i - 1] : runs[i + 1];
            into.start = std::min (into.start, runs[i].start);
            into.end = std::max (into.end, runs[i].end);
            runs.erase (runs.begin() + static_cast<long> (i));

            // Neighbours that now match become one.
            for (size_t j = 1; j < runs.size();)
            {
                if (runs[j].parts == runs[j - 1].parts)
                    runs[j - 1].end = runs[j].end, runs.erase (runs.begin() + static_cast<long> (j));
                else
                    ++j;
            }
        }

        for (auto& r : runs)
            a.sections.push_back ({ r.start * 4, std::min (a.bars, r.end * 4), {} });
    }

    // What each section does.
    {
        auto sectionLoudness = [&] (const Arrangement::Section& s) { return mean (a.loudness, s.start, s.end); };
        std::vector<float> sorted (a.loudness);
        const auto median = percentile (sorted, 0.5);
        size_t mostParts = 0;

        for (auto& s : a.sections)
            mostParts = std::max (mostParts, a.partsIn (s).size());

        auto has = [&] (const Arrangement::Section& s, const std::string& part)
        {
            auto list = a.partsIn (s);
            return std::find (list.begin(), list.end(), part) != list.end();
        };

        auto quieter = [&] (const Arrangement::Section& s)
        {
            return sectionLoudness (s) < median - 1.5f || a.partsIn (s).size() + 1 < mostParts;
        };

        const auto last = a.sections.size() - 1;

        for (size_t i = 0; i < a.sections.size(); ++i)
        {
            auto& s = a.sections[i];

            if (i == 0 && last > 0 && quieter (s))
                s.role = "intro";
            else if (i == last && last > 0 && quieter (s))
                s.role = "outro";
            else if (! has (s, "kick"))
                s.role = "break";
            else if (i > 0 && a.sections[i - 1].role == "break")
                s.role = "drop";
            else
                s.role = "groove";
        }

        // A section that rises into a drop builds it.
        for (size_t i = 0; i + 1 < a.sections.size(); ++i)
        {
            auto& s = a.sections[i];

            if (a.sections[i + 1].role != "drop" || s.role == "intro" || s.end - s.start < 4)
                continue;

            const auto rise = mean (a.brightness, s.end - 2, s.end) - mean (a.brightness, s.start, s.start + 2);
            const auto louder = mean (a.loudness, s.end - 2, s.end) - mean (a.loudness, s.start, s.start + 2);

            if (rise > 3 || louder > 3)
                s.role = "build";
        }
    }

    // Changes inside: a part out for a bar, and the music's filter opening or closing.
    for (auto& name : Arrangement::partNames())
    {
        const auto& on = a.parts[name].on;

        for (int b = 1; b + 1 < a.bars; ++b)
            if (on[static_cast<size_t> (b - 1)] && ! on[static_cast<size_t> (b)] && on[static_cast<size_t> (b + 1)])
                a.changes.push_back ({ b, name, "out for a bar" });
    }

    for (auto& s : a.sections)
    {
        auto list = a.partsIn (s);

        if (s.end - s.start < 8 || std::find (list.begin(), list.end(), "music") == list.end())
            continue;

        const auto change = mean (musicBrightness, s.end - 2, s.end) - mean (musicBrightness, s.start, s.start + 2);

        if (change >= 6)
            a.changes.push_back ({ s.start, "music", "opens (filter) to bar " + std::to_string (s.end) });
        else if (change <= -6)
            a.changes.push_back ({ s.start, "music", "closes (filter) to bar " + std::to_string (s.end) });
    }

    std::sort (a.changes.begin(), a.changes.end(), [] (auto& x, auto& y) { return x.bar < y.bar; });
    return a;
}

//==============================================================================
Json Arrangement::toJson() const
{
    Json lanes = Json::object();

    for (auto& [name, lane] : parts)
    {
        std::string on;

        for (bool b : lane.on)
            on += b ? '1' : '0';

        lanes[name] = { { "level", lane.level }, { "on", on } };
    }

    auto sectionList = Json::array(), changeList = Json::array();

    for (auto& s : sections)
        sectionList.push_back ({ { "start", s.start }, { "end", s.end }, { "role", s.role } });

    for (auto& c : changes)
        changeList.push_back ({ { "bar", c.bar }, { "part", c.part }, { "what", c.what } });

    return { { "tempo", std::round (tempo * 100) / 100 }, { "downbeat", std::round (downbeat * 1000) / 1000 }, { "seconds", std::round (seconds * 10) / 10 },
             { "bars", bars }, { "parts", lanes }, { "loudness", loudness }, { "low", low }, { "brightness", brightness }, { "density", density },
             { "sections", sectionList }, { "changes", changeList } };
}

Arrangement Arrangement::fromJson (const Json& j)
{
    Arrangement a;
    a.tempo = j.value ("tempo", 0.0);
    a.downbeat = j.value ("downbeat", 0.0);
    a.seconds = j.value ("seconds", 0.0);
    a.bars = j.value ("bars", 0);
    a.loudness = j.value ("loudness", std::vector<float>());
    a.low = j.value ("low", std::vector<float>());
    a.brightness = j.value ("brightness", std::vector<float>());
    a.density = j.value ("density", std::vector<float>());

    const auto lanes = j.value ("parts", Json::object());

    for (auto& [name, lane] : lanes.items())
    {
        Lane l;
        l.level = lane.value ("level", std::vector<float>());

        for (auto c : lane.value ("on", std::string()))
            l.on.push_back (c == '1');

        a.parts[name] = std::move (l);
    }

    for (auto& s : j.value ("sections", Json::array()))
        a.sections.push_back ({ s.value ("start", 0), s.value ("end", 0), s.value ("role", "") });

    for (auto& c : j.value ("changes", Json::array()))
        a.changes.push_back ({ c.value ("bar", 0), c.value ("part", ""), c.value ("what", "") });

    return a;
}

std::string Arrangement::describe() const
{
    if (bars == 0)
        return "No beat grid could be heard.";

    std::ostringstream out;
    out.setf (std::ios::fixed);
    out.precision (1);
    out << tempo << " BPM, " << bars << " bars of 4/4 (" << timeOf (seconds) << "), bar 1 at " << timeOf (downbeat) << "\n";

    // Energy as a bar of eighths against the loudest bar, two per section (its first and second half).
    const auto loudest = loudness.empty() ? 0.0f : *std::max_element (loudness.begin(), loudness.end());
    static const char* levels[] = { "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };

    auto energy = [&] (int from, int to)
    {
        const auto l = mean (loudness, from, to);
        const auto k = std::clamp (static_cast<int> ((l - (loudest - 18)) / 18 * 8), 0, 7);
        return std::string (levels[k]);
    };

    out << "Bars     Section  Energy  Parts\n";

    for (auto& s : sections)
    {
        auto range = std::to_string (s.start + 1) + "-" + std::to_string (s.end);
        range.resize (std::max<size_t> (range.size(), 9), ' ');
        auto role = s.role;
        role.resize (std::max<size_t> (role.size(), 9), ' ');

        auto list = partsIn (s);
        std::string names;

        for (auto& p : list)
            names += (names.empty() ? "" : ", ") + p;

        const auto middle = (s.start + s.end) / 2;
        out << range << role << energy (s.start, middle) << energy (middle, s.end) << "      " << (names.empty() ? "nothing" : names) << "\n";
    }

    if (! changes.empty())
    {
        out << "Changes\n";
        int shown = 0;

        for (auto& c : changes)
        {
            if (++shown > 16)
            {
                out << "(and " << changes.size() - 16 << " more)\n";
                break;
            }

            out << "bar " << c.bar + 1 << ": " << c.part << " " << c.what << "\n";
        }
    }

    return out.str();
}

} // namespace gk::listening
