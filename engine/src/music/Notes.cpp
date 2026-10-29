#include "Notes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace gk::music
{

namespace
{
    float median (std::vector<float> v)
    {
        if (v.empty())
            return 0;

        const auto mid = v.begin() + static_cast<std::ptrdiff_t> (v.size() / 2);
        std::nth_element (v.begin(), mid, v.end());
        return *mid;
    }

    float toMidi (double hz)    { return static_cast<float> (69.0 + 12.0 * std::log2 (hz / 440.0)); }

    /** How much a note's pitch wanders, in semitones (its frames' spread around their median). */
    float wander (const std::vector<PitchFrame>& frames, const HeardNote& n)
    {
        std::vector<float> pitches;

        for (auto& f : frames)
            if (f.time >= n.start && f.time < n.end && f.pitch > 0)
                pitches.push_back (f.pitch);

        if (pitches.size() < 3)
            return 1;

        const auto m = median (pitches);
        std::vector<float> deviations;

        for (auto p : pitches)
            deviations.push_back (std::abs (p - m));

        return median (deviations);
    }
}

//==============================================================================
std::vector<PitchFrame> trackPitch (const float* x, int numSamples, double sampleRate, double minHz, double maxHz)
{
    const auto hop = std::max (1, static_cast<int> (sampleRate * 0.01));
    const auto tauMin = std::max (2, static_cast<int> (sampleRate / maxHz));
    const auto tauMax = static_cast<int> (sampleRate / minHz);
    const auto length = tauMax + std::max (64, static_cast<int> (sampleRate * 0.015));
    const auto window = length + tauMax;

    std::vector<PitchFrame> frames;
    std::vector<double> d (static_cast<size_t> (tauMax + 1));
    float loudest = 0;

    for (int start = 0; start + window <= numSamples; start += hop)
    {
        PitchFrame f;
        f.time = (start + window / 2) / sampleRate;

        double energy = 0;
        for (int j = 0; j < window; ++j) energy += static_cast<double> (x[start + j]) * x[start + j];
        f.level = static_cast<float> (std::sqrt (energy / window));
        loudest = std::max (loudest, f.level);

        if (f.level > 1.0e-4f)
        {
            const float* s = x + start;
            double running = 0;
            int best = -1;

            for (int tau = 1; tau <= tauMax; ++tau)
            {
                double sum = 0;

                for (int j = 0; j < length; ++j)
                {
                    const double diff = s[j] - s[j + tau];
                    sum += diff * diff;
                }

                running += sum;
                d[static_cast<size_t> (tau)] = running > 0 ? sum * tau / running : 1.0;
            }

            for (int tau = tauMin; tau < tauMax; ++tau)
            {
                if (d[static_cast<size_t> (tau)] < 0.2)
                {
                    while (tau + 1 < tauMax && d[static_cast<size_t> (tau + 1)] < d[static_cast<size_t> (tau)])
                        ++tau;

                    best = tau;
                    break;
                }
            }

            if (best > 0)
            {
                double tau = best;
                const auto a = d[static_cast<size_t> (best - 1)], b = d[static_cast<size_t> (best)], c = d[static_cast<size_t> (best + 1)];

                if (const auto denom = a - 2 * b + c; std::abs (denom) > 1.0e-12)
                    tau += 0.5 * (a - c) / denom;

                f.clarity = static_cast<float> (1.0 - b);
                f.pitch = toMidi (sampleRate / tau);
            }
        }

        frames.push_back (f);
    }

    // Quiet frames (breath, room) have no pitch, however periodic.
    for (auto& f : frames)
        if (f.level < loudest * 0.06f)
            f.pitch = 0;

    // A lone frame an octave off its neighbours is folded back (YIN's usual slip).
    for (size_t i = 1; i + 1 < frames.size(); ++i)
    {
        const auto before = frames[i - 1].pitch, after = frames[i + 1].pitch;
        auto& p = frames[i].pitch;

        if (p > 0 && before > 0 && after > 0 && std::abs (before - after) < 1)
        {
            if (std::abs (p - 12 - before) < 1)         p -= 12;
            else if (std::abs (p + 12 - before) < 1)    p += 12;
        }
    }

    return frames;
}

//==============================================================================
std::vector<HeardNote> segmentNotes (const std::vector<PitchFrame>& frames)
{
    std::vector<std::pair<size_t, size_t>> runs;   // frame ranges [first, last)

    // Voiced runs, broken where the pitch moves to another note and stays there.
    size_t i = 0;

    while (i < frames.size())
    {
        if (frames[i].pitch <= 0)
        {
            ++i;
            continue;
        }

        const auto first = i;
        std::vector<float> recent { frames[i].pitch };
        ++i;

        while (i < frames.size() && frames[i].pitch > 0)
        {
            const auto reference = median (recent);

            // Three frames in a row away from the note: a new one.
            if (std::abs (frames[i].pitch - reference) > 0.7f
                && i + 2 < frames.size()
                && std::abs (frames[i + 1].pitch - reference) > 0.7f && std::abs (frames[i + 2].pitch - reference) > 0.7f
                && frames[i + 1].pitch > 0 && frames[i + 2].pitch > 0)
                break;

            recent.push_back (frames[i].pitch);

            if (recent.size() > 6)
                recent.erase (recent.begin());

            ++i;
        }

        runs.push_back ({ first, i });
    }

    // Within a run, a dip in level between two louder stretches is a consonant: "da da".
    std::vector<std::pair<size_t, size_t>> split;

    for (auto [first, last] : runs)
    {
        auto from = first;

        for (auto k = first + 3; k + 3 < last; ++k)
        {
            const auto level = frames[k].level;

            if (level > frames[k - 1].level || level > frames[k + 1].level)
                continue;

            float before = 0, after = 0;
            for (auto b = from; b < k; ++b) before = std::max (before, frames[b].level);
            for (auto a = k + 1; a < last; ++a) after = std::max (after, frames[a].level);

            if (k - from >= 4 && last - k >= 4 && level < 0.55f * before && level < 0.55f * after)
            {
                split.push_back ({ from, k });
                from = k + 1;
            }
        }

        split.push_back ({ from, last });
    }

    std::vector<HeardNote> notes;
    float loudest = 0;

    for (auto [first, last] : split)
    {
        if (last <= first)
            continue;

        HeardNote n;
        n.start = frames[first].time - 0.005;
        n.end = frames[last - 1].time + 0.005;

        if (n.end - n.start < 0.07)
            continue;

        // The pitch of its steady middle, leaving the slides in and out.
        const auto count = last - first, skip = count / 5;
        std::vector<float> pitches;

        for (auto k = first + skip; k < last - skip; ++k)
            pitches.push_back (frames[k].pitch);

        n.pitch = median (pitches);

        for (auto k = first; k < last; ++k)
            n.level = std::max (n.level, frames[k].level);

        loudest = std::max (loudest, n.level);
        notes.push_back (n);
    }

    for (auto& n : notes)
        n.level = loudest > 0 ? n.level / loudest : 1;

    return notes;
}

std::optional<Span> findSungPart (const std::vector<PitchFrame>& frames)
{
    const auto notes = segmentNotes (frames);

    // Sung notes last and hold their pitch; speech glides through short syllables.
    std::vector<bool> steady;

    for (auto& n : notes)
        steady.push_back (n.end - n.start >= 0.12 && wander (frames, n) < 0.3f);

    std::optional<Span> best;
    double bestLength = 0;

    // Runs of steady notes without long gaps (one wobbly note among them is let through).
    for (size_t first = 0; first < notes.size(); ++first)
    {
        if (! steady[first])
            continue;

        auto last = first;
        int count = 1, wobbly = 0;
        double held = notes[first].end - notes[first].start;

        while (last + 1 < notes.size() && notes[last + 1].start - notes[last].end < 0.7)
        {
            const auto next = last + 1;

            if (! steady[next])
            {
                if (wobbly > 0 || next + 1 >= notes.size() || ! steady[next + 1])
                    break;

                ++wobbly;
            }
            else
            {
                ++count;
                held += notes[next].end - notes[next].start;
            }

            last = next;
        }

        if (count >= 3 && held > bestLength)
        {
            bestLength = held;
            best = Span { notes[first].start, notes[last].end };
        }

        first = last;
    }

    return best;
}

//==============================================================================
namespace
{
    std::vector<HeardNote> extremeLine (std::vector<HeardNote> notes, bool highest)
    {
        std::vector<double> times;

        for (auto& n : notes)
        {
            times.push_back (n.start);
            times.push_back (n.end);
        }

        std::sort (times.begin(), times.end());
        times.erase (std::unique (times.begin(), times.end()), times.end());

        std::vector<HeardNote> line;
        const HeardNote* current = nullptr;

        for (size_t i = 0; i + 1 < times.size(); ++i)
        {
            const auto mid = 0.5 * (times[i] + times[i + 1]);
            const HeardNote* pick = nullptr;

            for (auto& n : notes)
                if (n.start <= mid && n.end > mid && (pick == nullptr || (highest ? n.pitch > pick->pitch : n.pitch < pick->pitch)))
                    pick = &n;

            if (pick != nullptr && pick == current && ! line.empty())
            {
                line.back().end = times[i + 1];
            }
            else if (pick != nullptr)
            {
                auto part = *pick;
                part.start = times[i];
                part.end = times[i + 1];

                // A note that was covered by another and comes out from under it carries on
                // rather than starting again.
                if (! line.empty() && std::abs (line.back().pitch - part.pitch) < 0.5f && std::abs (line.back().end - part.start) < 1.0e-6
                     && part.start > pick->start)
                    line.back().end = part.end;
                else
                    line.push_back (part);
            }

            current = pick;
        }

        std::erase_if (line, [] (auto& n) { return n.end - n.start < 0.06; });
        return line;
    }
}

std::vector<HeardNote> topLine (std::vector<HeardNote> notes)       { return extremeLine (std::move (notes), true); }
std::vector<HeardNote> bottomLine (std::vector<HeardNote> notes)    { return extremeLine (std::move (notes), false); }

std::optional<KeyEstimate> keyOfNotes (const std::vector<HeardNote>& notes)
{
    std::array<float, 12> chroma {};

    for (auto& n : notes)
        chroma[static_cast<size_t> (((static_cast<int> (std::lround (n.pitch)) % 12) + 12) % 12)] += static_cast<float> (n.end - n.start) * (0.3f + n.level);

    return estimateKey (chroma);
}

//==============================================================================
namespace
{
    /** How badly onsets miss a grid of step seconds, starting at the first: the mean squared
        miss, in steps (0 on the grid, about 0.08 for onsets anywhere). */
    double gridMiss (const std::vector<double>& onsets, double step)
    {
        double sum = 0;

        for (auto t : onsets)
        {
            const auto steps = (t - onsets.front()) / step;
            const auto miss = steps - std::round (steps);
            sum += miss * miss;
        }

        return sum / static_cast<double> (onsets.size());
    }

    double estimateTempo (const std::vector<HeardNote>& notes, double songTempo, double grid)
    {
        std::vector<double> onsets;

        for (auto& n : notes)
            if (onsets.empty() || n.start - onsets.back() > 0.05)
                onsets.push_back (n.start);

        if (onsets.size() < 3)
            return songTempo;

        // The tempo that puts the onsets on the grid in the simplest rhythm (8ths, quarters and
        // halves before 16ths, before dotted notes, before anything else); of those about as
        // good, the nearest to the song's own (or its double or half), since a melody is
        // usually hummed along with the song in mind.
        const auto complexity = [&] (double step)
        {
            double sum = 0;

            for (size_t i = 1; i < onsets.size(); ++i)
            {
                const auto steps = std::llround ((onsets[i] - onsets[i - 1]) / step);
                sum += steps == 2 || steps == 4 || steps == 8 || steps == 16 ? 0.0
                     : steps == 1 || steps == 12 ? 0.01
                     : steps == 3 || steps == 6 ? 0.02
                     : 0.035;
            }

            return sum / static_cast<double> (onsets.size() - 1);
        };

        struct Candidate { double bpm, miss; };
        std::vector<Candidate> candidates;

        for (double bpm = 70; bpm <= 180; bpm += 0.25)
        {
            const auto step = 60.0 / bpm * grid;
            candidates.push_back ({ bpm, gridMiss (onsets, step) + complexity (step) });
        }

        const auto best = std::min_element (candidates.begin(), candidates.end(), [] (auto& a, auto& b) { return a.miss < b.miss; })->miss;
        const auto distance = [&] (double bpm)
        {
            auto d = 1.0e9;

            for (auto factor : { 0.5, 1.0, 2.0 })
                d = std::min (d, std::abs (std::log2 (bpm / (songTempo * factor))));

            return d;
        };

        double chosen = songTempo, chosenDistance = 1.0e9;

        for (auto& c : candidates)
        {
            if (c.miss <= best + 0.006 && distance (c.bpm) < chosenDistance)
            {
                chosen = c.bpm;
                chosenDistance = distance (c.bpm);
            }
        }

        return chosen;
    }
}

Melody fitMelody (const std::vector<HeardNote>& heard, const FitOptions& options)
{
    Melody melody;

    if (heard.empty())
        return melody;

    auto notes = heard;
    std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.start < b.start; });

    melody.sourceTempo = options.sourceTempo.value_or (estimateTempo (notes, options.songTempo, options.grid));
    melody.heardKey = keyOfNotes (notes);

    const auto step = 60.0 / melody.sourceTempo * options.grid;
    const auto ticksPerStep = static_cast<int64_t> (std::llround (options.grid * 960));
    const auto origin = notes.front().start;

    // Into the song's key, then onto its scale.
    double shift = 0;

    if (options.key && melody.heardKey)
        shift = semitonesBetween (melody.heardKey->key, *options.key);

    std::vector<int> keys;

    for (auto& n : notes)
    {
        auto key = static_cast<int> (std::lround (n.pitch + shift));

        if (options.key && options.snapToScale)
        {
            // The scale note nearest the sung pitch, its fraction included.
            const auto exact = n.pitch + shift;
            const auto down = options.key->snap (static_cast<int> (std::floor (exact)));
            const auto up = options.key->snap (static_cast<int> (std::ceil (exact)));
            key = std::abs (exact - static_cast<float> (down)) <= std::abs (static_cast<float> (up) - exact) ? down : up;
        }

        keys.push_back (key);
    }

    // Whole octaves into the range.
    int octaves = 0;

    if (options.lowest && options.highest && ! keys.empty())
    {
        auto sorted = keys;
        std::nth_element (sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t> (sorted.size() / 2), sorted.end());
        const auto middle = sorted[sorted.size() / 2];
        octaves = static_cast<int> (std::lround (((*options.lowest + *options.highest) / 2.0 - middle) / 12.0));
    }

    melody.transposed = static_cast<int> (std::lround (shift)) + 12 * octaves;

    for (size_t i = 0; i < notes.size(); ++i)
    {
        PlacedNote p;
        const auto startStep = std::llround ((notes[i].start - origin) / step);
        const auto endStep = std::llround ((notes[i].end - origin) / step);
        p.start = startStep * ticksPerStep;
        p.length = std::max<int64_t> (1, endStep - startStep) * ticksPerStep;
        p.key = std::clamp (keys[i] + 12 * octaves, 0, 127);
        p.velocity = std::clamp (static_cast<int> (std::lround (50 + 77 * notes[i].level)), 1, 127);
        melody.notes.push_back (p);
    }

    // A note ends where the next one at another time starts, so a line doesn't overlap itself.
    for (size_t i = 0; i + 1 < melody.notes.size(); ++i)
    {
        auto next = std::find_if (melody.notes.begin() + static_cast<std::ptrdiff_t> (i + 1), melody.notes.end(),
                                  [&] (auto& n) { return n.start > melody.notes[i].start; });

        if (next != melody.notes.end() && melody.notes[i].start + melody.notes[i].length > next->start
             && std::count_if (melody.notes.begin(), melody.notes.end(), [&] (auto& n) { return n.start == melody.notes[i].start; }) == 1)
            melody.notes[i].length = next->start - melody.notes[i].start;
    }

    // Two notes on the same step and key are one.
    std::sort (melody.notes.begin(), melody.notes.end(), [] (auto& a, auto& b) { return std::tie (a.start, a.key) < std::tie (b.start, b.key); });
    melody.notes.erase (std::unique (melody.notes.begin(), melody.notes.end(), [] (auto& a, auto& b) { return a.start == b.start && a.key == b.key; }),
                        melody.notes.end());

    int64_t end = 0;

    for (auto& n : melody.notes)
        end = std::max (end, n.start + n.length);

    constexpr int64_t bar = 3840;
    melody.length = std::max<int64_t> (bar, (end + bar - 1) / bar * bar);
    return melody;
}

bool mentionsMelody (const std::string& transcript)
{
    std::string text;

    for (auto ch : transcript)
        text += static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));

    // Words that say a melody follows or was just sung (English and German).
    static const char* const words[] = {
        "melody", "melodie", "hum", "hums", "humming", "hummed", "sing", "singing", "sung", "summe", "summen", "gesummt",
        "da da", "dada", "la la", "na na", "dum dum", "tune", "riff", "hook", "bassline", "bass line",
        "goes like", "like this", "this line", "so geht", "die linie",
    };

    return std::any_of (std::begin (words), std::end (words), [&] (const char* w)
    {
        // Whole words, so "hum" isn't found in "human".
        for (auto at = text.find (w); at != std::string::npos; at = text.find (w, at + 1))
        {
            const auto end = at + std::char_traits<char>::length (w);
            const auto startsWord = at == 0 || ! std::isalpha (static_cast<unsigned char> (text[at - 1]));
            const auto endsWord = end >= text.size() || ! std::isalpha (static_cast<unsigned char> (text[end]));

            if (startsWord && endsWord)
                return true;
        }

        return false;
    });
}

} // namespace gk::music
