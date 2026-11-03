#include "Versions.h"

#include "model/JsonIO.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>

namespace gk::music
{

namespace
{
    constexpr int64_t step = 240;           // a 16th
    constexpr int64_t bar = 16 * step;

    std::set<int> onsetsOf (const std::vector<PlacedNote>& notes, int steps)
    {
        std::set<int> out;

        for (auto& n : notes)
            out.insert (static_cast<int> (((n.start / step) % steps + steps) % steps));

        return out;
    }

    /** How many onsets two rhythms share, within a 16th either way: F1 of the matches. */
    float rhythmMatch (const std::vector<PlacedNote>& a, const std::vector<PlacedNote>& b)
    {
        if (a.empty() || b.empty())
            return 0;

        std::vector<bool> used (b.size(), false);
        int matches = 0;

        for (auto& x : a)
        {
            for (size_t j = 0; j < b.size(); ++j)
            {
                if (! used[j] && std::abs (x.start - b[j].start) <= step)
                {
                    used[j] = true;
                    ++matches;
                    break;
                }
            }
        }

        return 2.0f * static_cast<float> (matches) / static_cast<float> (a.size() + b.size());
    }

    /** How alike the ups and downs are, step by step. */
    float contourMatch (const std::vector<PlacedNote>& a, const std::vector<PlacedNote>& b)
    {
        const auto n = std::min (a.size(), b.size());

        if (n < 2)
            return a.size() == b.size() ? 1.0f : 0.5f;

        int same = 0;

        for (size_t i = 1; i < n; ++i)
        {
            auto sign = [] (int d) { return (d > 0) - (d < 0); };
            same += sign (a[i].key - a[i - 1].key) == sign (b[i].key - b[i - 1].key) ? 1 : 0;
        }

        return static_cast<float> (same) / static_cast<float> (n - 1);
    }

    /** The best overlap (Jaccard) of a line's onsets with one of the style's rhythms, 0-100. */
    std::optional<float> styleFit (const MelodyVersion& v, const std::vector<VersionOptions::Rhythm>& rhythms)
    {
        if (rhythms.empty() || v.notes.empty())
            return {};

        float best = 0;

        for (auto& r : rhythms)
        {
            const auto mine = onsetsOf (v.notes, r.steps);
            const std::set<int> theirs (r.onsets.begin(), r.onsets.end());
            int both = 0;

            for (auto o : mine)
                both += theirs.contains (o) ? 1 : 0;

            const auto either = static_cast<int> (mine.size() + theirs.size()) - both;
            best = std::max (best, either > 0 ? static_cast<float> (both) / static_cast<float> (either) : 0.0f);
        }

        return std::round (best * 100);
    }

    /** Notes no longer than up to the next one, and not past the end. */
    void tidy (std::vector<PlacedNote>& notes, int64_t length)
    {
        std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.start < b.start; });
        notes.erase (std::unique (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.start == b.start; }), notes.end());
        std::erase_if (notes, [&] (auto& n) { return n.start < 0 || n.start >= length; });

        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto next = i + 1 < notes.size() ? notes[i + 1].start : length;
            notes[i].length = std::clamp<int64_t> (notes[i].length, step / 2, std::max<int64_t> (step / 2, next - notes[i].start));
        }
    }

    /** Looped (or cut) to a number of bars. */
    void loopTo (MelodyVersion& v, int bars)
    {
        const auto length = bars * bar;

        if (v.length <= 0 || v.notes.empty())
            return;

        std::vector<PlacedNote> out;

        for (int64_t offset = 0; offset < length; offset += v.length)
            for (auto n : v.notes)
                if (n.start + offset < length)
                    n.start += offset, out.push_back (n);

        v.notes = std::move (out);
        v.length = length;
        tidy (v.notes, length);
    }

    /** The key's scale as MIDI notes, low to high. */
    std::vector<int> scaleNotes (Key key)
    {
        std::vector<int> out;

        for (int n = 12; n < 120; ++n)
            if (key.contains (n % 12))
                out.push_back (n);

        return out;
    }
}

std::vector<MelodyVersion> melodyVersions (const Melody& hummed, const Melody& raw, const VersionOptions& options)
{
    std::vector<MelodyVersion> versions;

    if (hummed.notes.empty())
        return versions;

    const auto length = std::max<int64_t> (hummed.length, bar);
    const auto feel = hummed.feel == FitOptions::Feel::triplet ? ", in triplets" : hummed.feel == FitOptions::Feel::swing ? ", swung" : "";
    const MelodyVersion base { "hummed", std::string (options.key ? "As you hummed it, in key" : "As you hummed it, on the grid") + feel, hummed.notes, length };
    versions.push_back (base);

    // The hum's rhythm and shape, on the key's notes: each interval sung, in steps of the scale.
    if (options.key && raw.notes.size() == hummed.notes.size())
    {
        const auto scale = scaleNotes (*options.key);
        auto v = base;
        v.name = "scale";
        v.label = "Your rhythm and shape, on the key's notes";
        auto at = std::distance (scale.begin(), std::lower_bound (scale.begin(), scale.end(), options.key->snap (raw.notes[0].key)));

        for (size_t i = 0; i < v.notes.size(); ++i)
        {
            if (i > 0)
            {
                const auto interval = raw.notes[i].key - raw.notes[i - 1].key;
                auto steps = static_cast<long> (std::lround (interval / 1.7));

                if (interval != 0 && steps == 0)
                    steps = interval > 0 ? 1 : -1;

                at = std::clamp<long> (at + steps, 0, static_cast<long> (scale.size()) - 1);
            }

            v.notes[i].key = scale[static_cast<size_t> (at)];
        }

        versions.push_back (v);
    }

    // Its notes on the style's nearest rhythm: each onset moved to the closest the style plays,
    // within an eighth.
    if (! options.styleRhythms.empty())
    {
        const VersionOptions::Rhythm* best = nullptr;
        float bestFit = -1;

        for (auto& r : options.styleRhythms)
        {
            MelodyVersion probe { {}, {}, base.notes, length };
            const auto f = styleFit (probe, { r }).value_or (0);

            if (f > bestFit)
                bestFit = f, best = &r;
        }

        auto v = base;
        v.name = "groove";
        v.label = "Your notes, on the style's rhythm";

        for (auto& n : v.notes)
        {
            const auto s = static_cast<int> (n.start / step);
            const auto barStart = s - s % best->steps;
            int nearest = s, distance = 3;

            for (auto o : best->onsets)
                for (auto candidate : { barStart + o, barStart + o - best->steps, barStart + o + best->steps })
                    if (std::abs (candidate - s) < distance)
                        distance = std::abs (candidate - s), nearest = candidate;

            n.start = static_cast<int64_t> (nearest) * step;
        }

        tidy (v.notes, length);
        versions.push_back (v);
    }

    // Simpler: the notes on eighths or long ones, repeats of a note merged, a bar or two looped.
    {
        auto v = base;
        v.name = "simpler";
        v.label = "Simpler, as a short loop";
        // On the beats (or long) where that leaves a few, else on the eighths.
        auto keep = [&] (int64_t grid)
        {
            std::vector<PlacedNote> kept;

            for (auto& n : base.notes)
            {
                if (n.start % grid != 0 && n.length < grid)
                    continue;

                if (! kept.empty() && kept.back().key == n.key && n.start - (kept.back().start + kept.back().length) < step)
                    continue;

                kept.push_back (n);
            }

            return kept;
        };

        auto kept = keep (4 * step);

        if (kept.size() < 3)
            kept = keep (2 * step);

        if (kept.size() >= 2)
        {
            const auto loopBars = length >= 2 * bar ? 2 : 1;
            v.notes.clear();

            for (auto& n : kept)
                if (n.start < loopBars * bar)
                    v.notes.push_back (n);

            v.length = loopBars * bar;
            tidy (v.notes, v.length);

            if (v.notes.size() >= 2)
            {
                loopTo (v, static_cast<int> (length / bar));
                versions.push_back (v);
            }
        }
    }

    // Half and double speed.
    {
        auto half = base;
        half.name = "half";
        half.label = "Half speed";

        for (auto& n : half.notes)
            n.start *= 2, n.length *= 2;

        half.length = length * 2;

        if (half.length <= 8 * bar)
            versions.push_back (half);

        auto twice = base;
        twice.name = "double";
        twice.label = "Double speed";

        for (auto& n : twice.notes)
            n.start /= 2, n.length = std::max<int64_t> (step / 2, n.length / 2);

        twice.length = std::max<int64_t> (bar, ((length / 2 + bar - 1) / bar) * bar);
        tidy (twice.notes, twice.length);
        versions.push_back (twice);
    }

    // The other feel: the line read straight when it was heard as triplets or swung, else a
    // shuffle (each second 16th a little late).
    if (options.straight && ! options.straight->notes.empty())
    {
        versions.push_back ({ "straight", "Straight, on 16ths", options.straight->notes, std::max<int64_t> (options.straight->length, bar) });
    }
    else if (hummed.feel == FitOptions::Feel::straight)
    {
        auto v = base;
        v.name = "shuffle";
        v.label = "With a shuffle";
        bool moved = false;

        for (auto& n : v.notes)
            if (n.start % (2 * step) == step)
                n.start += step / 4, n.length = std::max<int64_t> (step / 2, n.length - step / 4), moved = true;

        if (moved)
        {
            tidy (v.notes, length);
            versions.push_back (v);
        }
    }

    // Answering a part in the song: shifted so its notes land in the part's gaps.
    if (! options.partner.empty() && options.partnerLength > 0)
    {
        const auto steps = static_cast<int> (options.partnerLength / step);
        const auto theirs = onsetsOf (options.partner, steps);
        int bestShift = 0, bestOverlap = 1 << 30;

        for (int shift = 1; shift < 16; ++shift)
        {
            int overlap = 0;

            for (auto& n : base.notes)
                overlap += theirs.contains (static_cast<int> ((n.start / step + shift) % steps)) ? 1 : 0;

            if (overlap < bestOverlap)
                bestOverlap = overlap, bestShift = shift;
        }

        auto v = base;
        v.name = "answer";
        v.label = "Answering " + options.partnerName;

        for (auto& n : v.notes)
            n.start = (n.start + bestShift * step) % length;

        tidy (v.notes, length);
        versions.push_back (v);
    }

    for (auto& v : versions)
    {
        if (options.bars > 0)
            loopTo (v, options.bars);

        v.closeness = std::round (100 * (0.6f * rhythmMatch (v.notes, base.notes) + 0.4f * contourMatch (v.notes, base.notes)));
        v.fit = styleFit (v, options.styleRhythms);
    }

    // The same notes twice: once.
    std::vector<MelodyVersion> distinct;

    for (auto& v : versions)
    {
        const bool seen = std::any_of (distinct.begin(), distinct.end(), [&] (auto& d)
        {
            return d.length == v.length && d.notes.size() == v.notes.size()
                && std::equal (d.notes.begin(), d.notes.end(), v.notes.begin(), [] (auto& a, auto& b) { return a.start == b.start && a.key == b.key; });
        });

        if (! seen)
            distinct.push_back (v);
    }

    // The kinds the user picks rise; a kind passed over eight times or more and never picked goes.
    auto liking = [&] (const MelodyVersion& v)
    {
        const auto it = options.picks.find (v.name);

        if (it == options.picks.end())
            return 0.0f;

        const auto share = (static_cast<float> (it->second.chosen) + 1) / (static_cast<float> (it->second.shown) + 3);
        return 120 * (share - 1.0f / 3);
    };

    distinct.erase (std::remove_if (distinct.begin() + 1, distinct.end(), [&] (auto& v)
    {
        const auto it = options.picks.find (v.name);
        return it != options.picks.end() && it->second.shown >= 8 && it->second.chosen == 0;
    }), distinct.end());

    std::stable_sort (distinct.begin() + 1, distinct.end(), [&] (auto& a, auto& b)
    {
        return a.closeness + a.fit.value_or (50) + liking (a) > b.closeness + b.fit.value_or (50) + liking (b);
    });

    if (static_cast<int> (distinct.size()) > std::max (1, options.count))
        distinct.resize (static_cast<size_t> (std::max (1, options.count)));

    return distinct;
}

std::map<std::string, VersionOptions::Record> readVersionPicks (const std::filesystem::path& file)
{
    std::map<std::string, VersionOptions::Record> picks;

    try
    {
        std::ifstream in (file);

        if (! in)
            return picks;

        // Held here: items() only refers to the object it's given.
        const auto kinds = model::Json::parse (in).value ("kinds", model::Json::object());

        for (auto& [name, r] : kinds.items())
            picks[name] = { r.value ("shown", 0), r.value ("chosen", 0) };
    }
    catch (const std::exception&)
    {
        // Started again.
    }

    return picks;
}

void addVersionPick (const std::filesystem::path& file, const std::string& chosen, const std::vector<std::string>& shown)
{
    auto picks = readVersionPicks (file);

    for (auto& name : shown)
        ++picks[name].shown;

    ++picks[chosen].chosen;

    model::Json kinds = model::Json::object();

    for (auto& [name, r] : picks)
        kinds[name] = { { "shown", r.shown }, { "chosen", r.chosen } };

    try
    {
        std::filesystem::create_directories (file.parent_path());
        std::ofstream (file, std::ios::trunc) << model::Json { { "kinds", kinds } }.dump (2);
    }
    catch (const std::exception&)
    {
        // Learned next time.
    }
}

} // namespace gk::music
