#include "Project.h"

#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace gk::model
{

int64_t ticksPerBar (const TimeSignature& ts)
{
    return ticksPerQuarter * 4 * ts.numerator / ts.denominator;
}

TickLength getPatternLength (const Pattern& pattern, const TimeSignature& ts)
{
    if (pattern.length)
        return *pattern.length;

    Ticks end = 0;

    for (auto& note : pattern.notes)
        end = std::max (end, note.start + note.length);

    auto bar = ticksPerBar (ts);
    return std::max<int64_t> (1, (end + bar - 1) / bar) * bar;
}

Ticks getSongEnd (const Project& project)
{
    Ticks end = 0;

    for (auto& clip : project.playlist.clips)
        end = std::max (end, getClipStart (clip) + getClipLength (clip));

    return end;
}

//==============================================================================
namespace
{
    /** Schema 1 audio clips named their file, reverse, stretch and insert themselves; now an audio
        channel does, shared by the clips that play the same file the same way. */
    bool moveAudioClipsToChannels (Json& doc)
    {
        if (! doc.contains ("playlist") || ! doc["playlist"].contains ("clips"))
            return false;

        if (! doc.contains ("channels"))
            doc["channels"] = Json::array();

        auto& channels = doc["channels"];
        std::set<std::string> ids;

        for (auto& c : channels)
            ids.insert (c.value ("id", ""));

        std::map<std::string, std::string> channelFor;   // file, reverse, tempo, insert -> channel id
        bool changed = false;

        for (auto& clip : doc["playlist"]["clips"])
        {
            if (clip.value ("type", "") != "audio" || ! clip.contains ("sample"))
                continue;

            const auto sample = clip["sample"].get<std::string>();
            const auto reverse = clip.value ("reverse", false);
            const auto insert = clip.value ("insert", std::string (masterInsertId));
            const auto tempo = clip.contains ("sourceTempo") ? clip["sourceTempo"].dump() : "";
            const auto key = sample + "|" + (reverse ? "r" : "") + "|" + tempo + "|" + insert;

            if (! channelFor.contains (key))
            {
                const auto name = std::filesystem::path (sample).stem().string();
                std::string base;

                for (auto ch : name)
                    base += std::isalnum (static_cast<unsigned char> (ch)) || ch == '-' || ch == '_' || ch == '.' ? ch : '-';

                base = base.substr (0, 56);

                if (base.empty())
                    base = "audio";

                auto id = base;

                for (int n = 2; ids.contains (id); ++n)
                    id = base + "-" + std::to_string (n);

                ids.insert (id);

                Json instrument { { "type", "audio" }, { "sample", sample } };

                if (reverse)                        instrument["reverse"] = true;
                if (clip.contains ("sourceTempo"))  instrument["sourceTempo"] = clip["sourceTempo"];

                Json channel { { "id", id }, { "name", name }, { "instrument", instrument } };

                if (insert != masterInsertId)
                    channel["insert"] = insert;

                channels.push_back (channel);
                channelFor[key] = id;
            }

            for (auto field : { "sample", "reverse", "sourceTempo", "insert" })
                clip.erase (field);

            clip["channel"] = channelFor[key];
            changed = true;
        }

        return changed;
    }
}

namespace
{
    /** The first EQ had four fixed bands; the seven-band one keeps them where they were:
        low shelf as band 1, the two peaks as bands 3 and 5, the high shelf as band 7. */
    const std::map<std::string, std::string> oldEqParams {
        { "lowFreq", "band1Freq" }, { "lowGainDb", "band1GainDb" }, { "lowQ", "band1Q" },
        { "mid1Freq", "band3Freq" }, { "mid1GainDb", "band3GainDb" }, { "mid1Q", "band3Q" },
        { "mid2Freq", "band5Freq" }, { "mid2GainDb", "band5GainDb" }, { "mid2Q", "band5Q" },
        { "highFreq", "band7Freq" }, { "highGainDb", "band7GainDb" }, { "highQ", "band7Q" },
    };

    bool upgradeEffects (Json& doc)
    {
        bool changed = false;
        std::set<std::pair<std::string, std::string>> eqs;   // insert, effect

        if (doc.contains ("mixer") && doc["mixer"].contains ("inserts"))
        {
            for (auto& insert : doc["mixer"]["inserts"])
            {
                if (! insert.contains ("effects"))
                    continue;

                for (auto& effect : insert["effects"])
                {
                    if (effect.value ("type", "") != "builtin" || effect.value ("kind", "") != "eq")
                        continue;

                    eqs.insert ({ insert.value ("id", ""), effect.value ("id", "") });

                    if (! effect.contains ("params"))
                        continue;

                    auto params = Json::object();

                    for (auto& [key, value] : effect["params"].items())
                    {
                        auto it = oldEqParams.find (key);
                        params[it != oldEqParams.end() ? it->second : key] = value;
                        changed = changed || it != oldEqParams.end();
                    }

                    effect["params"] = params;
                }
            }
        }

        if (doc.contains ("playlist") && doc["playlist"].contains ("clips"))
        {
            for (auto& clip : doc["playlist"]["clips"])
            {
                if (clip.value ("type", "") != "automation" || ! clip.contains ("target"))
                    continue;

                auto& t = clip["target"];

                if (t.value ("scope", "") == "effect" && eqs.contains ({ t.value ("insert", ""), t.value ("effect", "") }))
                {
                    if (auto it = oldEqParams.find (t.value ("param", "")); it != oldEqParams.end())
                    {
                        t["param"] = it->second;
                        changed = true;
                    }
                }
            }
        }

        return changed;
    }
}

namespace
{
    double param (const Json& params, const char* key, double fallback)
    {
        return params.contains (key) && params[key].is_number() ? params[key].get<double>() : fallback;
    }

    double reverbDampingHz (double damping)    { return std::round (20000.0 * std::pow (0.1, std::clamp (damping, 0.0, 1.0))); }
    double delayFeedback (double db)           { return std::round (std::min (95.0, 100.0 * std::pow (10.0, db / 20.0))); }

    /** Schema 1's reverb, delay and chorus were Tracktion's, each with its own wet/dry; GrooveKit's
        output only the effect and the slot's mix blends it in. Settings carry over as closely
        as the new controls allow. */
    void upgradeTimeEffects (Json& doc)
    {
        // insert/effect -> kind, for the automation clips below.
        std::map<std::string, std::string> kinds;

        if (doc.contains ("mixer") && doc["mixer"].contains ("inserts"))
        {
            for (auto& insert : doc["mixer"]["inserts"])
            {
                if (! insert.contains ("effects"))
                    continue;

                for (auto& effect : insert["effects"])
                {
                    const auto kind = effect.value ("kind", "");

                    if (effect.value ("type", "") != "builtin" || (kind != "reverb" && kind != "delay" && kind != "chorus"))
                        continue;

                    kinds[insert.value ("id", "") + "/" + effect.value ("id", "")] = kind;
                    const auto old = effect.value ("params", Json::object());
                    auto params = Json::object();
                    double mix = 1;

                    if (kind == "reverb")
                    {
                        const auto room = param (old, "roomSize", 0.3);
                        params = { { "size", std::round (room * 100) }, { "decay", std::round ((0.6 + room * 4.4) * 10) / 10 },
                                   { "damping", reverbDampingHz (param (old, "damping", 0.5)) }, { "width", std::round (param (old, "width", 1) * 100) } };
                        const auto wet = param (old, "wet", 1), dry = param (old, "dry", 1);
                        mix = wet + dry > 0 ? wet / (wet + dry) : 0;
                    }
                    else if (kind == "delay")
                    {
                        params = { { "sync", 0 }, { "timeMs", param (old, "timeMs", 150) }, { "feedback", delayFeedback (param (old, "feedbackDb", -6)) } };
                        mix = param (old, "mix", 0.3);
                    }
                    else
                    {
                        params = { { "depthMs", std::clamp (param (old, "depthMs", 3), 0.5, 10.0) }, { "rate", std::clamp (param (old, "speedHz", 1), 0.05, 5.0) },
                                   { "width", std::round (param (old, "width", 0.5) * 100) } };
                        mix = param (old, "mix", 0.5);
                    }

                    effect["params"] = params;

                    if (! effect.contains ("mix"))
                        effect["mix"] = std::round (mix * 100) / 100;
                }
            }
        }

        if (! doc.contains ("playlist") || ! doc["playlist"].contains ("clips"))
            return;

        auto& clips = doc["playlist"]["clips"];

        for (size_t i = clips.size(); i-- > 0;)
        {
            auto& clip = clips[i];

            if (clip.value ("type", "") != "automation" || ! clip.contains ("target") || clip["target"].value ("scope", "") != "effect")
                continue;

            auto& t = clip["target"];
            auto it = kinds.find (t.value ("insert", "") + "/" + t.value ("effect", ""));

            if (it == kinds.end())
                continue;

            const auto from = t.value ("param", "");
            std::string to;
            std::function<double (double)> convert = [] (double v) { return v; };

            if (it->second == "reverb" && from == "roomSize")       { to = "size"; convert = [] (double v) { return v * 100; }; }
            else if (it->second == "reverb" && from == "damping")   { to = "damping"; convert = reverbDampingHz; }
            else if (it->second == "reverb" && from == "width")     { to = "width"; convert = [] (double v) { return v * 100; }; }
            else if (it->second == "delay" && from == "feedbackDb") { to = "feedback"; convert = delayFeedback; }
            else if (from == "mix")                                 { to = "mix"; }

            // A parameter with nothing like it now (the reverb's separate wet and dry levels).
            if (to.empty())
            {
                clips.erase (clips.begin() + static_cast<std::ptrdiff_t> (i));
                continue;
            }

            t["param"] = to;

            if (clip.contains ("points"))
                for (auto& point : clip["points"])
                    if (point.contains ("value"))
                        point["value"] = convert (point["value"].get<double>());
        }
    }
}

bool migrateProject (Json& doc)
{
    if (! doc.is_object())
        return false;

    if (doc.value ("schemaVersion", 0) < 2)
        upgradeTimeEffects (doc);

    bool changed = moveAudioClipsToChannels (doc);
    changed = upgradeEffects (doc) || changed;

    if (doc.value ("schemaVersion", 0) == 1)
    {
        doc["schemaVersion"] = 2;
        changed = true;
    }

    return changed;
}

Project parseProject (const Json& json)
{
    Reader reader;
    Project project;

    // Current documents are read as they are; older ones through a converted copy.
    if (json.is_object() && json.value ("schemaVersion", 0) < 2)
    {
        auto copy = json;
        migrateProject (copy);
        readValue (reader, copy, project);
    }
    else
    {
        readValue (reader, json, project);
    }

    return project;
}

Project loadProject (const std::filesystem::path& songFile)
{
    std::ifstream in (songFile);

    if (! in)
        throw std::runtime_error ("Can't open " + songFile.string());

    Json json;

    try
    {
        json = Json::parse (in);
    }
    catch (const Json::parse_error& e)
    {
        throw std::runtime_error (songFile.string() + ": " + e.what());
    }

    migrateProject (json);
    return parseProject (json);
}

Json projectToJson (const Project& project)
{
    return toJson (project);
}

void saveProject (const Project& project, const std::filesystem::path& songFile)
{
    std::ofstream out (songFile);

    if (! out)
        throw std::runtime_error ("Can't write " + songFile.string());

    out << projectToJson (project).dump (2) << "\n";
}

//==============================================================================
namespace
{
    template <typename T>
    const T* findById (const std::vector<T>& items, const Id& id)
    {
        for (auto& item : items)
            if (item.id == id)
                return &item;

        return nullptr;
    }

    std::string at (const std::string& base, size_t index)
    {
        return base + "/" + std::to_string (index);
    }

    struct Validator
    {
        const Project& project;
        std::vector<ValidationIssue> issues;

        void issue (std::string path, std::string message)
        {
            issues.push_back ({ std::move (path), std::move (message) });
        }

        template <typename T, typename GetId>
        void checkUnique (const std::vector<T>& items, const std::string& path, GetId&& getId)
        {
            std::set<Id> seen;

            for (size_t i = 0; i < items.size(); ++i)
                if (! seen.insert (getId (items[i])).second)
                    issue (at (path, i) + "/id", "duplicate id \"" + getId (items[i]) + "\"");
        }

        template <typename T>
        void checkUnique (const std::vector<T>& items, const std::string& path)
        {
            checkUnique (items, path, [] (const T& t) -> const Id& { return t.id; });
        }

        void checkInsertRef (const Id& id, const std::string& path)
        {
            if (findInsert (project, id) == nullptr)
                issue (path, "no mixer insert \"" + id + "\"");
        }

        void run()
        {
            checkUnique (project.channels, "/channels");
            checkUnique (project.patterns, "/patterns");
            checkUnique (project.playlist.tracks, "/playlist/tracks");
            checkUnique (project.playlist.clips, "/playlist/clips", [] (const Clip& c) -> const Id& { return getClipId (c); });
            checkUnique (project.mixer.inserts, "/mixer/inserts");

            checkChannels();
            checkPatterns();
            checkClips();
            checkMixer();
        }

        void checkChannels()
        {
            for (size_t i = 0; i < project.channels.size(); ++i)
            {
                auto& channel = project.channels[i];
                auto path = at ("/channels", i);

                checkInsertRef (channel.insert, path + "/insert");

                if (auto sampler = std::get_if<SamplerInstrument> (&channel.instrument))
                {
                    checkUnique (sampler->layers, path + "/instrument/layers");

                    for (size_t l = 0; l < sampler->layers.size(); ++l)
                    {
                        auto& layer = sampler->layers[l];
                        auto layerPath = at (path + "/instrument/layers", l);

                        if (layer.keyRange.low > layer.keyRange.high)
                            issue (layerPath + "/keyRange", "low is above high");

                        if (layer.velocityRange.low > layer.velocityRange.high)
                            issue (layerPath + "/velocityRange", "low is above high");
                    }
                }
            }
        }

        void checkPatterns()
        {
            for (size_t i = 0; i < project.patterns.size(); ++i)
            {
                auto& pattern = project.patterns[i];

                for (size_t n = 0; n < pattern.notes.size(); ++n)
                {
                    auto channel = findChannel (project, pattern.notes[n].channel);

                    if (channel == nullptr)
                        issue (at (at ("/patterns", i) + "/notes", n) + "/channel",
                               "no channel \"" + pattern.notes[n].channel + "\"");
                    else if (std::holds_alternative<AudioInstrument> (channel->instrument))
                        issue (at (at ("/patterns", i) + "/notes", n) + "/channel",
                               "\"" + channel->id + "\" is an audio channel: it plays audio clips, not notes");
                }
            }
        }

        void checkClips()
        {
            for (size_t i = 0; i < project.playlist.tracks.size(); ++i)
                if (auto& insert = project.playlist.tracks[i].insert)
                    checkInsertRef (*insert, at ("/playlist/tracks", i) + "/insert");

            for (size_t i = 0; i < project.playlist.clips.size(); ++i)
            {
                auto& clip = project.playlist.clips[i];
                auto path = at ("/playlist/clips", i);

                if (findTrack (project, getClipTrack (clip)) == nullptr)
                    issue (path + "/track", "no playlist track \"" + getClipTrack (clip) + "\"");

                if (auto pc = std::get_if<PatternClip> (&clip))
                {
                    if (findPattern (project, pc->pattern) == nullptr)
                        issue (path + "/pattern", "no pattern \"" + pc->pattern + "\"");
                }
                else if (auto ac = std::get_if<AudioClip> (&clip))
                {
                    auto channel = findChannel (project, ac->channel);

                    if (channel == nullptr)
                        issue (path + "/channel", "no channel \"" + ac->channel + "\"");
                    else if (! std::holds_alternative<AudioInstrument> (channel->instrument))
                        issue (path + "/channel", "\"" + ac->channel + "\" isn't an audio channel (its instrument needs type \"audio\")");
                }
                else if (auto automation = std::get_if<AutomationClip> (&clip))
                {
                    checkAutomationTarget (automation->target, path + "/target");

                    for (size_t p = 1; p < automation->points.size(); ++p)
                        if (automation->points[p].time < automation->points[p - 1].time)
                            issue (at (path + "/points", p) + "/time", "points must be in time order");
                }
            }
        }

        void checkAutomationTarget (const AutomationTarget& target, const std::string& path)
        {
            auto requireField = [&] (const std::optional<Id>& field, const char* name) -> bool
            {
                if (! field)
                    issue (path, std::string ("scope \"") + toString (target.scope) + "\" needs \"" + name + "\"");

                return field.has_value();
            };

            auto requireParam = [&] (std::initializer_list<const char*> allowed)
            {
                for (auto p : allowed)
                    if (target.param == p)
                        return;

                issue (path + "/param", "\"" + target.param + "\" can't be automated in scope \"" + toString (target.scope) + "\"");
            };

            switch (target.scope)
            {
                case AutomationTargetScope::channel:
                    if (requireField (target.channel, "channel") && findChannel (project, *target.channel) == nullptr)
                        issue (path + "/channel", "no channel \"" + *target.channel + "\"");

                    requireParam ({ "volume", "pan" });
                    break;

                case AutomationTargetScope::insert:
                    if (requireField (target.insert, "insert"))
                        checkInsertRef (*target.insert, path + "/insert");

                    requireParam ({ "volume", "pan" });
                    break;

                case AutomationTargetScope::effect:
                    if (requireField (target.insert, "insert") && requireField (target.effect, "effect"))
                        if (auto insert = findInsert (project, *target.insert))
                            if (std::none_of (insert->effects.begin(), insert->effects.end(),
                                              [&] (const Effect& e) { return getEffectId (e) == *target.effect; }))
                                issue (path + "/effect", "insert \"" + insert->id + "\" has no effect \"" + *target.effect + "\"");

                    if (target.insert)
                        checkInsertRef (*target.insert, path + "/insert");

                    break;

                case AutomationTargetScope::send:
                    if (requireField (target.insert, "insert") && requireField (target.send, "send"))
                        if (auto insert = findInsert (project, *target.insert))
                            if (findById (insert->sends, *target.send) == nullptr)
                                issue (path + "/send", "insert \"" + insert->id + "\" has no send \"" + *target.send + "\"");

                    if (target.insert)
                        checkInsertRef (*target.insert, path + "/insert");

                    requireParam ({ "level" });
                    break;
            }
        }

        void checkMixer()
        {
            if (findInsert (project, masterInsertId) == nullptr)
                issue ("/mixer/inserts", "a \"master\" insert is required");

            for (size_t i = 0; i < project.mixer.inserts.size(); ++i)
            {
                auto& insert = project.mixer.inserts[i];
                auto path = at ("/mixer/inserts", i);
                bool isMaster = insert.id == masterInsertId;

                checkUnique (insert.effects, path + "/effects", [] (const Effect& e) -> const Id& { return getEffectId (e); });
                checkUnique (insert.sends, path + "/sends");

                if (! isMaster)
                {
                    if (insert.output == insert.id)
                        issue (path + "/output", "an insert can't feed itself");
                    else
                        checkInsertRef (insert.output, path + "/output");
                }
                else if (! insert.sends.empty())
                {
                    issue (path + "/sends", "the master insert can't have sends");
                }

                for (size_t s = 0; s < insert.sends.size(); ++s)
                {
                    auto& send = insert.sends[s];
                    auto sendPath = at (path + "/sends", s) + "/target";

                    if (send.target == insert.id)
                        issue (sendPath, "an insert can't send to itself");
                    else
                        checkInsertRef (send.target, sendPath);
                }
            }

            checkRoutingCycles();
        }

        /** Every insert's output and send targets form a graph that must be acyclic. */
        void checkRoutingCycles()
        {
            enum class State { unvisited, visiting, done };
            std::map<Id, State> state;
            std::vector<Id> stack;

            std::function<bool (const Insert&)> visit = [&] (const Insert& insert) -> bool
            {
                auto& s = state[insert.id];

                if (s == State::done)
                    return false;

                if (s == State::visiting)
                {
                    std::string cycle;

                    for (auto it = std::find (stack.begin(), stack.end(), insert.id); it != stack.end(); ++it)
                        cycle += *it + " -> ";

                    issue ("/mixer/inserts", "routing cycle: " + cycle + insert.id);
                    return true;
                }

                s = State::visiting;
                stack.push_back (insert.id);

                std::vector<Id> targets;

                if (insert.id != masterInsertId)
                    targets.push_back (insert.output);

                for (auto& send : insert.sends)
                    targets.push_back (send.target);

                for (auto& t : targets)
                    if (auto next = findInsert (project, t); next != nullptr && next != &insert)
                        if (visit (*next))
                            return true;

                stack.pop_back();
                state[insert.id] = State::done;
                return false;
            };

            for (auto& insert : project.mixer.inserts)
                if (visit (insert))
                    return;
        }
    };
}

std::vector<ValidationIssue> validate (const Project& project)
{
    Validator v { project, {} };
    v.run();
    return std::move (v.issues);
}

void validateOrThrow (const Project& project)
{
    auto issues = validate (project);

    if (issues.empty())
        return;

    std::ostringstream s;
    s << "Invalid project:";

    for (auto& i : issues)
        s << "\n  " << i.path << ": " << i.message;

    throw std::runtime_error (s.str());
}

//==============================================================================
const Channel* findChannel (const Project& p, const Id& id)         { return findById (p.channels, id); }
const Pattern* findPattern (const Project& p, const Id& id)         { return findById (p.patterns, id); }
const PlaylistTrack* findTrack (const Project& p, const Id& id)     { return findById (p.playlist.tracks, id); }
const Insert* findInsert (const Project& p, const Id& id)           { return findById (p.mixer.inserts, id); }

const Id& getClipId (const Clip& c)             { return std::visit ([] (auto& x) -> const Id& { return x.id; }, c); }
const Id& getClipTrack (const Clip& c)          { return std::visit ([] (auto& x) -> const Id& { return x.track; }, c); }
Ticks getClipStart (const Clip& c)              { return std::visit ([] (auto& x) { return x.start; }, c); }
TickLength getClipLength (const Clip& c)        { return std::visit ([] (auto& x) { return x.length; }, c); }
bool isClipMuted (const Clip& c)                { return std::visit ([] (auto& x) { return x.mute; }, c); }

const Id& getEffectId (const Effect& e)         { return std::visit ([] (auto& x) -> const Id& { return x.id; }, e); }

} // namespace gk::model
