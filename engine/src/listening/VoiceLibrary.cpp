#include "VoiceLibrary.h"

#include "Styles.h"
#include "library/SampleLibrary.h"

#include <map>
#include <set>
#include <sstream>

namespace gk::listening
{

using Json = model::Json;

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

    float round2 (float v)      { return std::round (v * 100) / 100; }

    /** Semitones that move a tonic to C, the smaller way round. */
    int toC (int tonic)
    {
        auto s = ((-tonic) % 12 + 12) % 12;
        return s > 5 ? s - 12 : s;
    }

    std::string rhythmOf (const std::vector<GridNote>& notes, int bars)
    {
        std::string r (static_cast<size_t> (bars * 16), '.');

        for (auto& n : notes)
            if (n.step >= 0 && n.step < bars * 16)
                r[static_cast<size_t> (n.step)] = 'x';

        return r;
    }

    std::set<int> onsets (const std::vector<GridNote>& notes)
    {
        std::set<int> s;

        for (auto& n : notes)
            s.insert (n.step);

        return s;
    }

    bool isMusicRole (const std::string& role)  { return role != "bass" && role != "kick"; }
}

const VoiceLibrary::Role* VoiceLibrary::role (const std::string& name) const
{
    for (auto& r : roles)
        if (r.name == name)
            return &r;

    return nullptr;
}

const VoiceLibrary::Pair* VoiceLibrary::pair (const std::string& a, const std::string& b) const
{
    for (auto& p : pairs)
        if ((p.a == a && p.b == b) || (p.a == b && p.b == a))
            return &p;

    return nullptr;
}

Json VoiceLibrary::toJson() const
{
    auto r = Json::array(), p = Json::array(), o = Json::array();

    for (auto& role : roles)
    {
        auto figures = Json::array();

        for (auto& f : role.figures)
        {
            auto notes = Json::array();

            for (auto& n : f.notes)
                notes.push_back ({ n.step, n.steps, n.key, n.velocity });

            figures.push_back ({ { "bars", f.bars }, { "notes", notes }, { "share", round2 (f.share) }, { "tracks", f.tracks } });
        }

        auto changes = Json::object();

        for (auto& [kind, share] : role.changes)
            changes[kind] = round2 (share);

        r.push_back ({ { "role", role.name }, { "tracks", role.tracks }, { "range", { role.lowest, role.highest } },
                       { "notesPerBar", round2 (role.notesPerBar) }, { "chord", round2 (role.chord) }, { "runBars", role.runBars },
                       { "changes", changes }, { "figures", figures } });
    }

    for (auto& pair : pairs)
    {
        auto intervals = Json::array();

        for (auto& [semitones, share] : pair.intervals)
            intervals.push_back ({ semitones, round2 (share) });

        p.push_back ({ { "a", pair.a }, { "b", pair.b }, { "count", pair.count }, { "together", round2 (pair.together) },
                       { "fills", round2 (pair.fills) }, { "answers", round2 (pair.answers) }, { "answerAfter", pair.answerAfter },
                       { "intervals", intervals } });
    }

    for (auto& a : atOnce)
        o.push_back ({ { "section", a.section }, { "median", round2 (a.median) }, { "most", a.most } });

    return { { "style", style }, { "tracks", tracks }, { "roles", r }, { "pairs", p }, { "atOnce", o } };
}

std::string VoiceLibrary::describe() const
{
    std::ostringstream out;

    if (tracks == 0)
    {
        out << "No voices for " << style << " yet: its references were heard before voices were (hear them again in the References tab).\n";
        return out.str();
    }

    out << "Voices in " << style << ", from " << tracks << " reference" << (tracks == 1 ? "" : "s")
        << " (figures moved to C, so a tonic of C; play them in the song's key; 16ths as in the maps)\n";

    if (! atOnce.empty())
    {
        out << "At once:";

        for (auto& a : atOnce)
            out << " " << a.section << " " << std::lround (a.median) << " (most " << a.most << ")";

        out << "\n";
    }

    for (auto& r : roles)
    {
        out << r.name << " in " << r.tracks << " of " << tracks << ", " << library::noteName (r.lowest) << "-" << library::noteName (r.highest)
            << ", " << std::lround (r.notesPerBar) << (std::lround (r.notesPerBar) == 1 ? " note" : " notes") << " a bar";

        if (r.chord >= 1.5f)
            out << ", chords of " << std::lround (r.chord);

        out << ", a figure lasts " << r.runBars << " bars";

        if (! r.changes.empty())
        {
            out << " then";

            for (size_t i = 0; i < r.changes.size() && i < 3; ++i)
                out << (i == 0 ? " " : ", ") << r.changes[i].first << " (" << std::lround (r.changes[i].second * 100) << "%)";
        }

        out << "\n";

        for (size_t i = 0; i < r.figures.size() && i < 5; ++i)
        {
            auto& f = r.figures[i];
            out << "  " << std::lround (f.share * 100) << "% of its bars, " << f.tracks << " track" << (f.tracks == 1 ? "" : "s") << ": "
                << figureText (f.notes, f.bars) << "\n";
        }
    }

    if (! pairs.empty())
    {
        out << "Pairs (where both play):\n";

        for (auto& p : pairs)
        {
            if (p.count < 2 && tracks > 2)
                continue;

            out << "- " << p.b << " with " << p.a << " in " << p.count << ": ";

            if (p.answers >= 0.5f)
                out << "answers it " << p.answerAfter << " 16ths after its phrase (" << std::lround (p.answers * 100) << "%)";
            else
                out << std::lround (p.together * 100) << "% of notes together, " << std::lround (p.fills * 100) << "% in its gaps";

            if (! p.intervals.empty())
            {
                out << "; over it by";

                for (auto& [semitones, share] : p.intervals)
                    out << " " << semitones << " (" << std::lround (share * 100) << "%)";
            }

            out << "\n";
        }
    }

    return out.str();
}

VoiceLibrary buildVoiceLibrary (const StyleProfile& profile)
{
    VoiceLibrary lib;
    lib.style = profile.name;

    struct RoleData
    {
        std::set<size_t> tracks;
        std::vector<int> lowest, highest, runBars;
        std::vector<float> notesPerBar, chord;
        std::map<std::string, int> changes;
        int changed = 0;

        struct Rhythm
        {
            int bars = 1, weight = 0;
            std::set<size_t> tracks;
            std::vector<GridNote> example;
            int exampleWeight = 0;
        };

        std::map<std::string, Rhythm> rhythms;
        int weight = 0;
    };

    struct PairData
    {
        std::set<size_t> tracks;
        int count = 0, answered = 0;
        std::vector<float> together, fills;
        std::vector<int> after;
        std::map<int, float> intervals;
    };

    std::map<std::string, RoleData> roles;
    std::map<std::pair<std::string, std::string>, PairData> pairs;
    std::map<std::string, std::vector<float>> atOnce;
    std::map<std::string, int> mostAtOnce;

    for (size_t t = 0; t < profile.tracks.size(); ++t)
    {
        auto& track = profile.tracks[t];

        if (! track.voices || track.voices->voices.empty())
            continue;

        ++lib.tracks;
        const auto& tv = *track.voices;
        const auto shift = tv.key ? toC (tv.key->key.tonic) : 0;

        for (auto& v : tv.voices)
        {
            auto& r = roles[v.role];
            r.tracks.insert (t);
            r.lowest.push_back (v.lowest + shift);
            r.highest.push_back (v.highest + shift);
            r.notesPerBar.push_back (v.notesPerBar);
            r.chord.push_back (v.chord);

            for (size_t i = 0; i < v.runs.size(); ++i)
            {
                auto& run = v.runs[i];
                const auto bars = run.to - run.from;
                r.runBars.push_back (bars);

                if (i > 0 && ! run.change.empty())
                {
                    auto kind = run.change.rfind ("as bars", 0) == 0 ? std::string ("back to an earlier figure")
                              : run.change.rfind ("transposed", 0) == 0 ? std::string ("transposed") : run.change;
                    ++r.changes[kind], ++r.changed;
                }

                auto notes = run.notes;

                for (auto& n : notes)
                    n.key += shift;

                auto& rhythm = r.rhythms[std::to_string (run.bars) + ":" + rhythmOf (notes, run.bars)];
                rhythm.bars = run.bars;
                rhythm.weight += bars;
                rhythm.tracks.insert (t);
                r.weight += bars;

                if (bars > rhythm.exampleWeight)
                    rhythm.example = notes, rhythm.exampleWeight = bars;
            }
        }

        for (auto& p : tv.pairs)
        {
            if (p.bars < 4)
                continue;

            const auto& a = tv.voices[static_cast<size_t> (p.a)].role;
            const auto& b = tv.voices[static_cast<size_t> (p.b)].role;
            auto key = p.answers ? std::make_pair (a, b) : std::make_pair (std::min (a, b), std::max (a, b));

            // An answer pair is kept under its unordered key too, so both ways count together.
            if (p.answers && pairs.count ({ b, a }) > 0)
                key = { b, a };

            auto& d = pairs[key];
            d.tracks.insert (t);
            ++d.count;
            d.together.push_back (p.together);
            d.fills.push_back (p.fills);

            if (p.answers)
                ++d.answered, d.after.push_back (p.answerAfter);

            for (auto& [semitones, share] : p.intervals)
                d.intervals[semitones] += share;
        }

        if (track.arrangement)
        {
            for (auto& section : track.arrangement->sections)
            {
                std::vector<float> counts;

                for (int bar = section.start; bar < section.end; ++bar)
                    counts.push_back (static_cast<float> (tv.playingIn (bar)));

                if (counts.empty())
                    continue;

                atOnce[section.role].push_back (medianOf (counts, 0.0f));
                mostAtOnce[section.role] = std::max (mostAtOnce[section.role], static_cast<int> (*std::max_element (counts.begin(), counts.end())));
            }
        }
    }

    for (auto& [name, r] : roles)
    {
        VoiceLibrary::Role role;
        role.name = name;
        role.tracks = static_cast<int> (r.tracks.size());
        role.lowest = medianOf (r.lowest, 0);
        role.highest = medianOf (r.highest, 0);
        role.notesPerBar = medianOf (r.notesPerBar, 0.0f);
        role.chord = medianOf (r.chord, 1.0f);
        role.runBars = medianOf (r.runBars, 0);

        for (auto& [kind, count] : r.changes)
            role.changes.push_back ({ kind, static_cast<float> (count) / static_cast<float> (std::max (1, r.changed)) });

        std::sort (role.changes.begin(), role.changes.end(), [] (auto& x, auto& y) { return x.second > y.second; });

        for (auto& [key, rhythm] : r.rhythms)
            role.figures.push_back ({ rhythm.bars, rhythm.example, static_cast<float> (rhythm.weight) / static_cast<float> (std::max (1, r.weight)),
                                      static_cast<int> (rhythm.tracks.size()) });

        // The rhythms most tracks play first, then the most bars.
        std::sort (role.figures.begin(), role.figures.end(), [] (auto& x, auto& y) { return x.tracks != y.tracks ? x.tracks > y.tracks : x.share > y.share; });

        if (role.figures.size() > 12)
            role.figures.resize (12);

        lib.roles.push_back (std::move (role));
    }

    std::sort (lib.roles.begin(), lib.roles.end(), [] (auto& x, auto& y) { return x.tracks > y.tracks; });

    for (auto& [key, d] : pairs)
    {
        VoiceLibrary::Pair p;
        p.a = key.first;
        p.b = key.second;
        p.count = static_cast<int> (d.tracks.size());
        p.together = medianOf (d.together, 0.0f);
        p.fills = medianOf (d.fills, 0.0f);
        p.answers = static_cast<float> (d.answered) / static_cast<float> (d.count);
        p.answerAfter = medianOf (d.after, 0);

        float total = 0;

        for (auto& [semitones, share] : d.intervals)
            total += share;

        for (auto& [semitones, share] : d.intervals)
            p.intervals.push_back ({ semitones, total > 0 ? share / total : 0.0f });

        std::sort (p.intervals.begin(), p.intervals.end(), [] (auto& x, auto& y) { return x.second > y.second; });

        if (p.intervals.size() > 3)
            p.intervals.resize (3);

        lib.pairs.push_back (std::move (p));
    }

    std::sort (lib.pairs.begin(), lib.pairs.end(), [] (auto& x, auto& y) { return x.count > y.count; });

    for (const char* section : { "intro", "groove", "build", "drop", "break", "outro" })
        if (auto it = atOnce.find (section); it != atOnce.end())
            lib.atOnce.push_back ({ section, medianOf (it->second, 0.0f), mostAtOnce[section] });

    return lib;
}

//==============================================================================
SongVoices songVoices (const model::Project& project)
{
    SongVoices song;
    const auto barTicks = model::ticksPerBar (project.timeSignature);
    const auto stepTicks = std::max<int64_t> (1, barTicks / 16);
    song.bars = static_cast<int> ((model::getSongEnd (project) + barTicks - 1) / barTicks);

    std::map<std::string, const model::Pattern*> patterns;

    for (auto& p : project.patterns)
        patterns[p.id] = &p;

    std::set<model::Row> mutedRows;

    for (size_t r = 0; r < project.playlist.rows.size(); ++r)
        if (project.playlist.rows[r].mute)
            mutedRows.insert (static_cast<model::Row> (r));

    std::vector<music::HeardNote> everything;

    for (auto& channel : project.channels)
    {
        if (channel.mute)
            continue;

        if (channel.part)
        {
            using P = model::ChannelPart;
            const auto part = *channel.part;

            if (part != P::bass && part != P::synth && part != P::vocal)
                continue;
        }

        std::vector<GridNote> notes;

        for (auto& clip : project.playlist.clips)
        {
            const auto* pc = std::get_if<model::PatternClip> (&clip);

            if (pc == nullptr || pc->mute || mutedRows.count (pc->row) > 0)
                continue;

            auto it = patterns.find (pc->pattern);

            if (it == patterns.end())
                continue;

            const auto length = model::getPatternLength (*it->second, project.timeSignature);

            if (length <= 0)
                continue;

            for (auto& n : it->second->notes)
            {
                if (n.channel != channel.id)
                    continue;

                // Every time the clip plays the note, the pattern looping inside it.
                for (int64_t loop = (pc->offset - n.start) / length - 1;; ++loop)
                {
                    const auto at = pc->start + n.start + loop * length - pc->offset;

                    if (at >= pc->start + pc->length)
                        break;

                    if (at < pc->start)
                        continue;

                    notes.push_back ({ static_cast<int> (at / stepTicks), std::max (1, static_cast<int> ((n.length + stepTicks / 2) / stepTicks)), n.key, n.velocity });
                }
            }
        }

        if (notes.size() < 2)
            continue;

        std::set<int> keys;

        for (auto& n : notes)
            keys.insert (n.key);

        // A drum hit played on one key, with no part to say otherwise.
        if (! channel.part && keys.size() < 2)
            continue;

        std::sort (notes.begin(), notes.end(), [] (auto& x, auto& y) { return x.step != y.step ? x.step < y.step : x.key < y.key; });

        Voice v;
        v.part = channel.part == model::ChannelPart::bass ? "bass" : "music";
        std::vector<int> sorted, lengths;
        std::map<int, int> perOnset;
        std::set<int> barsPlayed;

        for (auto& n : notes)
        {
            sorted.push_back (n.key), lengths.push_back (n.steps), ++perOnset[n.step], barsPlayed.insert (n.step / 16);
            everything.push_back ({ static_cast<double> (n.step), static_cast<double> (n.step + n.steps), static_cast<float> (n.key), 1.0f });
        }

        std::sort (sorted.begin(), sorted.end());
        v.lowest = sorted.front();
        v.highest = sorted.back();
        v.chord = static_cast<float> (notes.size()) / static_cast<float> (perOnset.size());
        v.notesPerBar = static_cast<float> (notes.size()) / static_cast<float> (barsPlayed.size());
        v.role = roleOf (v.part, sorted[sorted.size() / 2], v.chord, v.notesPerBar, static_cast<float> (medianOf (lengths, 1)));
        v.runs = foldRuns (notes, song.bars);
        song.voices.voices.push_back (std::move (v));
        song.channels.push_back (channel.id);
    }

    if (project.key)
        if (auto k = music::parseKey (*project.key))
            song.voices.key = music::KeyEstimate { *k, 1.0f };

    if (! song.voices.key)
        song.voices.key = music::keyOfNotes (everything);

    song.voices.pairs = pairVoices (song.voices.voices, song.bars);
    return song;
}

std::vector<std::string> checkVoices (const SongVoices& song, const VoiceLibrary& lib, std::optional<music::Key> songKey)
{
    struct Finding { float weight; std::string text; };
    std::vector<Finding> findings;
    const auto& voices = song.voices.voices;
    auto label = [&] (size_t i) { return voices[i].role + " (" + song.channels[i] + ")"; };

    if (lib.tracks == 0)
        return { lib.describe() };

    // Too many at once: every 8 bars against the most the references play in any section.
    int most = 0;
    float typical = 0;

    for (auto& a : lib.atOnce)
        most = std::max (most, a.most), typical = std::max (typical, a.median);

    for (int from = 0; from < song.bars && most > 0; from += 8)
    {
        int peak = 0;

        for (int bar = from; bar < std::min (song.bars, from + 8); ++bar)
            peak = std::max (peak, song.voices.playingIn (bar));

        if (peak > most)
        {
            findings.push_back ({ 3.0f + static_cast<float> (peak - most), "bars " + std::to_string (from + 1) + "-" + std::to_string (std::min (song.bars, from + 8)) + " have "
                                  + std::to_string (peak) + " melodic parts at once; " + lib.style + " references play at most " + std::to_string (most)
                                  + " (usually " + std::to_string (std::lround (typical)) + ")" });
        }
    }

    // Doubling where the style interlocks.
    for (auto& p : song.voices.pairs)
    {
        if (p.bars < 4 || p.together < 0.6f)
            continue;

        const auto& a = voices[static_cast<size_t> (p.a)].role;
        const auto& b = voices[static_cast<size_t> (p.b)].role;

        if (auto* lp = lib.pair (a, b); lp != nullptr && lp->together < 0.3f)
            findings.push_back ({ 2.5f, label (static_cast<size_t> (p.b)) + " and " + label (static_cast<size_t> (p.a)) + " play " + std::to_string (std::lround (p.together * 100))
                                  + "% of their notes together; in " + lib.style + " they " + (lp->answers >= 0.5f ? "answer each other" : "interlock")
                                  + " (" + std::to_string (std::lround (lp->together * 100)) + "% together)" });
    }

    // A part the style answers, without an answer.
    for (size_t i = 0; i < voices.size(); ++i)
    {
        for (auto& lp : lib.pairs)
        {
            if (lp.answers < 0.5f || lp.a != voices[i].role || lp.count * 3 < lib.tracks)
                continue;

            const auto answered = std::any_of (song.voices.pairs.begin(), song.voices.pairs.end(), [&] (auto& p) { return p.answers && p.a == static_cast<int> (i); });

            if (! answered)
                findings.push_back ({ 2.0f, label (i) + " has no answer: in " + std::to_string (lp.count) + " of " + std::to_string (lib.tracks) + " references a " + lp.b
                                      + " answers the " + lp.a + " " + std::to_string (lp.answerAfter) + " 16ths after its phrase (voices write with partner)" });

            break;
        }
    }

    // A figure that never changes.
    for (size_t i = 0; i < voices.size(); ++i)
    {
        const auto* role = lib.role (voices[i].role);
        const auto usual = role != nullptr && role->runBars > 0 ? role->runBars : 16;

        for (auto& run : voices[i].runs)
            if (run.to - run.from >= std::max (32, 2 * usual))
            {
                findings.push_back ({ 1.5f, label (i) + " plays one figure for " + std::to_string (run.to - run.from) + " bars (" + std::to_string (run.from + 1) + "-"
                                      + std::to_string (run.to) + "); in " + lib.style + " a " + voices[i].role + " figure lasts about " + std::to_string (usual) + " bars, then "
                                      + (role != nullptr && ! role->changes.empty() ? role->changes.front().first : std::string ("changes")) });
                break;
            }
    }

    // Notes out of the key.
    if (songKey)
    {
        for (size_t i = 0; i < voices.size(); ++i)
        {
            int out = 0, all = 0;

            for (auto& run : voices[i].runs)
                for (auto& n : run.notes)
                    ++all, out += songKey->contains (n.key % 12) ? 0 : 1;

            if (all > 0 && out * 10 > all)
                findings.push_back ({ 2.0f, std::to_string (std::lround (100.0 * out / all)) + "% of the notes of " + label (i) + " are outside " + music::keyName (*songKey) });
        }
    }

    // Busier than the style's part, or a part most references have and the song doesn't.
    std::set<std::string> have;

    for (size_t i = 0; i < voices.size(); ++i)
    {
        have.insert (voices[i].role);

        if (const auto* role = lib.role (voices[i].role); role != nullptr && role->notesPerBar > 0 && voices[i].notesPerBar > 2 * role->notesPerBar + 1)
            findings.push_back ({ 1.0f, label (i) + " plays " + std::to_string (std::lround (voices[i].notesPerBar)) + " notes a bar; " + lib.style + " " + role->name
                                  + "s play about " + std::to_string (std::lround (role->notesPerBar)) });
    }

    for (auto& role : lib.roles)
        if (role.tracks * 2 > lib.tracks && have.count (role.name) == 0 && role.name != "kick")
            findings.push_back ({ 0.5f, "no " + role.name + ": " + std::to_string (role.tracks) + " of " + std::to_string (lib.tracks) + " " + lib.style + " references have one" });

    std::stable_sort (findings.begin(), findings.end(), [] (auto& x, auto& y) { return x.weight > y.weight; });
    std::vector<std::string> out;

    for (auto& f : findings)
        out.push_back (f.text);

    return out;
}

//==============================================================================
std::vector<WrittenVoice> writeVoices (const VoiceLibrary& lib, const WriteOptions& options)
{
    std::vector<WrittenVoice> written;
    const auto* role = lib.role (options.role);

    // A role the style hasn't: its commonest melodic role stands in.
    if (role == nullptr || role->figures.empty())
    {
        for (auto& r : lib.roles)
            if (isMusicRole (r.name) == isMusicRole (options.role) && ! r.figures.empty())
            {
                role = &r;
                break;
            }
    }

    if (role == nullptr || role->figures.empty())
        return written;

    // Notes and register from another style, when mixing.
    const VoiceLibrary::Role* noteRole = nullptr;

    if (options.notesFrom != nullptr)
    {
        noteRole = options.notesFrom->role (options.role);

        if (noteRole == nullptr || noteRole->figures.empty())
            noteRole = options.notesFrom->role (role->name);
    }

    // How it goes with the partner: asked for, or the style's way with the partner's role.
    std::string relation = options.relation;

    if (relation.empty() && ! options.partner.empty())
    {
        if (const auto* p = lib.pair (options.partnerRole, role->name))
            relation = p->answers >= 0.5f ? "answer" : p->together >= 0.5f ? "together" : "interlock";
        else
            relation = "interlock";
    }

    const auto* pairStats = options.partner.empty() ? nullptr : lib.pair (options.partnerRole, role->name);
    const auto shift = options.key ? -toC (options.key->tonic) : 0;

    for (int c = 0; c < std::max (1, options.count); ++c)
    {
        const auto index = static_cast<size_t> (options.variation + c) % role->figures.size();
        const auto& figure = role->figures[index];
        WrittenVoice w;
        w.bars = figure.bars;
        w.notes = figure.notes;
        w.from = lib.style + " " + role->name + " figure " + std::to_string (index + 1) + " (" + std::to_string (std::lround (figure.share * 100))
                 + "% of its bars, " + std::to_string (figure.tracks) + " track" + (figure.tracks == 1 ? "" : "s") + ")";

        // Notes from the other style: its figure's keys in order, over this rhythm, in its register.
        if (noteRole != nullptr)
        {
            const auto& other = noteRole->figures[index % noteRole->figures.size()].notes;
            std::vector<int> keys;

            for (auto& n : other)
                keys.push_back (n.key);

            for (size_t i = 0; i < w.notes.size() && ! keys.empty(); ++i)
                w.notes[i].key = keys[i % keys.size()];

            w.from += ", notes from " + options.notesFrom->style + " " + noteRole->name;
        }

        // Varied, so it's never a copy: every other one with a note moved by a step of the
        // scale, every third with its rhythm moved an 8th.
        if (c % 3 == 1 && w.notes.size() >= 2)
        {
            auto& n = w.notes[w.notes.size() / 2];
            n.key += (c / 3) % 2 == 0 ? 2 : -2;
            w.from += ", a note moved";
        }
        else if (c % 3 == 2)
        {
            for (auto& n : w.notes)
                n.step = (n.step + 2) % (w.bars * 16);

            std::sort (w.notes.begin(), w.notes.end(), [] (auto& x, auto& y) { return x.step != y.step ? x.step < y.step : x.key < y.key; });
            w.from += ", moved an 8th later";
        }

        // Into the song's key and its scale.
        for (auto& n : w.notes)
        {
            n.key += shift;

            if (options.key)
                n.key = options.key->snap (n.key);

            n.key = std::clamp (n.key, 0, 127);
        }

        // Fitted to the partner, over the partner's length.
        if (! options.partner.empty())
        {
            const auto cycle = std::max (1, options.partnerBars) * 16;
            std::vector<bool> sounding (static_cast<size_t> (cycle), false);
            std::set<int> partnerOnsets;

            for (auto& n : options.partner)
            {
                partnerOnsets.insert (((n.step % cycle) + cycle) % cycle);

                for (int s = n.step; s < n.step + n.steps; ++s)
                    sounding[static_cast<size_t> (((s % cycle) + cycle) % cycle)] = true;
            }

            // The figure over the partner's cycle.
            std::vector<GridNote> laid;

            for (int at = 0; at < cycle; at += w.bars * 16)
                for (auto n : w.notes)
                    if ((n.step += at) < cycle)
                        laid.push_back (n);

            w.bars = cycle / 16;
            w.relation = relation;

            if (relation == "together")
            {
                // The partner's rhythm, the figure's notes.
                std::vector<GridNote> doubled;
                size_t i = 0;

                for (auto step : partnerOnsets)
                    if (! laid.empty())
                        doubled.push_back ({ step, 1, laid[i++ % laid.size()].key, laid[(i - 1) % laid.size()].velocity });

                laid = std::move (doubled);
            }
            else if (relation == "answer")
            {
                // The longest silence of the partner, the answer starting after its phrase.
                int bestStart = 0, bestLength = 0;

                for (int s = 0; s < cycle; ++s)
                {
                    if (sounding[static_cast<size_t> (s)] || sounding[static_cast<size_t> ((s - 1 + cycle) % cycle)] == false)
                        continue;

                    int length = 0;

                    while (length < cycle && ! sounding[static_cast<size_t> ((s + length) % cycle)])
                        ++length;

                    if (length > bestLength)
                        bestLength = length, bestStart = s;
                }

                const auto delay = pairStats != nullptr ? pairStats->answerAfter : 1;
                std::vector<GridNote> answer;

                if (! laid.empty())
                {
                    const auto first = laid.front().step;

                    for (auto n : laid)
                    {
                        const auto offset = n.step - first + delay;

                        if (offset + 1 > bestLength)
                            break;

                        n.step = (bestStart + offset) % cycle;
                        n.steps = std::min (n.steps, bestLength - offset);
                        answer.push_back (n);
                    }
                }

                if (! answer.empty())
                    laid = std::move (answer);
            }
            else
            {
                // Interlocking: a note where the partner starts one moves to the nearest free step.
                std::set<int> taken = partnerOnsets;

                for (auto& n : laid)
                {
                    if (taken.count (n.step) == 0)
                    {
                        taken.insert (n.step);
                        continue;
                    }

                    for (int d : { 1, -1, 2, -2 })
                        if (const auto s = (n.step + d + cycle) % cycle; taken.count (s) == 0)
                        {
                            n.step = s;
                            taken.insert (s);
                            break;
                        }
                }
            }

            std::sort (laid.begin(), laid.end(), [] (auto& x, auto& y) { return x.step != y.step ? x.step < y.step : x.key < y.key; });
            w.notes = std::move (laid);
        }

        // Repeated to the bars asked for.
        if (options.bars > w.bars && w.bars > 0)
        {
            std::vector<GridNote> repeated;

            for (int at = 0; at < options.bars * 16; at += w.bars * 16)
                for (auto n : w.notes)
                    if ((n.step += at) < options.bars * 16)
                        repeated.push_back (n);

            w.notes = std::move (repeated);
            w.bars = options.bars;
        }

        // Two the same are one.
        if (std::any_of (written.begin(), written.end(), [&] (auto& o) { return o.notes == w.notes; }))
            continue;

        written.push_back (std::move (w));
    }

    return written;
}

} // namespace gk::listening
