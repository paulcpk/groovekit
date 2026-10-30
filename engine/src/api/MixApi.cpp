#include "ApiHandler.h"
#include "Rpc.h"

#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "analysis/MixAnalysis.h"
#include "compiler/EditCompiler.h"
#include "compiler/EditGraveyard.h"
#include "effects/BuiltinEffects.h"
#include "library/SampleLibrary.h"
#include "library/StarterPack.h"
#include "listening/Listening.h"
#include "listening/KickSound.h"
#include "listening/Maps.h"
#include "mixer/Slots.h"
#include "live/AudioDevices.h"
#include "live/LiveEngine.h"
#include "music/Notes.h"
#include "plugins/PluginCatalog.h"
#include "plugins/PluginWindows.h"
#include "render/Encode.h"
#include "render/Render.h"
#include "render/Stretch.h"
#include "session/Mastering.h"
#include "sampler/SamplerPlugin.h"
#include "guides/Guides.h"
#include "session/AgentNotes.h"
#include "session/Chats.h"
#include "session/Presets.h"
#include "session/RecentProjects.h"
#include "session/Summary.h"
#include "voice/Transcriber.h"

#include <juce_events/juce_events.h>

namespace gk
{

Json ApiHandler::callMix (const std::string& method, const Json& params, Client& client)
{
    using model::ChannelPart;
    const auto projectDir = projectDirOf (session);

    // What a channel's first sample is, from the library when it's there.
    auto entryOf = [&] (const model::Channel& c) -> std::optional<library::SampleEntry>
    {
        auto* sampler = std::get_if<model::SamplerInstrument> (&c.instrument);

        if (sampler == nullptr || sampler->layers.empty())
            return {};

        std::filesystem::path sample = sampler->layers.front().sample;

        if (sample.is_relative())
            sample = projectDir / sample;

        try { return entryFor (sample); } catch (const std::exception&) { return {}; }
    };

    auto partOf = [&] (const model::Channel& c)
    {
        if (c.part)
            return *c.part;

        const auto e = entryOf (c);
        return mixer::guessPart (c.name, e ? e->classification.category : "", e && e->classification.loop,
                                 e ? e->features.bands.sub + e->features.bands.low : 0.0f);
    };

    // The style the song is made in, for slots and balance from its references.
    const auto profile = styleProfile (params);

    auto proposed = [&] (ChannelPart part)
    {
        auto slot = mixer::defaultSlot (part);

        if (profile != nullptr && (part == ChannelPart::bass || part == ChannelPart::synth || part == ChannelPart::vocal))
            slot = mixer::fitToReferences (slot, part, profile->spectrum (part == ChannelPart::bass ? listening::Role::bass : listening::Role::music));

        return slot;
    };

    auto describe = [&] (const model::Channel& c)
    {
        const auto part = partOf (c);
        const auto current = mixer::readSlot (session.getProject(), c);
        return Json { { "channel", c.id }, { "name", c.name }, { "part", model::toString (part) }, { "range", mixer::rangeOf (part) }, { "guessed", ! c.part.has_value() },
                      { "insert", c.insert }, { "slot", current ? current->toJson() : Json() }, { "proposed", proposed (part).toJson() } };
    };

    auto findOrThrow = [&] (const std::string& id) -> const model::Channel&
    {
        if (auto* c = model::findChannel (session.getProject(), id))
            return *c;

        throw RpcError (invalidParams, "No channel \"" + id + "\"");
    };

    // Slots go in as one undo step with whatever the agent did just before.
    ExecuteOptions options;
    options.origin = client.origin;
    options.group = params.contains ("group") ? readParam<std::string> (params, "group") : "slots-" + std::to_string (juce::Time::currentTimeMillis());

    auto setSlot = [&] (const model::Channel& c, ChannelPart part, const mixer::Slot& slot)
    {
        options.label = "Mix slot for " + (c.name.empty() ? c.id : c.name);
        session.execute (mixer::slotCommands (session.getProject(), c.id, part, slot), options);
    };

    if (method == "mix.slots")
    {
        std::vector<std::string> ids;

        if (params.contains ("channels"))
            ids = readParam<std::vector<std::string>> (params, "channels");
        else
            for (auto& c : session.getProject().channels)
                ids.push_back (c.id);

        const auto apply = params.value ("apply", false);
        const auto onlyNew = params.value ("onlyNew", false);
        auto channels = Json::array(), applied = Json::array();

        for (auto& id : ids)
        {
            const auto c = findOrThrow (id);

            if (apply && ! (onlyNew && mixer::readSlot (session.getProject(), c)))
            {
                const auto part = partOf (c);
                setSlot (c, part, proposed (part));
                applied.push_back (id);
            }

            channels.push_back (describe (findOrThrow (id)));
        }

        return { { "channels", channels }, { "applied", applied }, { "style", profile != nullptr ? Json (profile->name) : Json() },
                 { "fromReferences", profile != nullptr && ! profile->spectrum (listening::Role::music).empty() } };
    }

    if (method == "mix.setSlot")
    {
        const auto c = findOrThrow (readParam<std::string> (params, "channel"));     // a copy: the edit replaces the song
        auto part = partOf (c);

        if (params.contains ("part"))
        {
            const auto name = readParam<std::string> (params, "part");
            const auto p = mixer::partFromName (name);

            if (! p)
                throw RpcError (invalidParams, "part: one of kick, rumble, drums, bass, percussion, clap, hats, synth, vocal, fx");

            part = *p;
        }

        // A new part starts from its own slot; otherwise the one there is changed.
        auto slot = (params.contains ("part") && c.part != part) ? proposed (part) : mixer::readSlot (session.getProject(), c).value_or (proposed (part));
        slot.from = "set";

        auto frequency = [&] (const char* key, double& out)
        {
            if (! params.contains (key))
                return;

            const auto v = params[key].is_null() ? 0.0 : readParam<double> (params, key);

            if (v != 0 && (v < 20 || v > 20000))
                throw RpcError (invalidParams, std::string (key) + ": 20 to 20000 Hz, or 0 for none");

            out = v;
        };

        frequency ("lowCutHz", slot.lowCutHz);
        frequency ("highCutHz", slot.highCutHz);
        frequency ("topShelfHz", slot.topShelfHz);

        if (params.contains ("steep"))
            slot.steep = readParam<bool> (params, "steep");

        if (params.contains ("topShelfDb"))
            slot.topShelfDb = std::clamp (readParam<double> (params, "topShelfDb"), -24.0, 24.0);

        if (slot.lowCutHz > 0 && slot.highCutHz > 0 && slot.lowCutHz >= slot.highCutHz)
            throw RpcError (invalidParams, "lowCutHz must be below highCutHz");

        setSlot (c, part, slot);
        return describe (findOrThrow (c.id));
    }

    if (method == "mix.check")
    {
        const auto& project = session.getProject();
        RenderOptions render;

        if (auto v = optionalNumber (params, "start"))  render.start = static_cast<model::Ticks> (*v);
        if (auto v = optionalNumber (params, "end"))    render.end = static_cast<model::Ticks> (*v);

        analysis::MixCheckContext checks;
        checks.barSeconds = static_cast<double> (model::ticksPerBar (project.timeSignature)) / model::ticksPerQuarter * 60 / project.tempo;

        for (auto& c : project.channels)
        {
            checks.parts[c.id] = partOf (c);

            // Ducked by a keyed compressor or a duck on its way to master.
            auto insertId = c.insert;

            for (int hops = 0; hops < 32 && insertId != model::masterInsertId; ++hops)
            {
                const auto* insert = model::findInsert (project, insertId);

                if (insert == nullptr)
                    break;

                for (auto& effect : insert->effects)
                    if (auto* b = std::get_if<model::BuiltinEffect> (&effect); b != nullptr && ! b->bypass
                        && ((b->kind == model::BuiltinEffectKind::compressor && b->sidechain) || b->kind == model::BuiltinEffectKind::duck))
                        checks.ducked.insert (c.id);

                insertId = insert->output;
            }
        }

        if (profile != nullptr)
            checks.referenceBalance = profile->measurements().value ("balance", std::vector<float>());

        // Problems the song shows without listening: too many parts at once or coming in together,
        // parts with no slot, loops out of key. Read now; the renders (listen true) follow as steps.
        auto problems = Json::array();
        Json result;
        const auto maxParts = static_cast<int> (optionalNumber (params, "maxParts").value_or (6));
        const auto arrangement = analysis::checkArrangement (project, checks.parts, maxParts);

        for (auto& p : arrangement["problems"])
            problems.push_back (p);

        result["partsPerBar"] = arrangement["bars"];
        const auto key = project.key ? music::parseKey (*project.key) : std::nullopt;

        for (auto& c : project.channels)
        {
            if (c.mute)
                continue;

            const auto part = checks.parts[c.id];

            if (! mixer::readSlot (project, c))
                problems.push_back ({ { "severity", 7 }, { "kind", "noSlot" }, { "channels", { c.id } },
                                      { "what", c.id + " (" + model::toString (part) + ") has no slot: nothing keeps it out of the other parts' frequencies" },
                                      { "fix", "mix_slots apply for it (or set, with its part)" } });

            const auto pitched = part == ChannelPart::bass || part == ChannelPart::synth || part == ChannelPart::vocal;

            if (! key || ! pitched)
                continue;

            if (auto e = entryOf (c); e && e->classification.loop)
                if (auto fit = library::SampleLibrary::fitTo (*e, *key); fit && ! fit->fits)
                {
                    const auto transpose = std::get<model::SamplerInstrument> (c.instrument).layers.front().transpose;

                    if (((transpose - fit->transpose) % 12 + 12) % 12 != 0)
                        problems.push_back ({ { "severity", 6 }, { "kind", "key" }, { "channels", { c.id } },
                                              { "what", c.id + "'s loop doesn't go with the song's key (" + *project.key + "): its notes rub against the others'" },
                                              { "fix", "transpose its layer to " + std::to_string (fit->transpose) + " semitones, or choose a loop that fits (search_samples fitsKey song)" } });
                }
        }

        result["style"] = profile != nullptr ? Json (profile->name) : Json();

        auto steps = params.value ("listen", true)
            ? analysis::checkMixSteps (context.getEngine(), project, { projectDir, tracktion::Edit::forRendering }, render, projectDir / "renders" / "analysis", checks)
            : std::make_shared<Steps>();
        auto* s = steps.get();

        steps->here ([s, heard = std::move (problems), shown = std::move (result)]
        {
            auto& result = s->result;      // what listening found, or nothing

            if (! result.is_object())
                result = Json::object();

            auto problems = result.value ("problems", Json::array());

            for (auto& p : heard)
                problems.push_back (p);

            for (auto& [key, value] : shown.items())
                result[key] = value;

            std::sort (problems.begin(), problems.end(), [] (const Json& a, const Json& b) { return a["severity"].get<double>() > b["severity"].get<double>(); });
            result["problems"] = problems;
        });

        deferred = steps;
        return {};
    }

    if (method == "mix.audition")
    {
        deferred = auditionSteps (params, false);
        return {};
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

std::shared_ptr<const listening::StyleProfile> ApiHandler::styleProfile (const Json& params) const
{
    const auto name = params.contains ("style") ? readParam<std::string> (params, "style") : session.getProject().style.value_or ("");

    if (listening == nullptr || name.empty())
        return nullptr;

    if (auto p = listening->getStyles().get (name))
        return p;

    // A guide's name or id stands for the style whose references' folder goes with it.
    const auto guides = guides::GuideStore (guides::GuideStore::getDefaultFolder()).list();

    for (auto& n : listening->getStyles().names())
        if (guideFor (guides, n) == Json (name))
            return listening->getStyles().get (n);

    return nullptr;
}

std::shared_ptr<Steps> ApiHandler::auditionSteps (const Json& params, bool tolerant)
{
    using model::ChannelPart;
    const auto projectDir = projectDirOf (session);

    auto entryOf = [&] (const model::Channel& c) -> std::optional<library::SampleEntry>
    {
        auto* sampler = std::get_if<model::SamplerInstrument> (&c.instrument);

        if (sampler == nullptr || sampler->layers.empty())
            return {};

        std::filesystem::path sample = sampler->layers.front().sample;

        if (sample.is_relative())
            sample = projectDir / sample;

        try { return entryFor (sample); } catch (const std::exception&) { return {}; }
    };

    auto partOf = [&] (const model::Channel& c)
    {
        if (c.part)
            return *c.part;

        const auto e = entryOf (c);
        return mixer::guessPart (c.name, e ? e->classification.category : "", e && e->classification.loop,
                                 e ? e->features.bands.sub + e->features.bands.low : 0.0f);
    };

    const auto profile = styleProfile (params);

    auto proposed = [&] (ChannelPart part)
    {
        auto slot = mixer::defaultSlot (part);

        if (profile != nullptr && (part == ChannelPart::bass || part == ChannelPart::synth || part == ChannelPart::vocal))
            slot = mixer::fitToReferences (slot, part, profile->spectrum (part == ChannelPart::bass ? listening::Role::bass : listening::Role::music));

        return slot;
    };

    auto findOrThrow = [&] (const std::string& id) -> const model::Channel&
    {
        if (auto* c = model::findChannel (session.getProject(), id))
            return *c;

        throw RpcError (invalidParams, "No channel \"" + id + "\"");
    };

    const auto& project = session.getProject();
    const model::Channel* channel = params.contains ("channel") ? &findOrThrow (readParam<std::string> (params, "channel")) : nullptr;

    // What to try: samples (paths), or candidates that are a sample or an instrument, each maybe
    // with notes of its own to play in a phantom slot.
    struct Candidate
    {
        std::optional<std::string> path;
        std::optional<model::Instrument> instrument;
        std::optional<std::vector<model::Note>> notes;
        std::optional<ChannelPart> part;
        std::optional<library::SampleEntry> entry;
        std::string source;     // as given, for its audition's file name
    };

    auto notesOf = [&] (Json list)
    {
        for (auto& n : list)
            if (n.is_object())
                n["channel"] = "phantom";

        return readParam<std::vector<model::Note>> (Json { { "notes", list } }, "notes");
    };

    auto partNamed = [&] (const Json& j) -> std::optional<ChannelPart>
    {
        if (! j.contains ("part"))
            return {};

        if (auto p = mixer::partFromName (readParam<std::string> (j, "part")))
            return p;

        throw RpcError (invalidParams, "part: one of kick, rumble, drums, bass, percussion, clap, hats, synth, vocal, fx");
    };

    std::vector<Candidate> candidates;

    if (params.contains ("candidates"))
        for (auto& c : params["candidates"])
        {
            Candidate candidate;

            if (c.contains ("path"))        candidate.path = readParam<std::string> (c, "path");
            if (c.contains ("instrument"))  candidate.instrument = readParam<model::Instrument> (c, "instrument");
            if (c.contains ("notes"))       candidate.notes = notesOf (c["notes"]);
            candidate.part = partNamed (c);
            candidate.source = c.dump();

            if (! candidate.path && ! candidate.instrument)
                throw RpcError (invalidParams, "candidates: each needs a path or an instrument");

            candidates.push_back (std::move (candidate));
        }
    else
        for (auto& path : readParam<std::vector<std::string>> (params, "paths"))
            candidates.push_back ({ path, {}, {}, {}, {}, path });

    if (candidates.empty() || candidates.size() > 20)
        throw RpcError (invalidParams, "paths: one to ten samples");

    const auto sharedNotes = params.contains ("notes") ? std::optional (notesOf (params["notes"])) : std::nullopt;
    const auto sharedPart = partNamed (params);

    for (auto& c : candidates)
        if (c.path && c.path->size() > 0)
        {
            try { c.entry = entryFor (*c.path); } catch (const std::exception&) {}
        }

    const auto bar = model::ticksPerBar (project.timeSignature);
    RenderOptions render;
    render.sampleRate = 44100;
    render.start = static_cast<model::Ticks> (optionalNumber (params, "start").value_or (0));
    render.end = static_cast<model::Ticks> (optionalNumber (params, "end").value_or (static_cast<double> (*render.start + 8 * bar)));

    const auto folder = getDefaultCacheDir() / "auditions" / "in-song";
    const CompileOptions compile { projectDir, tracktion::Edit::forRendering };
    auto fileFor = [&] (const std::string& what) { return folder / (std::to_string (std::hash<std::string> {} (what)) + ".wav"); };
    const auto key = project.key ? music::parseKey (*project.key) : std::nullopt;
    auto isPitched = [] (ChannelPart p) { return p == ChannelPart::bass || p == ChannelPart::synth || p == ChannelPart::vocal; };

    Json result { { "bars", { *render.start / bar + 1, *render.end / bar + 1 } } };
    std::optional<library::SampleEntry> currentEntry;

    // What a candidate's try needs between its steps.
    struct Run
    {
        Json result, out = Json::array();
        std::shared_ptr<analysis::Placement> placement;
        bool failed = false;
    };

    auto run = std::make_shared<Run>();
    auto steps = std::make_shared<Steps>();
    auto& engine = context.getEngine();

    // One try: the Edits made here, the renders pooled, then what it measured taken up; with
    // tolerant, a try that fails is reported as such instead of failing the whole audition.
    auto addTry = [&, run, steps, render, tolerant] (model::Project trial, model::Id id, std::filesystem::path file,
                                                     std::function<void (Json&)> took, Json onError)
    {
        steps->here ([run, &engine, trial = std::move (trial), id, file, compile, tolerant, onError]
        {
            run->failed = false;

            try
            {
                run->placement = analysis::preparePlacement (engine, trial, id, compile, file);
            }
            catch (const std::exception& e)
            {
                if (! tolerant)
                    throw;

                run->failed = true;
                auto j = onError;
                j["error"] = e.what();
                run->out.push_back (j);
            }
        });

        auto tried = std::make_shared<Json>();

        steps->pooled ([run, render, tried, tolerant, onError]
        {
            if (run->failed)
                return;

            try
            {
                *tried = analysis::renderPlacement (*run->placement, render);
            }
            catch (const std::exception& e)
            {
                if (! tolerant)
                    throw;

                run->failed = true;
                auto j = onError;
                j["error"] = e.what();
                run->out.push_back (j);
            }
        });

        steps->here ([run, tried, took]
        {
            run->placement.reset();   // its Edits go here, on the message thread

            if (! run->failed)
                took (*tried);
        });
    };

    if (channel != nullptr)
    {
        std::optional<std::string> currentPath;

        if (auto* sampler = std::get_if<model::SamplerInstrument> (&channel->instrument); sampler != nullptr && ! sampler->layers.empty())
            currentPath = sampler->layers.front().sample;

        result["channel"] = channel->id;
        result["part"] = model::toString (partOf (*channel));
        currentEntry = entryOf (*channel);

        if (! mixer::readSlot (project, *channel))
            result["note"] = "The channel has no slot yet: give it one first (mix_slots), so each candidate is heard in its place";

        addTry (project, channel->id, fileFor (channel->id + "@current"), [run, currentPath] (Json& current)
        {
            if (currentPath)
                current["path"] = *currentPath;

            run->result["current"] = current;
        }, Json::object());
    }

    // Samples play as loud as the sound they'd replace, so the louder one doesn't win for being
    // louder; in a phantom slot, as loud as each other.
    std::optional<float> targetLufs;

    if (currentEntry)
        targetLufs = currentEntry->features.integratedLufs;
    else
    {
        std::vector<float> loudness;

        for (auto& c : candidates)
            if (c.entry && c.entry->features.integratedLufs)
                loudness.push_back (*c.entry->features.integratedLufs);

        if (! loudness.empty())
        {
            std::nth_element (loudness.begin(), loudness.begin() + static_cast<long> (loudness.size() / 2), loudness.end());
            targetLufs = loudness[loudness.size() / 2];
        }
    }

    // Notes for a phantom one-shot or instrument when none are given: a bar of its part's usual figure.
    auto figureFor = [&] (ChannelPart part)
    {
        const auto tonic = key ? key->tonic : 9;
        std::vector<model::Note> notes;
        auto add = [&] (model::Ticks start, model::TickLength length, int k) { notes.push_back ({ "phantom", start, length, k, 100 }); };

        for (int beat = 0; beat < 4; ++beat)
            switch (part)
            {
                case ChannelPart::hats:         add (beat * 960 + 480, 120, 60); break;
                case ChannelPart::percussion:   if (beat % 2 == 1) add (beat * 960, 240, 60); break;
                case ChannelPart::bass:         add (beat * 960 + 480, 360, 36 + tonic); break;
                case ChannelPart::synth:
                case ChannelPart::vocal:
                case ChannelPart::fx:           if (beat % 2 == 0) add (beat * 960, 720, 48 + tonic); break;
                default:                        add (beat * 960, 240, 60); break;
            }

        return notes;
    };


    for (auto& candidate : candidates)
    {
        const auto entry = candidate.entry;

        if (candidate.path && ! entry && ! candidate.instrument)
        {
            run->out.push_back ({ { "path", *candidate.path }, { "error", "can't be read" } });
            continue;
        }

        auto trial = project;
        model::Id id;
        auto part = ChannelPart::synth;
        model::SamplerLayer* layer = nullptr;
        model::Channel* placed = nullptr;

        if (channel != nullptr)
        {
            // In the channel's place, playing its notes or clips: a sample in its first layer (loops at
            // their exact tempo, pitched one-shots on their root), or a whole instrument.
            id = channel->id;
            part = partOf (*channel);
            placed = &*std::find_if (trial.channels.begin(), trial.channels.end(), [&] (auto& x) { return x.id == id; });

            if (candidate.instrument)
                placed->instrument = *candidate.instrument;
            else
            {
                auto* sampler = std::get_if<model::SamplerInstrument> (&placed->instrument);

                if (sampler == nullptr || sampler->layers.empty())
                {
                    model::SamplerInstrument s;
                    s.layers.push_back ({});
                    s.layers.front().id = "l";
                    placed->instrument = s;
                    sampler = &std::get<model::SamplerInstrument> (placed->instrument);
                }

                layer = &sampler->layers.front();
                layer->sample = entry->path;

                if (layer->sourceTempo && entry->classification.loop)
                    if (auto tempo = library::SampleLibrary::loopTempo (*entry))
                        layer->sourceTempo = *tempo;

                if (! entry->classification.loop && isPitched (part))
                    if (auto root = library::SampleLibrary::keyOf (*entry).root)
                        layer->rootKey = *root;
            }
        }
        else
        {
            // A phantom slot: a channel of its own through its part's slot, as if it were in the
            // arrangement: a loop back to back over the bars, a one-shot or an instrument playing notes.
            part = candidate.part.value_or (sharedPart.value_or (entry ? mixer::guessPart (std::filesystem::path (entry->path).stem().string(),
                                                                                                entry->classification.category, entry->classification.loop,
                                                                                                entry->features.bands.sub + entry->features.bands.low)
                                                                        : ChannelPart::synth));
            id = "audition";

            auto taken = [&] (const model::Id& x)
            {
                return model::findChannel (trial, x) != nullptr || model::findInsert (trial, x) != nullptr
                    || std::any_of (trial.patterns.begin(), trial.patterns.end(), [&] (auto& p) { return p.id == x; })
                    || std::any_of (trial.playlist.clips.begin(), trial.playlist.clips.end(), [&] (auto& c) { return std::visit ([&] (auto& v) { return v.id.starts_with (x); }, c); });
            };

            while (taken (id))
                id += "-x";

            model::Insert insert;
            insert.id = id;
            insert.name = "Audition";
            insert.effects.push_back (model::Effect (mixer::slotEffect (proposed (part))));
            trial.mixer.inserts.push_back (insert);

            const auto notes = candidate.notes ? candidate.notes : sharedNotes;
            const auto loopTempo = entry && entry->classification.loop ? library::SampleLibrary::loopTempo (*entry) : std::nullopt;
            const auto asLoop = ! candidate.instrument && ! notes && loopTempo;

            if (! candidate.instrument && ! notes && entry && entry->classification.loop && ! loopTempo)
            {
                run->out.push_back ({ { "path", entry->path }, { "error", "a loop with no tempo needs a channel or notes to play" } });
                continue;
            }

            model::Channel c;
            c.id = id;
            c.name = "Audition";
            c.insert = id;
            c.group = asLoop ? model::ChannelGroup::arrangement : model::ChannelGroup::rack;
            c.part = part;

            if (candidate.instrument)
                c.instrument = *candidate.instrument;
            else
            {
                model::SamplerLayer l;
                l.id = "l";
                l.sample = entry->path;

                if (asLoop)
                    l.sourceTempo = *loopTempo;
                else if (isPitched (part))
                    l.rootKey = library::SampleLibrary::keyOf (*entry).root.value_or (60);
                else
                    l.pitchTracking = false;

                model::SamplerInstrument sampler;
                sampler.layers.push_back (l);
                c.instrument = sampler;
            }

            trial.channels.push_back (c);
            placed = &trial.channels.back();

            if (auto* sampler = std::get_if<model::SamplerInstrument> (&placed->instrument); sampler != nullptr && ! sampler->layers.empty())
                layer = &sampler->layers.front();

            int row = 0;

            for (auto& clip : trial.playlist.clips)
                row = std::max (row, std::visit ([] (auto& v) { return static_cast<int> (v.row) + 1; }, clip));

            if (asLoop)
            {
                // A clip per time round, back to back: whole beats long, so it stays on the grid.
                const auto beats = std::max (1.0, std::round (entry->features.durationSeconds * *loopTempo / 60));
                const auto every = static_cast<model::Ticks> (beats * model::ticksPerQuarter);
                int n = 0;

                for (auto at = *render.start; at < *render.end; at += every)
                {
                    model::AudioClip clip;
                    clip.id = id + "-" + std::to_string (n++);
                    clip.row = row;
                    clip.start = at;
                    clip.length = std::min (every, *render.end - at);
                    clip.channel = id;
                    trial.playlist.clips.push_back (clip);
                }
            }
            else
            {
                model::Pattern pattern;
                pattern.id = id;
                pattern.notes = notes ? *notes : figureFor (part);

                for (auto& n : pattern.notes)
                    n.channel = id;

                trial.patterns.push_back (pattern);

                model::PatternClip clip;
                clip.id = id + "-0";
                clip.row = row;
                clip.start = *render.start;
                clip.length = *render.end - *render.start;
                clip.pattern = id;
                trial.playlist.clips.push_back (clip);
            }
        }

        double gain = 0;

        if (layer != nullptr && ! candidate.instrument && targetLufs && entry && entry->features.integratedLufs)
        {
            gain = std::clamp (static_cast<double> (*targetLufs - *entry->features.integratedLufs), -18.0, 18.0);
            layer->gainDb = std::clamp (layer->gainDb + gain, -60.0, 24.0);
        }

        const auto inPlace = channel != nullptr;
        Json onError = entry ? Json { { "path", entry->path } } : Json::object();

        addTry (std::move (trial), id, fileFor (id + "@" + candidate.source + "@" + (sharedNotes ? params["notes"].dump() : "") + "@" + std::to_string (*render.start)),
                [run, entry, gain, inPlace, part, key] (Json& j)
        {
            if (entry)
            {
                j["path"] = entry->path;
                j["name"] = std::filesystem::path (entry->path).filename().string();
            }

            j["gainDb"] = std::round (gain * 10) / 10;

            if (! inPlace)
                j["part"] = model::toString (part);

            // Whether a sample's notes go with the song's.
            if (key && entry && (part == ChannelPart::bass || part == ChannelPart::synth || part == ChannelPart::vocal))
                if (auto fit = library::SampleLibrary::fitTo (*entry, *key))
                    j["key"] = { { "fits", fit->fits }, { "transpose", fit->transpose } };

            run->out.push_back (std::move (j));
        }, onError);
    }

    // Candidates that couldn't be tried were noted as the loop went: they keep their place.
    run->result = std::move (result);
    auto* final = &steps->result;

    steps->here ([run, final]
    {
        run->result["candidates"] = run->out;
        *final = std::move (run->result);
    });

    return steps;

}

} // namespace gk
