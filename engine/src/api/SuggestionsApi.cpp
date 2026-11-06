// Sounds for the song, picked out in the background: once the song has something in it and has
// stood still for a few seconds, the library is ranked for every part it could use next (hat loops,
// claps, percussion, bass and synth loops...) by how well each sample fits the song's style and the
// user's taste, at the song's tempo and in its key, leaving out what the song already uses; then the
// first few of each list are tried in the song (rendered with its busiest bars, as mix.audition
// does), so the agent can put them to the user at once instead of searching and rendering first.
//
// Off unless the user turns it on (the app does, by default); kept in <app data>/suggestions.json.
// Only an engine serving the app works in the background (backgroundSuggestions).

#include "ApiHandler.h"
#include "Rpc.h"

#include "EngineContext.h"
#include "listening/Listening.h"
#include "mixer/Slots.h"
#include "music/Notes.h"
#include "render/Render.h"
#include "render/Stretch.h"

#include <juce_events/juce_events.h>

#include <fstream>
#include <set>

namespace gk
{

namespace
{
    /** A list the background keeps ready: which samples, the part they'd play and how they're judged. */
    struct ListSpec
    {
        const char* id;
        const char* label;
        model::ChannelPart part;
        const char* category;
        std::optional<bool> loop;
        const char* role;
        bool atTempo = false;       // loops: at the song's tempo (or half or double)
        bool inKey = false;         // tonal: goes with the song's key
        bool onlyWithoutKick = false;
    };

    using P = model::ChannelPart;

    const std::vector<ListSpec>& listSpecs()
    {
        static const std::vector<ListSpec> specs {
            { "hatLoops",   "Hat loops",            P::hats,       "hat",    true,  "hats",    true },
            { "hats",       "Hat one-shots",        P::hats,       "hat",    false, "hatHit" },
            { "topLoops",   "Top and drum loops",   P::drums,      "drums",  true,  "drums",   true },
            { "percLoops",  "Percussion loops",     P::percussion, "perc",   true,  "drums",   true },
            { "perc",       "Percussion hits",      P::percussion, "perc",   false, "percHit" },
            { "claps",      "Claps",                P::clap,       "clap",   false, "percHit" },
            { "snares",     "Snares",               P::clap,       "snare",  false, "percHit" },
            { "cymbals",    "Cymbals and rides",    P::hats,       "cymbal", false, "hatHit" },
            { "bassLoops",  "Bass loops",           P::bass,       "bass",   true,  "bass",    true, true },
            { "synthLoops", "Synth loops",          P::synth,      "synth",  true,  "music",   true, true },
            { "synths",     "Synth one-shots",      P::synth,      "synth",  false, "music",   false, true },
            { "vocals",     "Vocals",               P::vocal,      "vocal",  {},    "music",   false, true },
            { "fx",         "FX",                   P::fx,         "fx",     {},    "music" },
            { "kicks",      "Kick one-shots",       P::kick,       "kick",   false, "kickHit", false, false, true },
            { "kickLoops",  "Kick loops",           P::kick,       "kick",   true,  "kick",    true,  false, true },
        };
        return specs;
    }

    constexpr int listLength = 24;
    constexpr int quietMs = 5000;           // how long the song stands still before the lists are made again
    constexpr int barsTried = 8;

    /** The fields of a suggested sample the agent and the browser need. */
    Json brief (const Json& sample)
    {
        Json j = Json::object();

        for (auto* k : { "path", "description", "sounds", "category", "loop", "durationSeconds", "tempo", "sourceTempo", "key",
                         "rootKey", "keyFit", "fit", "taste", "rating" })
            if (sample.contains (k))
                j[k] = sample[k];

        return j;
    }
}

struct ApiHandler::Suggestions
{
    bool loaded = false;
    bool enabled = false;
    bool chosen = false;                // the setting was ever made: the app turns it on otherwise
    int tries = 5;                      // tried in the song per list

    int64_t edits = 0;                  // changes to the song so far
    std::shared_ptr<std::atomic<bool>> round;   // the running round's: false once another starts

    std::string state = "off";          // off, waiting, finding, trying, ready
    std::string reason;                 // what it waits for
    std::string style;
    Json lists = Json::array();
    std::string listSignature;
    Json bars;                          // [from, to]: the bars candidates are tried over (1-based, to excluded)
    std::filesystem::path rest;         // the song as last rendered for the tries
};

std::filesystem::path ApiHandler::suggestionsSettingsFile() const
{
    return suggestionsFile.empty() ? EngineContext::getAppDataFolder() / "suggestions.json" : suggestionsFile;
}

ApiHandler::Suggestions& ApiHandler::getSuggestions()
{
    if (suggestions == nullptr)
        suggestions = std::make_shared<Suggestions>();

    auto& s = *suggestions;

    if (! s.loaded)
    {
        s.loaded = true;

        try
        {
            std::ifstream in (suggestionsSettingsFile());

            if (in)
            {
                const auto j = Json::parse (in);
                s.enabled = j.value ("enabled", false);
                s.tries = std::clamp (j.value ("tries", 5), 0, 20);
                s.chosen = true;
            }
        }
        catch (const std::exception&)
        {
            // A broken file counts as never set.
        }
    }

    return s;
}

void ApiHandler::songChangedForSuggestions()
{
    if (! backgroundSuggestions)
        return;

    auto& s = getSuggestions();
    const auto edit = ++s.edits;

    if (! s.enabled)
        return;

    // What was tried is for the song as it was: it's tried again once the song stands still.
    juce::Timer::callAfterDelay (quietMs, [this, alive = alive, edit]
    {
        if (*alive && suggestions != nullptr && suggestions->edits == edit)
            startSuggestions();
    });
}

void ApiHandler::notifySuggestions()
{
    if (broadcast)
        broadcast (notification ("suggestions", suggestionsJson (false, {}, 0)).dump());
}

Json ApiHandler::suggestionsJson (bool full, const std::string& which, int limit)
{
    auto& s = getSuggestions();
    Json result { { "enabled", s.enabled }, { "chosen", s.chosen }, { "tries", s.tries }, { "state", s.state } };

    if (! s.reason.empty())     result["reason"] = s.reason;
    if (! s.style.empty())      result["style"] = s.style;
    if (! s.bars.is_null())     result["bars"] = s.bars;

    // Samples the song has taken up since are left out.
    std::set<std::string> used;
    const auto dir = projectDirOf (session);

    for (auto& c : session.getProject().channels)
        if (auto* sampler = std::get_if<model::SamplerInstrument> (&c.instrument))
            for (auto& l : sampler->layers)
            {
                std::filesystem::path p = l.sample;
                used.insert ((p.is_relative() ? dir / p : p).lexically_normal().string());
            }

    auto lists = Json::array();
    const auto wanted = mixer::partFromName (which);

    for (auto& list : s.lists)
    {
        const auto matches = which.empty() || list["id"] == which || (wanted && list["part"] == model::toString (*wanted));

        if (! matches)
            continue;

        // Tries stand while the song hasn't changed since they were made.
        const auto fresh = list.value ("triedAt", int64_t (-1)) == s.edits;
        auto samples = Json::array();
        int tried = 0;

        for (auto& sample : list["samples"])
        {
            if (used.contains (std::filesystem::path (sample["path"].get<std::string>()).lexically_normal().string()))
                continue;

            auto j = sample;

            if (j.contains ("tried"))
            {
                if (fresh && ! j["tried"].contains ("error"))
                    ++tried;
                else
                    j.erase ("tried");
            }

            samples.push_back (std::move (j));
        }

        Json entry { { "id", list["id"] }, { "label", list["label"] }, { "part", list["part"] }, { "inSong", list["inSong"] },
                     { "count", samples.size() }, { "tried", tried } };

        if (full)
        {
            if (limit > 0 && static_cast<int> (samples.size()) > limit)
                samples.erase (samples.begin() + limit, samples.end());

            entry["samples"] = std::move (samples);
        }
        else
        {
            auto top = Json::array();

            for (size_t n = 0; n < std::min<size_t> (3, samples.size()); ++n)
                top.push_back (std::filesystem::path (samples[n]["path"].get<std::string>()).stem().string());

            entry["top"] = top;
        }

        lists.push_back (std::move (entry));
    }

    result["lists"] = lists;
    return result;
}

Json ApiHandler::callSuggestions (const std::string& method, const Json& params)
{
    auto& s = getSuggestions();

    if (method == "suggestions.get")
    {
        const auto which = params.value ("part", params.value ("list", std::string()));

        if (! which.empty() && ! mixer::partFromName (which)
            && std::none_of (listSpecs().begin(), listSpecs().end(), [&] (auto& spec) { return which == spec.id; }))
            throw RpcError (invalidParams, "part: a part (hats, percussion, clap, drums, bass, synth, vocal, fx, kick) or a list's id");

        return suggestionsJson (params.value ("full", false) || ! which.empty(), which, params.value ("limit", listLength));
    }

    if (method == "suggestions.configure")
    {
        if (params.contains ("enabled"))   s.enabled = readParam<bool> (params, "enabled");
        if (params.contains ("tries"))     s.tries = std::clamp (readParam<int> (params, "tries"), 0, 20);

        s.chosen = true;

        try
        {
            const auto file = suggestionsSettingsFile();
            std::filesystem::create_directories (file.parent_path());
            std::ofstream (file) << Json { { "enabled", s.enabled }, { "tries", s.tries } }.dump (2);
        }
        catch (const std::exception&)
        {
            // Kept for as long as the engine runs.
        }

        if (s.enabled)
        {
            // Tried again with the new number, the lists kept.
            if (params.contains ("tries"))
                for (auto& list : s.lists)
                    list.erase ("triedAt");

            startSuggestions();
        }
        else
        {
            if (s.round)
                *s.round = false;

            s.round = nullptr;
            s.state = "off";
            s.reason.clear();
            notifySuggestions();
        }

        return suggestionsJson (false, {}, 0);
    }

    if (method == "suggestions.refresh")
    {
        s.listSignature.clear();
        startSuggestions();
        return suggestionsJson (false, {}, 0);
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

void ApiHandler::enableBackgroundSuggestions()
{
    backgroundSuggestions = true;

    // A song just opened: its lists are made once it's had a moment.
    juce::Timer::callAfterDelay (quietMs / 2, [this, alive = alive]
    {
        if (*alive && getSuggestions().enabled)
            startSuggestions();
    });
}

void ApiHandler::stopSuggestions()
{
    if (suggestions != nullptr && suggestions->round)
        *suggestions->round = false;
}

void ApiHandler::startSuggestions()
{
    auto& s = getSuggestions();

    if (s.round)
        *s.round = false;

    s.round = nullptr;

    auto wait = [&] (std::string why)
    {
        s.state = s.enabled ? "waiting" : "off";
        s.reason = s.enabled ? std::move (why) : "";
        notifySuggestions();
    };

    if (! s.enabled)
        return wait ({});

    if (! backgroundSuggestions)
        return wait ("this engine doesn't work in the background");

    if (listening == nullptr || library == nullptr || ! listening->getModels().getStatus().ready || listening->getIndex().size() == 0)
        return wait ("GrooveKit has to hear the sample library first (Browser)");

    const auto profile = styleProfile (Json::object());

    if (profile == nullptr)
        return wait ("the song needs a style with reference tracks");

    const auto project = session.getProject();

    if (project.channels.empty())
        return wait ("nothing in the song yet");

    // What the song has: its samples (left out of the lists) and its parts (their lists come last).
    const auto dir = projectDirOf (session);
    std::set<std::string> used;
    std::set<model::ChannelPart> parts;

    for (auto& c : project.channels)
    {
        std::optional<library::SampleEntry> entry;

        if (auto* sampler = std::get_if<model::SamplerInstrument> (&c.instrument))
            for (auto& l : sampler->layers)
            {
                std::filesystem::path p = l.sample;
                p = (p.is_relative() ? dir / p : p).lexically_normal();
                used.insert (p.string());

                if (! entry && ! c.part)
                    try { entry = entryFor (p); } catch (const std::exception&) {}
            }

        parts.insert (c.part ? *c.part
                             : mixer::guessPart (c.name, entry ? entry->classification.category : "", entry && entry->classification.loop,
                                                 entry ? entry->features.bands.sub + entry->features.bands.low : 0.0f));
    }

    const auto songKey = project.key ? music::parseKey (*project.key) : std::nullopt;
    std::vector<ListSpec> specs;

    for (auto& spec : listSpecs())
        if (! spec.onlyWithoutKick || ! parts.contains (P::kick))
            specs.push_back (spec);

    // The parts the song doesn't have first: they're what it's most likely to want next.
    std::stable_sort (specs.begin(), specs.end(), [&] (auto& a, auto& b) { return ! parts.contains (a.part) && parts.contains (b.part); });

    std::string signature = profile->name + "|" + std::to_string (project.tempo) + "|" + project.key.value_or ("") + "|";

    for (auto& u : used)
        signature += u + "\n";

    for (auto& spec : specs)
        signature += std::string (spec.id) + (parts.contains (spec.part) ? "+" : "-");

    // The busiest bars: where most channels play at once (8 bars, from every 4th).
    const auto bar = model::ticksPerBar (project.timeSignature);
    model::Ticks end = 0;

    for (auto& clip : project.playlist.clips)
        std::visit ([&] (auto& c) { end = std::max (end, c.start + c.length); }, clip);

    model::Ticks from = 0;
    size_t most = 0;

    for (model::Ticks at = 0; at < end; at += 4 * bar)
    {
        std::set<std::string> playing;

        for (auto& clip : project.playlist.clips)
        {
            if (auto* a = std::get_if<model::AudioClip> (&clip); a != nullptr && a->start < at + barsTried * bar && a->start + a->length > at)
                playing.insert (a->channel);

            if (auto* p = std::get_if<model::PatternClip> (&clip); p != nullptr && p->start < at + barsTried * bar && p->start + p->length > at)
                for (auto& pattern : project.patterns)
                    if (pattern.id == p->pattern)
                        for (auto& n : pattern.notes)
                            playing.insert (n.channel);
        }

        if (playing.size() > most)
        {
            most = playing.size();
            from = at;
        }
    }

    auto round = std::make_shared<std::atomic<bool>> (true);
    s.round = round;
    s.style = profile->name;
    s.bars = Json::array ({ from / bar + 1, from / bar + 1 + barsTried });
    s.reason.clear();

    const auto edit = s.edits;
    const auto search = s.listSignature != signature || s.lists.empty();
    auto found = std::make_shared<Json> (Json::array());
    auto steps = std::make_shared<Steps>();

    if (search)
    {
        s.state = "finding";
        notifySuggestions();

        steps->pooled ([listening = listening, specs, style = profile->name, tempo = project.tempo, songKey, used, parts, found]
        {
            for (auto& spec : specs)
            {
                auto find = [&] (bool strict)
                {
                    library::SampleQuery q;
                    q.category = spec.category;
                    q.loop = spec.loop;
                    q.limit = listLength + static_cast<int> (used.size());

                    if (strict && spec.atTempo)             q.tempo = tempo;
                    if (strict && spec.inKey && songKey)    q.fitsKey = songKey;

                    listening::Listening::SoundQuery sound;
                    sound.style = style;
                    sound.role = listening::roleFromName (spec.role);

                    auto samples = Json::array();
                    const auto results = listening->search (q, sound);

                    for (auto& r : results["results"])
                        if (! used.contains (std::filesystem::path (r["path"].get<std::string>()).lexically_normal().string())
                            && r.value ("rating", std::string()) != "doesn't fit" && samples.size() < listLength)
                            samples.push_back (brief (r));

                    return samples;
                };

                Json samples;

                try
                {
                    samples = find (true);

                    // Few at the song's tempo or in its key: the closest of the rest after them.
                    if (samples.size() < 8 && (spec.atTempo || (spec.inKey && songKey)))
                        for (auto& more : find (false))
                            if (samples.size() < listLength
                                && std::none_of (samples.begin(), samples.end(), [&] (auto& x) { return x["path"] == more["path"]; }))
                                samples.push_back (more);
                }
                catch (const std::exception&)
                {
                    continue;
                }

                if (! samples.empty())
                    found->push_back ({ { "id", spec.id }, { "label", spec.label }, { "part", model::toString (spec.part) },
                                        { "inSong", parts.contains (spec.part) }, { "samples", std::move (samples) } });
            }
        });

        steps->here ([this, round, found, signature]
        {
            if (! *round)
                return;

            suggestions->lists = std::move (*found);
            suggestions->listSignature = signature;
        });
    }

    // Then the song over those bars, rendered once for every try.
    struct Rest
    {
        std::optional<CompiledEdit> edit;
        model::Project project;
    };

    auto rest = std::make_shared<Rest>();
    rest->project = project;
    const auto folder = getDefaultCacheDir() / "auditions" / "suggested";
    const auto restFile = folder / ("song-" + std::to_string (edit) + "-" + std::to_string (reinterpret_cast<uintptr_t> (round.get())) + ".wav");
    RenderOptions render;
    render.sampleRate = 44100;
    render.start = from;
    render.end = from + barsTried * bar;

    steps->here ([this, round, rest, dir]
    {
        if (! *round || suggestions->tries == 0)
            return;

        suggestions->state = "trying";
        notifySuggestions();

        // Tries of earlier rounds (a pick window may still play one from the last half hour), and
        // the song as rendered for them (each try links it in beside itself).
        std::error_code ec;
        const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::minutes (30);

        for (auto& f : std::filesystem::directory_iterator (getDefaultCacheDir() / "auditions" / "suggested", ec))
            if (f.is_regular_file (ec) && (f.path().filename().string().starts_with ("song-") || f.last_write_time (ec) < cutoff))
                std::filesystem::remove (f.path(), ec);

        rest->edit.emplace (compileProject (context.getEngine(), rest->project, { dir, tracktion::Edit::forRendering }));
    });

    steps->pooled ([round, rest, restFile, render]
    {
        if (! *round || ! rest->edit)
            return;

        std::filesystem::create_directories (restFile.parent_path());
        renderToWav (*rest->edit, rest->project, restFile, render);
    });

    steps->here ([this, round, rest, restFile]
    {
        rest->edit.reset();   // its Edit goes here, on the message thread

        if (*round)
            suggestions->rest = restFile;
    });

    steps->runLater (context, round, [this, steps, round, edit] (std::exception_ptr error)
    {
        if (! *round)
            return;

        if (error || suggestions->tries == 0 || suggestions->rest.empty())
        {
            suggestions->state = "ready";
            notifySuggestions();
            return;
        }

        notifySuggestions();
        trySuggestions (round, edit, 0);
    });
}

void ApiHandler::trySuggestions (std::shared_ptr<std::atomic<bool>> round, int64_t edit, size_t next)
{
    auto& s = *suggestions;

    // The next list not tried for this song yet.
    while (next < s.lists.size() && s.lists[next].value ("triedAt", int64_t (-1)) == edit)
        ++next;

    if (next >= s.lists.size())
    {
        s.state = "ready";
        notifySuggestions();
        return;
    }

    auto& list = s.lists[next];
    auto paths = Json::array();

    for (auto& sample : list["samples"])
        if (static_cast<int> (paths.size()) < s.tries)
            paths.push_back (sample["path"]);

    const auto bar = model::ticksPerBar (session.getProject().timeSignature);
    const auto from = (s.bars[0].get<int64_t>() - 1) * bar;
    const Json params { { "paths", paths }, { "part", list["part"] }, { "start", from }, { "end", from + barsTried * bar } };
    std::shared_ptr<Steps> steps;

    try
    {
        if (! paths.empty())
            steps = auditionSteps (params, true, { getDefaultCacheDir() / "auditions" / "suggested", s.rest });
    }
    catch (const std::exception&)
    {
    }

    const auto id = list["id"];

    if (steps == nullptr)
    {
        list["triedAt"] = edit;
        return trySuggestions (round, edit, next + 1);
    }

    steps->runLater (context, round, [this, steps, round, edit, next, id] (std::exception_ptr error)
    {
        if (! *round)
            return;

        auto& s = *suggestions;

        if (next < s.lists.size() && s.lists[next]["id"] == id)
        {
            auto& list = s.lists[next];

            if (! error && steps->result.contains ("candidates"))
            {
                // Each try onto its sample: the audition file, how much room it has in the mix
                // (0-100), the bands where it would cover the song, and its level against the mix.
                for (auto& t : steps->result["candidates"])
                    for (auto& sample : list["samples"])
                        if (sample["path"] == t.value ("path", Json()))
                        {
                            if (t.contains ("error"))
                                sample["tried"] = { { "error", t["error"] } };
                            else
                                sample["tried"] = { { "audition", t.value ("file", "") }, { "room", t.value ("fit", 0) },
                                                    { "covers", t.value ("covers", Json::array()) }, { "levelDb", t.value ("levelDb", 0.0) } };
                        }

                // Those tried first, by how they fit the style and sit in the song together.
                auto score = [] (const Json& x)
                {
                    if (! x.contains ("tried") || x["tried"].contains ("error"))
                        return -1.0;

                    return 0.65 * std::clamp (x.value ("fit", 0.0), 0.0, 100.0) + 0.35 * x["tried"].value ("room", 0.0);
                };

                auto& samples = list["samples"];
                std::stable_sort (samples.begin(), samples.end(), [&] (const Json& a, const Json& b) { return score (a) > score (b); });
            }

            list["triedAt"] = edit;
            notifySuggestions();
        }

        trySuggestions (round, edit, next + 1);
    });
}

} // namespace gk
