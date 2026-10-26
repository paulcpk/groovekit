#include "Project.h"

#include <fstream>
#include <functional>
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
Project parseProject (const Json& json)
{
    Reader reader;
    Project project;
    readValue (reader, json, project);
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
                    if (findChannel (project, pattern.notes[n].channel) == nullptr)
                        issue (at (at ("/patterns", i) + "/notes", n) + "/channel",
                               "no channel \"" + pattern.notes[n].channel + "\"");
            }
        }

        void checkClips()
        {
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
                    checkInsertRef (ac->insert, path + "/insert");
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
