#include "Voices.h"

#include "Clap.h"
#include "library/SampleFeatures.h"
#include "library/SampleLibrary.h"
#include "music/Notes.h"

#include <functional>
#include <map>
#include <set>
#include <sstream>

#include <juce_dsp/juce_dsp.h>

namespace gk::listening
{

using Json = model::Json;

namespace
{
    float round1 (float v)   { return std::round (v * 10) / 10; }
}

Json SoundedNote::toJson() const
{
    auto h = Json::array();

    for (auto v : harmonics)
        h.push_back (round1 (v));

    return { { "start", std::round (note.start * 1000) / 1000 }, { "end", std::round (note.end * 1000) / 1000 },
             { "pitch", round1 (note.pitch) }, { "level", std::round (note.level * 100) / 100 },
             { "harmonics", h }, { "air", round1 (air) }, { "fall", round1 (fall) } };
}

std::vector<SoundedNote> hearNotes (NoteTranscriber& transcriber, const juce::AudioBuffer<float>& stem, double rate, double minNoteSeconds)
{
    std::vector<SoundedNote> out;

    for (auto& n : transcriber.transcribe (stem, rate, minNoteSeconds))
        out.push_back ({ n });

    if (out.empty())
        return out;

    // The sound of each note from the stem, mono: a 4096-point spectrum of its first 120 ms
    // (Hann window), read at its harmonics; and its level over its start and its end.
    const auto mono = resampleMono (stem, rate, rate);
    constexpr int order = 12, size = 1 << order;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window (size, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> buffer (2 * size);

    auto rmsAt = [&] (double from, double seconds)
    {
        const auto a = std::clamp<int64_t> (static_cast<int64_t> (from * rate), 0, static_cast<int64_t> (mono.size()));
        const auto b = std::clamp<int64_t> (a + static_cast<int64_t> (seconds * rate), a, static_cast<int64_t> (mono.size()));
        double sum = 0;

        for (auto i = a; i < b; ++i)
            sum += mono[static_cast<size_t> (i)] * mono[static_cast<size_t> (i)];

        return b > a ? 10 * std::log10 (sum / static_cast<double> (b - a) + 1e-12) : -120.0;
    };

    // A 4096-point spectrum of the note's start.
    auto spectrumAt = [&] (int64_t from)
    {
        std::vector<float> b (2 * size, 0.0f);

        for (int64_t i = 0; i < size; ++i)
            if (from + i >= 0 && from + i < static_cast<int64_t> (mono.size()))
                b[static_cast<size_t> (i)] = mono[static_cast<size_t> (from + i)];

        window.multiplyWithWindowingTable (b.data(), size);
        fft.performFrequencyOnlyForwardTransform (b.data());
        return b;
    };

    for (auto& s : out)
    {
        const auto from = static_cast<int64_t> (s.note.start * rate);
        buffer = spectrumAt (from);

        const auto binHz = rate / size;
        const auto f0 = 440.0 * std::pow (2.0, (s.note.pitch - 69) / 12.0);
        auto peakNear = [&] (double hz)
        {
            const auto centre = static_cast<int> (std::lround (hz / binHz));
            float peak = 0;

            for (int b = std::max (1, centre - 2); b <= std::min (size / 2 - 1, centre + 2); ++b)
                peak = std::max (peak, buffer[static_cast<size_t> (b)]);

            return peak;
        };

        float strongest = 1e-9f;
        std::array<float, 8> raw {};

        for (int h = 0; h < 8; ++h)
            if (f0 * (h + 1) < rate / 2)
                strongest = std::max (strongest, raw[static_cast<size_t> (h)] = peakNear (f0 * (h + 1)));

        for (int h = 0; h < 8; ++h)
            s.harmonics[static_cast<size_t> (h)] = std::max (-60.0f, juce::Decibels::gainToDecibels (raw[static_cast<size_t> (h)] / strongest));

        double all = 0, above = 0;

        for (int b = 1; b < size / 2; ++b)
        {
            const auto e = static_cast<double> (buffer[static_cast<size_t> (b)]) * buffer[static_cast<size_t> (b)];
            all += e;

            if (b * binHz > f0 * 8.5)
                above += e;
        }

        s.air = static_cast<float> (all > 0 ? std::max (-60.0, 10 * std::log10 (above / all + 1e-12)) : -60.0);

        const auto length = s.note.end - s.note.start;
        const auto part = std::min (0.05, length / 3);
        s.fall = static_cast<float> (std::max (0.0, rmsAt (s.note.start, part) - rmsAt (s.note.end - part, part)));
    }

    return out;
}

namespace
{
    /** Where a sound's spectrum brightens or grows suddenly (spectral flux between lowHz and
        highHz, peaks over the local median): the attacks of notes that run into each other,
        such as a distorted bass whose level hardly moves while its filter opens on every note. */
    std::vector<double> fluxOnsets (const std::vector<float>& mono, double rate, double lowHz, double highHz)
    {
        constexpr double fluxRate = 22050;
        const auto x = rate == fluxRate ? mono : [&]
        {
            juce::AudioBuffer<float> b (1, static_cast<int> (mono.size()));
            b.copyFrom (0, 0, mono.data(), b.getNumSamples());
            return resampleMono (b, rate, fluxRate);
        }();

        constexpr int order = 10, size = 1 << order, hop = 128;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window (size, juce::dsp::WindowingFunction<float>::hann, false);
        std::vector<float> buffer (2 * size), previous (size / 2, 0.0f);
        const auto lowBin = std::max (1, static_cast<int> (lowHz / fluxRate * size)), highBin = std::min (size / 2, static_cast<int> (highHz / fluxRate * size));
        std::vector<float> flux;

        for (size_t start = 0; start + size <= x.size(); start += hop)
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            std::copy_n (x.begin() + static_cast<std::ptrdiff_t> (start), size, buffer.begin());
            window.multiplyWithWindowingTable (buffer.data(), size);
            fft.performFrequencyOnlyForwardTransform (buffer.data());
            float sum = 0;

            for (int b = lowBin; b < highBin; ++b)
            {
                const auto now = std::log1p (100 * buffer[static_cast<size_t> (b)]);
                sum += std::max (0.0f, now - previous[static_cast<size_t> (b)]);
                previous[static_cast<size_t> (b)] = now;
            }

            flux.push_back (sum);
        }

        // Peaks above the median around them (a quarter second) by a margin, 50 ms apart at least.
        std::vector<double> onsets;
        const auto frameSeconds = hop / fluxRate;
        const int around = static_cast<int> (0.125 / frameSeconds), gap = static_cast<int> (0.05 / frameSeconds);
        float largest = 0;

        for (auto f : flux)
            largest = std::max (largest, f);

        for (int i = 1; i + 1 < static_cast<int> (flux.size()); ++i)
        {
            const auto f = flux[static_cast<size_t> (i)];

            if (f < flux[static_cast<size_t> (i - 1)] || f < flux[static_cast<size_t> (i + 1)] || f < 0.1f * largest)
                continue;

            std::vector<float> local;

            for (int k = std::max (0, i - around); k < std::min (static_cast<int> (flux.size()), i + around); ++k)
                local.push_back (flux[static_cast<size_t> (k)]);

            std::nth_element (local.begin(), local.begin() + static_cast<long> (local.size() / 2), local.end());

            if (f < 2.0f * local[local.size() / 2] + 0.08f * largest)
                continue;

            // The frame's middle, less a little: the attack is where the window first reaches it.
            const auto t = (static_cast<double> (i) * hop + size / 2) / fluxRate - 0.01;

            if (! onsets.empty() && (t - onsets.back()) < gap * frameSeconds)
                continue;

            onsets.push_back (t);
        }

        return onsets;
    }
}

std::vector<SoundedNote> hearBass (const juce::AudioBuffer<float>& stem, double rate)
{
    std::vector<SoundedNote> notes;
    constexpr double pitchRate = 4000;
    const auto low = resampleMono (stem, rate, pitchRate);
    const auto frames = music::trackPitch (low.data(), static_cast<int> (low.size()), pitchRate, 32, 500);

    if (frames.empty())
        return notes;

    // Attacks, on the stem at its own rate.
    analysis::Audio audio;
    audio.sampleRate = rate;
    {
        const auto mono = resampleMono (stem, rate, rate);
        audio.buffer.setSize (1, static_cast<int> (mono.size()));
        audio.buffer.copyFrom (0, 0, mono.data(), static_cast<int> (mono.size()));
    }

    auto starts = library::detectTransients (audio);

    {
        const auto mono = resampleMono (stem, rate, rate);
        const auto bright = fluxOnsets (mono, rate, 100, 1500);
        starts.insert (starts.end(), bright.begin(), bright.end());
    }
    float loudest = 0;

    for (auto& f : frames)
        loudest = std::max (loudest, f.level);

    // Where the pitch moves by more than half a semitone and holds there for 30 ms; not by an
    // octave, which is the tracker slipping on a deep note (a played octave has its own attack).
    const auto voiced = [&] (const music::PitchFrame& f) { return f.pitch > 0 && f.level > 0.03f * loudest; };

    for (size_t i = 1; i + 3 < frames.size(); ++i)
    {
        const auto moved = std::abs (frames[i].pitch - frames[i - 1].pitch);

        if (! voiced (frames[i]) || ! voiced (frames[i - 1]) || moved < 0.5f || std::abs (moved - 12) < 0.5f)
            continue;

        bool holds = true;

        for (size_t k = i; k < i + 3; ++k)
            holds = holds && voiced (frames[k]) && std::abs (frames[k].pitch - frames[i].pitch) < 0.5f;

        if (holds)
            starts.push_back (frames[i].time - 0.02);
    }

    std::sort (starts.begin(), starts.end());

    // Starts closer than 40 ms are one.
    std::vector<double> merged;

    for (auto t : starts)
        if (merged.empty() || t - merged.back() > 0.04)
            merged.push_back (t);

    // Each stretch from a start to the next is a note when most of it has a pitch: the median of
    // its frames, which end where the level falls 20 dB under the stretch's peak.
    const auto end = frames.back().time;

    for (size_t i = 0; i < merged.size(); ++i)
    {
        const auto from = merged[i], to = i + 1 < merged.size() ? merged[i + 1] : end;
        std::vector<float> pitches;
        float peak = 0;
        double last = from;
        int count = 0;

        for (auto& f : frames)
            if (f.time >= from + 0.015 && f.time < to)
                peak = std::max (peak, f.level);

        for (auto& f : frames)
        {
            if (f.time < from + 0.015 || f.time >= to)
                continue;

            ++count;

            if (f.level < 0.1f * peak)
                break;

            last = f.time;

            if (voiced (f))
                pitches.push_back (f.pitch);
        }

        if (count == 0 || pitches.size() * 2 < static_cast<size_t> (count) || pitches.size() < 2 || last - from < 0.04)
            continue;

        std::nth_element (pitches.begin(), pitches.begin() + static_cast<long> (pitches.size() / 2), pitches.end());
        SoundedNote n;
        n.note.start = from;
        n.note.end = std::min (to, last + 0.01);
        n.note.pitch = pitches[pitches.size() / 2];
        n.note.level = peak / loudest;
        notes.push_back (n);
    }

    return notes;
}

//==============================================================================
namespace
{
    template <typename T>
    T medianOf (std::vector<T> v, T fallback = {})
    {
        if (v.empty())
            return fallback;

        std::nth_element (v.begin(), v.begin() + static_cast<long> (v.size() / 2), v.end());
        return v[v.size() / 2];
    }

    using Cell = std::pair<int, int>;   // a step in the figure, a key

    /** How alike two figures are: their shared notes against all, a note matching the same key a
        step either side (heard notes wobble by that much). */
    float jaccard (const std::set<Cell>& a, const std::set<Cell>& b)
    {
        if (a.empty() && b.empty())
            return 1;

        size_t both = 0;

        for (auto& c : a)
            both += b.count (c) > 0 || b.count ({ c.first - 1, c.second }) > 0 || b.count ({ c.first + 1, c.second }) > 0 ? 1 : 0;

        return static_cast<float> (both) / static_cast<float> (std::max<size_t> (1, a.size() + b.size() - both));
    }

    std::set<Cell> cellsOf (const std::vector<GridNote>& notes)
    {
        std::set<Cell> cells;

        for (auto& n : notes)
            cells.insert ({ n.step, n.key });

        return cells;
    }

    std::set<int> stepsOf (const std::vector<GridNote>& notes)
    {
        std::set<int> steps;

        for (auto& n : notes)
            steps.insert (n.step);

        return steps;
    }
}

Json Voice::Run::toJson() const
{
    auto list = Json::array();

    for (auto& n : notes)
        list.push_back ({ n.step, n.steps, n.key, n.velocity });

    Json j { { "from", from }, { "to", to }, { "bars", bars }, { "notes", list } };

    if (! change.empty())
        j["change"] = change;

    if (variations > 0)
        j["variations"] = variations;

    return j;
}

Voice::Run Voice::Run::fromJson (const Json& j)
{
    Run r;
    r.from = j.value ("from", 0);
    r.to = j.value ("to", 0);
    r.bars = j.value ("bars", 1);
    r.change = j.value ("change", "");
    r.variations = j.value ("variations", 0);

    for (auto& n : j.value ("notes", Json::array()))
        if (n.is_array() && n.size() == 4)
            r.notes.push_back ({ n[0].get<int>(), n[1].get<int>(), n[2].get<int>(), n[3].get<int>() });

    return r;
}

std::vector<GridNote> Voice::notesIn (int bar) const
{
    std::vector<GridNote> out;

    for (auto& r : runs)
    {
        if (bar < r.from || bar >= r.to)
            continue;

        const auto offset = ((bar - r.from) % r.bars) * 16;

        for (auto n : r.notes)
            if (n.step >= offset && n.step < offset + 16)
            {
                n.step -= offset;
                out.push_back (n);
            }
    }

    return out;
}

bool Voice::playsIn (int bar) const
{
    return std::any_of (runs.begin(), runs.end(), [&] (auto& r) { return bar >= r.from && bar < r.to; });
}

Json Voice::toJson() const
{
    auto list = Json::array();

    for (auto& r : runs)
        list.push_back (r.toJson());

    return { { "role", role }, { "part", part }, { "sound", sound }, { "range", { lowest, highest } },
             { "level", std::round (level * 100) / 100 }, { "chord", round1 (chord) }, { "notesPerBar", round1 (notesPerBar) }, { "runs", list } };
}

Voice Voice::fromJson (const Json& j)
{
    Voice v;
    v.role = j.value ("role", "");
    v.part = j.value ("part", "");
    v.sound = j.value ("sound", "");

    if (auto range = j.value ("range", Json::array()); range.size() == 2)
        v.lowest = range[0].get<int>(), v.highest = range[1].get<int>();

    v.level = j.value ("level", 0.0f);
    v.chord = j.value ("chord", 1.0f);
    v.notesPerBar = j.value ("notesPerBar", 0.0f);

    for (auto& r : j.value ("runs", Json::array()))
        v.runs.push_back (Run::fromJson (r));

    return v;
}

Json Pairing::toJson() const
{
    auto list = Json::array();

    for (auto& [semitones, share] : intervals)
        list.push_back ({ semitones, std::round (share * 100) / 100 });

    Json j { { "a", a }, { "b", b }, { "bars", bars }, { "together", std::round (together * 100) / 100 },
             { "fills", std::round (fills * 100) / 100 }, { "intervals", list } };

    if (answers)
        j["answerAfter"] = answerAfter;

    return j;
}

Pairing Pairing::fromJson (const Json& j)
{
    Pairing p;
    p.a = j.value ("a", 0);
    p.b = j.value ("b", 0);
    p.bars = j.value ("bars", 0);
    p.together = j.value ("together", 0.0f);
    p.fills = j.value ("fills", 0.0f);
    p.answers = j.contains ("answerAfter");
    p.answerAfter = j.value ("answerAfter", 0);

    for (auto& i : j.value ("intervals", Json::array()))
        if (i.is_array() && i.size() == 2)
            p.intervals.push_back ({ i[0].get<int>(), i[1].get<float>() });

    return p;
}

int TrackVoices::playingIn (int bar) const
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [&] (auto& v) { return v.playsIn (bar); }));
}

Json TrackVoices::toJson() const
{
    auto v = Json::array(), p = Json::array();

    for (auto& voice : voices)
        v.push_back (voice.toJson());

    for (auto& pair : pairs)
        p.push_back (pair.toJson());

    Json j { { "voices", v }, { "pairs", p } };

    if (key)
        j["key"] = { { "key", music::keyName (key->key) }, { "confidence", std::round (key->confidence * 100) / 100 } };

    return j;
}

TrackVoices TrackVoices::fromJson (const Json& j)
{
    TrackVoices t;

    for (auto& v : j.value ("voices", Json::array()))
        t.voices.push_back (Voice::fromJson (v));

    for (auto& p : j.value ("pairs", Json::array()))
        t.pairs.push_back (Pairing::fromJson (p));

    if (j.contains ("key"))
        if (auto k = music::parseKey (j["key"].value ("key", "")))
            t.key = music::KeyEstimate { *k, j["key"].value ("confidence", 0.0f) };

    return t;
}

//==============================================================================
std::vector<Voice::Run> foldRuns (const std::vector<GridNote>& notes, int bars, const std::vector<int>& sectionStarts)
{
    std::vector<Voice::Run> runs;

    if (notes.empty() || bars <= 0)
        return runs;

    // The figure's length: the shortest of 1, 2 and 4 bars whose blocks repeat about as well as
    // the best.
    auto blocksOf = [&] (int length)
    {
        std::vector<std::vector<GridNote>> blocks (static_cast<size_t> ((bars + length - 1) / length));

        for (auto n : notes)
        {
            const auto block = n.step / (16 * length);

            if (n.step < 0 || block >= static_cast<int> (blocks.size()))
                continue;

            n.step -= block * 16 * length;
            blocks[static_cast<size_t> (block)].push_back (n);
        }

        return blocks;
    };

    auto repeats = [&] (const std::vector<std::vector<GridNote>>& blocks)
    {
        float sum = 0;
        int count = 0;

        for (size_t k = 0; k + 1 < blocks.size(); ++k)
            if (! blocks[k].empty() && ! blocks[k + 1].empty())
                sum += jaccard (cellsOf (blocks[k]), cellsOf (blocks[k + 1])), ++count;

        return count > 0 ? sum / static_cast<float> (count) : 0.0f;
    };

    int length = 1;
    {
        const std::array<int, 3> lengths { 1, 2, 4 };
        std::array<float, 3> scores {};

        for (size_t i = 0; i < lengths.size(); ++i)
            scores[i] = repeats (blocksOf (lengths[i]));

        const auto best = *std::max_element (scores.begin(), scores.end());

        for (size_t i = 0; i < lengths.size(); ++i)
            if (scores[i] >= best - 0.1f)
            {
                length = lengths[i];
                break;
            }
    }

    const auto blocks = blocksOf (length);
    const auto count = static_cast<int> (blocks.size());

    // A figure: the notes in at least a share of the blocks, each with its median length and
    // velocity.
    auto consensus = [&] (int from, int to, float share)
    {
        std::map<Cell, std::vector<const GridNote*>> seen;
        std::map<Cell, std::set<int>> where;
        int played = 0;

        for (int k = from; k < to; ++k)
        {
            played += blocks[static_cast<size_t> (k)].empty() ? 0 : 1;

            for (auto& n : blocks[static_cast<size_t> (k)])
                seen[{ n.step, n.key }].push_back (&n), where[{ n.step, n.key }].insert (k);
        }

        // A note heard a step early or late in some repeats counts where most of them have it;
        // not two notes of the same key a step apart that play in the same repeats.
        {
            std::vector<Cell> order;

            for (auto& [cell, list] : seen)
                order.push_back (cell);

            std::stable_sort (order.begin(), order.end(), [&] (auto& x, auto& y) { return seen[x].size() > seen[y].size(); });

            for (auto& cell : order)
            {
                auto here = seen.find (cell);

                if (here == seen.end())
                    continue;

                for (auto step : { cell.first - 1, cell.first + 1 })
                    if (auto near = seen.find ({ step, cell.second }); near != seen.end() && near->second.size() <= here->second.size())
                    {
                        auto& a = where[cell];
                        auto& b = where[{ step, cell.second }];
                        const auto both = std::count_if (b.begin(), b.end(), [&] (int k) { return a.count (k) > 0; });

                        if (both * 5 > static_cast<long> (std::min (a.size(), b.size())))
                            continue;

                        a.insert (b.begin(), b.end());
                        here->second.insert (here->second.end(), near->second.begin(), near->second.end());
                        seen.erase (near);
                    }
            }
        }

        std::vector<GridNote> figure;

        for (auto& [cell, list] : seen)
        {
            if (static_cast<float> (list.size()) < share * static_cast<float> (played))
                continue;

            std::vector<int> steps, velocities;

            for (auto* n : list)
                steps.push_back (n->steps), velocities.push_back (n->velocity);

            figure.push_back ({ cell.first, std::max (1, medianOf (steps, 1)), cell.second, medianOf (velocities, 100) });
        }

        std::sort (figure.begin(), figure.end(), [] (auto& x, auto& y) { return x.step != y.step ? x.step < y.step : x.key < y.key; });
        return figure;
    };

    auto addRun = [&] (int from, int to)
    {
        auto figure = consensus (from, to, 0.5f);

        if (figure.empty())
            figure = consensus (from, to, 0.34f);

        if (figure.empty())
            return;

        Voice::Run r;
        r.from = from * length;
        r.to = std::min (bars, to * length);
        r.bars = length;
        r.notes = std::move (figure);
        const auto cells = cellsOf (r.notes);

        for (int k = from; k < to; ++k)
            if (jaccard (cellsOf (blocks[static_cast<size_t> (k)]), cells) < 0.5f)
                ++r.variations;

        runs.push_back (std::move (r));
    };

    // Splits a stretch where the figure changes: where the figure of up to four blocks before
    // differs from the one of up to four after (two at least each side), at the most different
    // place nearby.
    auto split = [&] (int from, int to)
    {
        std::vector<float> score (static_cast<size_t> (to - from + 1), 1.0f);

        for (int k = from + 2; k + 2 <= to; ++k)
        {
            const auto left = std::min (4, k - from), right = std::min (4, to - k);
            score[static_cast<size_t> (k - from)] = jaccard (cellsOf (consensus (k - left, k, 0.5f)), cellsOf (consensus (k, k + right, 0.5f)));
        }

        // Each stretch of low scores is one change: at the block where the blocks around it are
        // most like their own side's figure.
        int start = from;

        for (int k = from + 2; k + 2 <= to; ++k)
        {
            if (score[static_cast<size_t> (k - from)] >= 0.4f)
                continue;

            auto last = k;

            while (last + 3 <= to && score[static_cast<size_t> (last + 1 - from)] < 0.4f)
                ++last;

            const auto lo = std::max (start, k - 4), hi = std::min (to, last + 4);
            int best = k;
            float bestFit = -1;

            for (int at = k; at <= last; ++at)
            {
                const auto left = cellsOf (consensus (lo, at, 0.5f)), right = cellsOf (consensus (at, hi, 0.5f));
                float fit = 0;

                for (int b = lo; b < hi; ++b)
                    fit += jaccard (cellsOf (blocks[static_cast<size_t> (b)]), b < at ? left : right);

                if (fit > bestFit)
                    bestFit = fit, best = at;
            }

            addRun (start, best);
            start = best;
            k = last;
        }

        addRun (start, to);
    };

    // Stretches where it plays, within the sections: a section start or a block without a note
    // ends one.
    std::set<int> starts;

    for (auto bar : sectionStarts)
        starts.insert (bar / length);

    for (int k = 0; k < count;)
    {
        if (blocks[static_cast<size_t> (k)].empty())
        {
            ++k;
            continue;
        }

        auto end = k + 1;

        while (end < count && ! blocks[static_cast<size_t> (end)].empty() && starts.count (end) == 0)
            ++end;

        split (k, end);
        k = end;
    }

    // Runs next to each other with much the same figure are one.
    for (size_t i = 1; i < runs.size();)
    {
        auto& before = runs[i - 1];
        auto& now = runs[i];

        if (before.to == now.from && jaccard (cellsOf (before.notes), cellsOf (now.notes)) >= 0.7f)
        {
            const auto from = before.from / length, to = (now.to + length - 1) / length;
            const auto variations = before.variations + now.variations;
            runs.erase (runs.begin() + static_cast<long> (i - 1), runs.begin() + static_cast<long> (i + 1));
            const auto size = runs.size();
            addRun (from, to);

            if (runs.size() > size)
            {
                std::rotate (runs.begin() + static_cast<long> (i - 1), runs.end() - 1, runs.end());
                runs[i - 1].variations = std::max (runs[i - 1].variations, variations);
            }

            continue;
        }

        ++i;
    }

    // How each run differs from the one before it.
    for (size_t i = 1; i < runs.size(); ++i)
    {
        auto& now = runs[i];
        const auto& before = runs[i - 1];

        // The same figure as an earlier run: back to it.
        for (size_t k = 0; k + 1 < i; ++k)
            if (runs[k].bars == now.bars && jaccard (cellsOf (runs[k].notes), cellsOf (now.notes)) >= 0.8f)
            {
                now.change = "as bars " + std::to_string (runs[k].from + 1) + "-" + std::to_string (runs[k].to);
                break;
            }

        if (! now.change.empty())
            continue;

        if (stepsOf (now.notes) == stepsOf (before.notes) && now.notes.size() == before.notes.size())
        {
            const auto shift = now.notes.front().key - before.notes.front().key;
            bool transposed = shift != 0;

            for (size_t n = 0; n < now.notes.size() && transposed; ++n)
                transposed = now.notes[n].key - before.notes[n].key == shift;

            now.change = transposed ? "transposed " + std::string (shift > 0 ? "+" : "") + std::to_string (shift) : "notes";
            continue;
        }

        const auto a = cellsOf (before.notes), b = cellsOf (now.notes);

        if (jaccard (a, b) >= 0.6f)
            now.change = "varied";
        else if (std::includes (a.begin(), a.end(), b.begin(), b.end()))
            now.change = "thinner";
        else if (std::includes (b.begin(), b.end(), a.begin(), a.end()))
            now.change = "fuller";
        else if (stepsOf (now.notes) == stepsOf (before.notes))
            now.change = "notes";
        else
            now.change = "new";
    }

    return runs;
}

//==============================================================================
std::vector<Pairing> pairVoices (const std::vector<Voice>& voices, int bars)
{
    std::vector<Pairing> pairs;

    for (int i = 0; i < static_cast<int> (voices.size()); ++i)
    {
        for (int j = i + 1; j < static_cast<int> (voices.size()); ++j)
        {
            const auto& a = voices[static_cast<size_t> (i)];
            const auto& b = voices[static_cast<size_t> (j)];
            Pairing p { i, j };
            int both = 0, either = 0, bOnly = 0, bCount = 0, aOnly = 0, aCount = 0;
            std::map<int, int> intervals;
            int sounding = 0;

            // Phrases across bars: steps from the track's start.
            std::vector<GridNote> allA, allB;

            for (int bar = 0; bar < bars; ++bar)
            {
                if (! a.playsIn (bar) || ! b.playsIn (bar))
                    continue;

                ++p.bars;
                const auto na = a.notesIn (bar), nb = b.notesIn (bar);
                const auto sa = stepsOf (na), sb = stepsOf (nb);

                for (auto s : sa)
                    ++aCount, ++either, both += sb.count (s), aOnly += sb.count (s) == 0;

                for (auto s : sb)
                    ++bCount, either += sa.count (s) == 0, bOnly += sa.count (s) == 0;

                // At each note of b, the interval over a's lowest note sounding there or started
                // in the beat before.
                for (auto& n : nb)
                {
                    std::optional<int> la;

                    for (auto& m : na)
                        if ((m.step <= n.step && m.step + m.steps > n.step) || (m.step < n.step && m.step >= n.step - 4))
                            la = std::min (la.value_or (128), m.key);

                    if (la)
                        ++intervals[((n.key - *la) % 12 + 12) % 12], ++sounding;
                }

                for (auto n : na)
                    n.step += bar * 16, allA.push_back (n);

                for (auto n : nb)
                    n.step += bar * 16, allB.push_back (n);
            }

            if (p.bars == 0)
                continue;

            p.together = either > 0 ? static_cast<float> (both) / static_cast<float> (either) : 0.0f;
            p.fills = bCount > 0 ? static_cast<float> (bOnly) / static_cast<float> (bCount) : 0.0f;

            for (auto& [semitones, count] : intervals)
                p.intervals.push_back ({ semitones, static_cast<float> (count) / static_cast<float> (sounding) });

            std::sort (p.intervals.begin(), p.intervals.end(), [] (auto& x, auto& y) { return x.second > y.second; });

            if (p.intervals.size() > 3)
                p.intervals.resize (3);

            // Answering: a phrase of one (notes less than a quarter note apart) that starts within
            // 12 steps of the other's last note ending and plays while the other is silent all
            // through it, at least three steps long, so parts that just alternate note by note
            // interlock rather than answer. The one that answers more is the second.
            auto answering = [] (const std::vector<GridNote>& caller, const std::vector<GridNote>& answer, std::vector<int>& after)
            {
                int count = 0;
                auto silentAt = [&] (int step)
                {
                    return std::none_of (caller.begin(), caller.end(), [&] (auto& c) { return c.step <= step && c.step + c.steps > step; });
                };

                for (size_t i = 0; i < answer.size();)
                {
                    auto end = i + 1;

                    while (end < answer.size() && answer[end].step - (answer[end - 1].step + answer[end - 1].steps) < 4)
                        ++end;

                    const auto from = answer[i].step;
                    const auto to = answer[end - 1].step + answer[end - 1].steps;
                    std::optional<int> lastEnd;

                    for (auto& c : caller)
                        if (c.step < from && c.step + c.steps <= from && c.step + c.steps >= from - 12)
                            lastEnd = std::max (lastEnd.value_or (-1000), c.step + c.steps);

                    bool silent = true;

                    for (int step = from; step < to && silent; ++step)
                        silent = silentAt (step);

                    if (lastEnd && silent && to - from >= 3)
                        count += static_cast<int> (end - i), after.push_back (from - *lastEnd);

                    i = end;
                }

                return answer.empty() ? 0.0f : static_cast<float> (count) / static_cast<float> (answer.size());
            };

            std::vector<int> afterB, afterA;
            const auto bAnswers = answering (allA, allB, afterB), aAnswers = answering (allB, allA, afterA);

            if (p.together < 0.3f && std::max (bAnswers, aAnswers) >= 0.6f)
            {
                p.answers = true;

                if (aAnswers > bAnswers)
                {
                    std::swap (p.a, p.b);
                    p.fills = aCount > 0 ? static_cast<float> (aOnly) / static_cast<float> (aCount) : 0.0f;
                    p.answerAfter = medianOf (afterA, 0);

                    for (auto& i : p.intervals)
                        i.first = (12 - i.first) % 12;
                }
                else
                {
                    p.answerAfter = medianOf (afterB, 0);
                }
            }

            pairs.push_back (std::move (p));
        }
    }

    return pairs;
}

std::string roleOf (const std::string& part, int medianKey, float chord, float notesPerBar, float medianSteps)
{
    if (part == "drums")
        return "kick";

    if (part == "bass")
        return "bass";

    if (chord >= 1.5f)
        return medianSteps >= 8 ? "pad" : medianSteps >= 3 ? "chords" : "stab";

    if (medianSteps >= 8)
        return "pad";

    if (notesPerBar >= 7 && medianSteps <= 2)
        return "arp";

    return medianSteps <= 2 ? "pluck" : "lead";
}

std::string figureText (const std::vector<GridNote>& notes, int bars)
{
    std::ostringstream out;
    const int steps = bars * 16;

    for (int i = 0; i < steps; ++i)
    {
        if (i % 16 == 0)
            out << "|";

        std::string cell;
        bool held = false;

        for (auto& n : notes)
        {
            if (n.step == i)
                cell += (cell.empty() ? "" : "+") + library::noteName (n.key);
            else if (n.step < i && n.step + n.steps > i)
                held = true;
        }

        out << (cell.empty() ? (held ? "-" : ".") : cell) << (i % 16 == 15 ? "" : " ");
    }

    out << "|";
    return out.str();
}

std::string describeVoices (const TrackVoices& t)
{
    if (t.voices.empty())
        return {};

    std::ostringstream out;
    auto name = [&] (int i) { return std::to_string (i + 1) + " (" + t.voices[static_cast<size_t> (i)].role + ")"; };
    auto range = [] (const Voice& v) { return library::noteName (v.lowest) + (v.highest != v.lowest ? "-" + library::noteName (v.highest) : ""); };

    out << "Voices: the melodic parts heard through the track";

    if (t.key)
        out << " (" << music::keyName (t.key->key) << ")";

    out << ", each as runs of its figure in 16ths (a note name starts a note, - holds it, . is a rest, + joins notes started together; ticks in the map's voices are steps, 240 ticks a step)\n";

    for (size_t i = 0; i < t.voices.size(); ++i)
    {
        auto& v = t.voices[i];
        out << i + 1 << ". " << v.role << ", " << range (v);

        if (! v.sound.empty())
            out << ", " << v.sound;

        if (v.chord >= 1.5f)
            out << ", chords of " << std::lround (v.chord);

        out << ", " << std::lround (v.notesPerBar) << " notes a bar\n";
        const size_t shown = 6;

        for (size_t r = 0; r < v.runs.size() && r < shown; ++r)
        {
            auto& run = v.runs[r];
            out << "   bars " << run.from + 1 << "-" << run.to;

            if (! run.change.empty())
                out << " (" << run.change << ")";

            if (run.change.rfind ("as bars", 0) == 0)
            {
                out << "\n";
                continue;
            }

            out << ": " << figureText (run.notes, run.bars);

            if (run.variations > 0)
                out << " (" << run.variations << " fill" << (run.variations > 1 ? "s" : "") << ")";

            out << "\n";
        }

        if (v.runs.size() > shown)
            out << "   ... " << v.runs.size() - shown << " more runs\n";
    }

    // The pairs that say most: answers, doubling and interlocking parts, and the bass with
    // each; the rest are in the map's pairs.
    std::vector<std::string> lines;

    for (auto& p : t.pairs)
    {
        const auto withBass = t.voices[static_cast<size_t> (p.a)].part == "bass" || t.voices[static_cast<size_t> (p.b)].part == "bass";

        if (p.bars < 4 || ! (p.answers || p.together >= 0.6f || p.fills >= 0.8f || withBass))
            continue;

        std::ostringstream line;
        line << name (p.b) << " with " << name (p.a) << ", " << p.bars << " bars: ";

        if (p.answers)
            line << "answers it " << p.answerAfter << " 16ths after its phrase";
        else if (p.together >= 0.6f)
            line << "plays with it (" << std::lround (p.together * 100) << "% of their notes together)";
        else
            line << std::lround (p.together * 100) << "% of notes together, " << std::lround (p.fills * 100) << "% of its notes in the other's gaps";

        if (! p.intervals.empty())
        {
            line << "; over it by";

            for (auto& [semitones, share] : p.intervals)
                line << " " << semitones << " (" << std::lround (share * 100) << "%)";
        }

        lines.push_back (line.str());
    }

    if (! lines.empty())
    {
        out << "Together:\n";

        for (auto& l : lines)
            out << "- " << l << "\n";
    }

    return out.str();
}

//==============================================================================
namespace
{
    std::string soundOf (const std::vector<const SoundedNote*>& notes)
    {
        if (notes.empty())
            return {};

        std::array<std::vector<float>, 8> h;
        std::vector<float> air;

        for (auto* n : notes)
        {
            for (size_t k = 0; k < 8; ++k)
                h[k].push_back (n->harmonics[k]);

            air.push_back (n->air);
        }

        std::array<float, 8> m {};

        for (size_t k = 0; k < 8; ++k)
            m[k] = medianOf (h[k], -60.0f);

        const auto odd = (m[2] + m[4] + m[6]) / 3, even = (m[1] + m[3] + m[5] + m[7]) / 4;
        const auto upper = (m[1] + m[2] + m[3] + m[4] + m[5] + m[6] + m[7]) / 7;
        const auto a = medianOf (air, -60.0f);
        std::string s = upper < -40 ? "pure, sine-like" : odd - even > 12 ? "hollow, square-like" : "full, saw-like";

        if (a > -18)
            s += ", bright and noisy";

        return s;
    }

    /** The features a note's voice is told by: the shape of its harmonics (odd against even, the
        slope) and its air. */
    std::array<float, 3> timbreOf (const SoundedNote& n)
    {
        const auto& h = n.harmonics;
        const auto odd = (h[2] + h[4] + h[6]) / 3, even = (h[1] + h[3] + h[5] + h[7]) / 4;

        // The least-squares slope of the harmonics against log2 of their number.
        double sx = 0, sy = 0, sxx = 0, sxy = 0;

        for (int k = 0; k < 8; ++k)
        {
            const auto x = std::log2 (k + 1.0);
            sx += x, sy += h[static_cast<size_t> (k)], sxx += x * x, sxy += x * h[static_cast<size_t> (k)];
        }

        const auto slope = static_cast<float> ((8 * sxy - sx * sy) / (8 * sxx - sx * sx));
        return { (odd - even) / 10, slope / 6, (n.air + 30) / 10 };
    }
}

TrackVoices hearVoices (NoteTranscriber& transcriber, const Arrangement& a, const Separator::Stems& stems)
{
    TrackVoices result;

    if (a.bars < 2 || a.tempo <= 0)
        return result;

    const auto rate = Separator::sampleRate;
    const auto sixteenth = a.barSeconds() / 16;
    const auto bars = a.bars;
    auto toStep = [&] (double t) { return static_cast<int> (std::lround ((t - a.downbeat) / sixteenth)); };
    auto toSteps = [&] (double seconds) { return std::max (1, static_cast<int> (std::lround (seconds / sixteenth))); };
    auto velocityOf = [] (float level) { return std::clamp (static_cast<int> (std::lround (50 + 77 * level)), 1, 127); };

    std::vector<music::HeardNote> everything;
    std::vector<int> sectionStarts;

    for (auto& section : a.sections)
        sectionStarts.push_back (section.start);

    auto makeVoice = [&] (const std::string& part, const std::vector<GridNote>& notes, const std::vector<const SoundedNote*>& sounded)
    {
        Voice v;
        v.part = part;
        std::vector<int> keys, lengths;
        std::vector<float> levels;
        std::map<int, std::set<int>> onsets;
        std::set<int> barsPlayed;

        for (auto& n : notes)
        {
            keys.push_back (n.key), lengths.push_back (n.steps);
            onsets[n.step].insert (n.key);
            barsPlayed.insert (n.step / 16);
        }

        for (auto* s : sounded)
            levels.push_back (s->note.level);

        std::sort (keys.begin(), keys.end());
        v.lowest = keys[keys.size() / 20];
        v.highest = keys[keys.size() - 1 - keys.size() / 20];
        v.level = medianOf (levels, 1.0f);
        v.chord = static_cast<float> (notes.size()) / static_cast<float> (std::max<size_t> (1, onsets.size()));
        v.notesPerBar = static_cast<float> (notes.size()) / static_cast<float> (std::max<size_t> (1, barsPlayed.size()));
        v.role = roleOf (part, keys[keys.size() / 2], v.chord, v.notesPerBar, static_cast<float> (medianOf (lengths, 1)));
        v.sound = part == "music" ? soundOf (sounded) : std::string();
        v.runs = foldRuns (notes, bars, sectionStarts);
        return v;
    };

    // The bass, one note at a time.
    std::vector<SoundedNote> bass;

    if (stems.bass.getNumSamples() > 0)
    {
        bass = hearBass (stems.bass, rate);
        float loudest = 0;

        for (auto& n : bass)
            loudest = std::max (loudest, n.note.level);

        std::vector<GridNote> notes;
        std::vector<const SoundedNote*> kept;

        for (auto& n : bass)
        {
            const auto step = toStep (n.note.start);

            if (n.note.level < 0.2f * loudest || step < 0 || step >= bars * 16)
                continue;

            notes.push_back ({ step, toSteps (n.note.end - n.note.start), static_cast<int> (std::lround (n.note.pitch)), velocityOf (n.note.level / loudest) });
            kept.push_back (&n);
            everything.push_back (n.note);
        }

        if (notes.size() >= 8)
            result.voices.push_back (makeVoice ("bass", notes, kept));
    }

    // The music: the stem less its vocals, every note, told apart into voices.
    if (stems.other.getNumSamples() > 0)
    {
        juce::AudioBuffer<float> music (stems.other);

        if (stems.vocals.getNumSamples() == music.getNumSamples())
            for (int c = 0; c < std::min (music.getNumChannels(), stems.vocals.getNumChannels()); ++c)
                music.addFrom (c, 0, stems.vocals, c, 0, music.getNumSamples(), -1.0f);

        auto heard = hearNotes (transcriber, music, rate);

        // A held note the model heard in pieces (a detuned pad beats): a piece that starts off the
        // grid right where one of the same key ends is that note going on.
        {
            std::sort (heard.begin(), heard.end(), [] (auto& x, auto& y) { return x.note.start < y.note.start; });
            std::vector<SoundedNote> joined;

            for (auto& n : heard)
            {
                const auto position = (n.note.start - a.downbeat) / sixteenth;
                const auto offGrid = std::abs (position - std::round (position)) > 0.25;
                auto held = std::find_if (joined.rbegin(), joined.rend(), [&] (auto& j)
                                          { return std::lround (j.note.pitch) == std::lround (n.note.pitch) && std::abs (n.note.start - j.note.end) < 0.06; });

                if (offGrid && held != joined.rend())
                    held->note.end = std::max (held->note.end, n.note.end);
                else
                    joined.push_back (n);
            }

            heard = std::move (joined);
        }

        float loudest = 0;

        for (auto& n : heard)
            loudest = std::max (loudest, n.note.level);

        struct Placed
        {
            GridNote grid;
            const SoundedNote* sounded;
            std::array<float, 3> timbre;
        };

        std::vector<Placed> placed;

        for (auto& n : heard)
        {
            const auto step = toStep (n.note.start);
            const auto key = static_cast<int> (std::lround (n.note.pitch));

            if (n.note.level < 0.2f * loudest || step < 0 || step >= bars * 16)
                continue;

            // The bass coming through: low, with a bass note of the same name at the same time.
            if (key < 48 && std::any_of (bass.begin(), bass.end(), [&] (auto& b)
                                         { return std::abs (b.note.start - n.note.start) < 0.06 && std::lround (b.note.pitch) % 12 == key % 12; }))
                continue;

            placed.push_back ({ { step, toSteps (n.note.end - n.note.start), key, velocityOf (n.note.level / loudest) }, &n, timbreOf (n) });
            everything.push_back (n.note);
        }

        // Cells: a note at a place in a two-bar cycle, heard in two cycles or more, with the mean
        // of its sound, the length of its notes and its key.
        std::map<Cell, std::vector<size_t>> cells;

        for (size_t i = 0; i < placed.size(); ++i)
            cells[{ placed[i].grid.step % 32, placed[i].grid.key }].push_back (i);

        struct CellInfo
        {
            Cell cell;
            std::vector<size_t> notes;
            std::array<float, 3> timbre {};
            float lengthLog = 0;
        };

        std::vector<CellInfo> infos;

        for (auto& [cell, list] : cells)
        {
            std::set<int> cycles;

            for (auto i : list)
                cycles.insert (placed[i].grid.step / 32);

            if (cycles.size() < 2)
                continue;

            CellInfo info { cell, list };
            std::vector<int> lengths;

            for (auto i : list)
            {
                for (size_t k = 0; k < 3; ++k)
                    info.timbre[k] += placed[i].timbre[k] / static_cast<float> (list.size());

                lengths.push_back (placed[i].grid.steps);
            }

            info.lengthLog = std::log2 (static_cast<float> (medianOf (lengths, 1)));
            infos.push_back (std::move (info));
        }

        // The most played cells, so the grouping stays quick on a busy track.
        constexpr size_t maxCells = 400;

        if (infos.size() > maxCells)
        {
            std::sort (infos.begin(), infos.end(), [] (auto& x, auto& y) { return x.notes.size() > y.notes.size(); });
            infos.resize (maxCells);
        }

        auto distance = [] (const std::array<float, 3>& ta, int ka, float la, const std::array<float, 3>& tb, int kb, float lb)
        {
            float d = 0;

            for (size_t k = 0; k < 3; ++k)
                d += (ta[k] - tb[k]) * (ta[k] - tb[k]);

            return std::sqrt (d) + std::max (0.0f, static_cast<float> (std::abs (ka - kb)) - 7) / 6 + std::abs (la - lb) / 2;
        };

        // Grouped by average linkage, up to a distance.
        constexpr float threshold = 2.0f;
        const auto n = infos.size();
        std::vector<std::vector<float>> d (n, std::vector<float> (n, 0.0f));

        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 1; j < n; ++j)
                d[i][j] = d[j][i] = distance (infos[i].timbre, infos[i].cell.second, infos[i].lengthLog, infos[j].timbre, infos[j].cell.second, infos[j].lengthLog);

        std::vector<std::vector<size_t>> groups (n);
        std::vector<bool> alive (n, true);

        for (size_t i = 0; i < n; ++i)
            groups[i] = { i };

        for (;;)
        {
            float best = std::numeric_limits<float>::max();
            size_t ba = 0, bb = 0;

            for (size_t i = 0; i < n; ++i)
                for (size_t j = i + 1; j < n && alive[i]; ++j)
                    if (alive[j] && d[i][j] < best)
                        best = d[i][j], ba = i, bb = j;

            if (best > threshold)
                break;

            // Lance-Williams for average linkage.
            const auto na = static_cast<float> (groups[ba].size()), nb = static_cast<float> (groups[bb].size());

            for (size_t k = 0; k < n; ++k)
                if (alive[k] && k != ba && k != bb)
                    d[ba][k] = d[k][ba] = (na * d[ba][k] + nb * d[bb][k]) / (na + nb);

            groups[ba].insert (groups[ba].end(), groups[bb].begin(), groups[bb].end());
            alive[bb] = false;
        }

        // Each group big enough is a voice.
        const auto minimum = std::max<size_t> (8, placed.size() * 3 / 100);

        for (size_t g = 0; g < n; ++g)
        {
            if (! alive[g])
                continue;

            std::vector<GridNote> notes;
            std::vector<const SoundedNote*> sounded;

            for (auto c : groups[g])
                for (auto i : infos[c].notes)
                    notes.push_back (placed[i].grid), sounded.push_back (placed[i].sounded);

            if (notes.size() < minimum)
                continue;

            std::sort (notes.begin(), notes.end(), [] (auto& x, auto& y) { return x.step != y.step ? x.step < y.step : x.key < y.key; });
            result.voices.push_back (makeVoice ("music", notes, sounded));
        }
    }

    // A kick that changes pitch: its pitch on the beats where it plays, from the drums' low end.
    if (stems.drums.getNumSamples() > 0 && a.parts.count ("kick") > 0)
    {
        constexpr double pitchRate = 4000;
        const auto low = resampleMono (stems.drums, rate, pitchRate);
        const auto frames = music::trackPitch (low.data(), static_cast<int> (low.size()), pitchRate, 32, 300);
        const auto& lane = a.parts.at ("kick");
        std::vector<GridNote> notes;
        std::map<int, int> counts;

        for (int bar = 0; bar < bars; ++bar)
        {
            if (static_cast<size_t> (bar) >= lane.on.size() || ! lane.on[static_cast<size_t> (bar)])
                continue;

            for (int beat = 0; beat < 4; ++beat)
            {
                const auto t = a.downbeat + (bar * 16 + beat * 4) * sixteenth;
                std::vector<float> pitches;

                for (auto& f : frames)
                    if (f.time >= t + 0.03 && f.time <= t + 0.09 && f.pitch > 0)
                        pitches.push_back (f.pitch);

                if (pitches.size() < 3)
                    continue;

                const auto key = static_cast<int> (std::lround (medianOf (pitches, 0.0f)));
                notes.push_back ({ bar * 16 + beat * 4, 1, key, 100 });
                ++counts[key];
            }
        }

        // Pitched when two notes or more each take a tenth of the hits, two semitones apart or more.
        std::vector<int> common;

        for (auto& [key, count] : counts)
            if (count * 10 >= static_cast<int> (notes.size()))
                common.push_back (key);

        if (common.size() >= 2 && common.back() - common.front() >= 2)
        {
            std::erase_if (notes, [&] (auto& n) { return std::find (common.begin(), common.end(), n.key) == common.end(); });
            result.voices.insert (result.voices.begin(), makeVoice ("drums", notes, {}));
        }
    }

    // In order from the bottom: the kick, the bass, then the music by register.
    std::stable_sort (result.voices.begin(), result.voices.end(), [] (auto& x, auto& y)
    {
        const auto rank = [] (const Voice& v) { return v.part == "drums" ? 0 : v.part == "bass" ? 1 : 2; };
        return rank (x) != rank (y) ? rank (x) < rank (y) : x.lowest + x.highest < y.lowest + y.highest;
    });

    result.key = music::keyOfNotes (everything);
    result.pairs = pairVoices (result.voices, bars);
    return result;
}

} // namespace gk::listening
