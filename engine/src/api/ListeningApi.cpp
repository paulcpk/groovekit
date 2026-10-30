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

Json ApiHandler::callListening (const std::string& method, const Json& params)
{
    if (listening == nullptr)
        throw RpcError (operationFailed, "listening isn't available in this engine");

    if (method == "listening.status")
        return listening->getStatus();

    if (method == "listening.download")
    {
        listening->download();
        return listening->getStatus();
    }

    if (method == "listening.index")
    {
        listening->startIndexing();
        return listening->getStatus();
    }

    if (method == "styles.list")
    {
        auto list = Json::array();
        const auto guides = guides::GuideStore (guides::GuideStore::getDefaultFolder()).list();

        for (auto& name : listening->getStyles().names())
            if (auto profile = listening->getStyles().get (name))
            {
                auto m = profile->measurements();
                list.push_back ({ { "name", profile->name }, { "folder", profile->folder.string() }, { "tracks", profile->tracks.size() },
                                  { "tempo", m.value ("tempo", Json()) }, { "guide", guideFor (guides, profile->name) } });
            }

        return { { "styles", list }, { "progress", listening->getStatus()["styles"] } };
    }

    // The references folder (one folder of tracks per style) and how each folder stands.
    if (method == "styles.references" || method == "styles.createReferences")
    {
        const auto root = referencesFolder.empty() ? listening::Styles::getReferencesFolder() : referencesFolder;

        if (method == "styles.createReferences")
            std::filesystem::create_directories (root);

        auto folders = Json::array();
        const auto guides = guides::GuideStore (guides::GuideStore::getDefaultFolder()).list();
        std::error_code ec;

        if (std::filesystem::is_directory (root, ec))
            for (auto& f : listening->getStyles().referenceFolders (root))
                folders.push_back ({ { "name", f.name }, { "path", f.path.string() }, { "files", f.files },
                                     { "style", f.style ? Json (*f.style) : Json() }, { "unheard", f.unheard }, { "gone", f.gone },
                                     { "guide", guideFor (guides, f.name) } });

        return { { "folder", root.string() }, { "exists", std::filesystem::is_directory (root, ec) }, { "folders", folders } };
    }

    if (method == "styles.add")
    {
        const auto name = listening->addStyle (readParam<std::string> (params, "path"), params.value ("name", ""));
        return { { "name", name }, { "progress", listening->getStatus()["styles"] } };
    }

    auto style = [&]
    {
        auto profile = listening->getStyles().get (readParam<std::string> (params, "name"));

        if (profile == nullptr)
            throw RpcError (invalidParams, "No style named \"" + params["name"].get<std::string>() + "\" (styles.list)");

        return profile;
    };

    if (method == "styles.describe")
        return listening->describeStyle (*style());

    if (method == "styles.compare")
        return compareToStyle (params);

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::callMaps (const std::string& method, const Json& params)
{
    if (listening == nullptr)
        throw RpcError (operationFailed, "listening isn't available in this engine");

    auto& styles = listening->getStyles();
    auto& maps = styles.getMaps();
    const auto root = referencesFolder.empty() ? listening::Styles::getReferencesFolder() : referencesFolder;

    auto ids = [&]
    {
        return readParam<std::vector<std::string>> (params, "ids");
    };

    // The songs asked for, as they stand.
    auto songsFor = [&] (const std::vector<std::string>& wanted)
    {
        std::vector<listening::Styles::Song> found;

        for (auto& song : styles.songs (root))
            if (std::find (wanted.begin(), wanted.end(), song.id) != wanted.end())
                found.push_back (song);

        return found;
    };

    auto songJson = [] (const listening::Styles::Song& s)
    {
        Json j { { "id", s.id }, { "style", s.style }, { "title", s.title }, { "status", s.status }, { "inUse", s.inUse } };

        if (s.file)
            j["file"] = s.file->string();

        if (! s.error.empty())
            j["error"] = s.error;

        if (s.map != nullptr)
            j["map"] = { { "source", s.map->source }, { "heardAt", s.map->heardAt }, { "analysis", s.map->track.analysis },
                         { "tempo", std::round (s.map->track.tempo * 10) / 10 }, { "lufs", std::round (s.map->track.lufs * 10) / 10 } };

        return j;
    };

    if (method == "maps.list")
    {
        auto list = Json::array();

        for (auto& song : styles.songs (root))
            list.push_back (songJson (song));

        return { { "folder", maps.getFolder().string() }, { "references", root.string() }, { "songs", list } };
    }

    if (method == "maps.status")
    {
        const auto p = styles.getProgress();
        auto item = [] (const listening::Styles::Progress::Item& i)
        {
            return Json { { "id", i.id }, { "style", i.style }, { "title", i.title } };
        };

        auto queued = Json::array(), recent = Json::array();

        for (auto& i : p.queued)
            queued.push_back (item (i));

        for (auto& d : p.recent)
        {
            auto j = item (d.item);
            j["ok"] = d.ok;
            j["seconds"] = std::round (d.seconds * 10) / 10;
            j["at"] = d.at;

            if (! d.error.empty())
                j["error"] = d.error;

            recent.push_back (j);
        }

        Json result { { "running", p.running }, { "paused", p.paused }, { "queued", queued }, { "recent", recent },
                      { "secondsPerSong", std::round (p.secondsPerSong * 10) / 10 }, { "ready", listening->getModels().getStatus().ready } };

        if (p.current)
        {
            result["current"] = item (*p.current);
            result["current"]["progress"] = p.currentProgress;
            result["current"]["started"] = p.currentStarted;
        }

        return result;
    }

    if (method == "maps.get")
    {
        const auto id = readParam<std::string> (params, "id");
        auto map = maps.get (id);

        if (map == nullptr)
            throw RpcError (invalidParams, "No map for \"" + id + "\" (maps.list)");

        // What was heard, without the sound fingerprints (only how many there are).
        auto j = map->toJson();
        auto& sounds = j["track"]["sounds"];

        for (auto& [role, list] : sounds.items())
            list = list.size();

        j["id"] = id;
        j["inUse"] = maps.isInUse (id);

        if (map->track.arrangement)
            j["score"] = map->track.arrangement->describe() + (map->track.figures.empty() ? "" : "\n" + listening::describeFigures (map->track.figures));

        j["current"] = map->track.analysis >= listening::currentAnalysisVersion();
        return j;
    }

    // The queue follows the ticks while songs are being heard: unticked ones leave it (the one
    // being heard stops and keeps its old map), ticked ones join it.
    if (method == "maps.setInUse")
    {
        const auto list = ids();
        const auto inUse = readParam<bool> (params, "inUse");
        maps.setInUse (list, inUse);

        if (! inUse)
        {
            styles.unqueue (list);
        }
        else if (const auto now = styles.getProgress(); now.current || ! now.queued.empty())
        {
            std::vector<std::pair<std::string, std::filesystem::path>> todo;

            for (auto& song : songsFor (list))
                if (song.file)
                    todo.emplace_back (song.style, *song.file);

            styles.hear (todo);
        }

        listening->notifyChanged();
        return { { "changed", params["ids"].size() } };
    }

    if (method == "maps.hear")
    {
        std::vector<std::pair<std::string, std::filesystem::path>> todo;

        for (auto& song : songsFor (ids()))
            if (song.file)
                todo.emplace_back (song.style, *song.file);

        styles.hear (todo);
        return { { "queued", todo.size() } };
    }

    if (method == "maps.hearNew")
    {
        const auto style = params.value ("style", std::string());
        std::vector<std::pair<std::string, std::filesystem::path>> todo;

        for (auto& song : styles.songs (root))
            if (song.file && (style.empty() || song.style == style) && (song.status == "new" || song.status == "changed" || song.status == "failed"))
                todo.emplace_back (song.style, *song.file);

        styles.hear (todo);
        return { { "queued", todo.size() } };
    }

    if (method == "maps.pause")     { styles.pause();       return { { "paused", true } }; }
    if (method == "maps.resume")    { styles.resume();      return { { "paused", false } }; }
    if (method == "maps.clearQueue") { styles.clearQueue(); return { { "cleared", true } }; }

    if (method == "maps.unqueue")
    {
        styles.unqueue (ids());
        return { { "unqueued", params["ids"].size() } };
    }

    if (method == "maps.forget")
    {
        styles.forget (ids());
        return { { "forgotten", params["ids"].size() } };
    }

    if (method == "maps.unskip")
    {
        maps.setSkipped (ids(), false);
        listening->notifyChanged();
        return { { "unskipped", params["ids"].size() } };
    }

    // Out of the references: the audio goes to "Not used" beside the style folders (nothing is
    // deleted), the map with it.
    if (method == "maps.removeReferences")
    {
        int moved = 0;
        std::error_code ec;

        for (auto& song : songsFor (ids()))
        {
            if (song.file)
            {
                const auto styleFolder = song.file->parent_path();
                const auto base = song.file->lexically_relative (root).string().starts_with ("..") ? styleFolder.parent_path() : root;
                const auto folder = base / "Not used" / song.style;
                std::filesystem::create_directories (folder);

                auto target = juce::File ((folder / song.file->filename()).string()).getNonexistentSibling (false);

                if (! juce::File (song.file->string()).moveFileTo (target))
                    throw RpcError (operationFailed, "Couldn't move " + song.file->string() + " to " + folder.string());

                ++moved;
            }

            maps.remove (song.id);
            maps.setSkipped ({ song.id }, false);
        }

        styles.unqueue (ids());
        return { { "moved", moved } };
    }

    if (method == "maps.export")
    {
        const std::filesystem::path zip = readParam<std::string> (params, "path");
        const auto count = maps.exportZip (ids(), zip);
        return { { "path", zip.string() }, { "count", count } };
    }

    if (method == "maps.import")
    {
        const auto result = maps.importZip (readParam<std::string> (params, "path"));
        listening->notifyChanged();
        return { { "added", result.added }, { "kept", result.kept } };
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

namespace
{
    /** A sample as it plays in a groove: bars at tempo, a one-shot on the pattern's steps, a loop
        stretched to the tempo (its pitch kept) and repeated. Peaks at -3 dB, so candidates are
        heard and measured at the same level. */
    juce::AudioBuffer<float> auditionOf (const library::SampleEntry& e, double tempo, std::string pattern, double rate, int bars)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (e.path)));

        if (reader == nullptr)
            throw std::runtime_error ("Can't read " + e.path);

        const auto count = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, static_cast<juce::int64> (30 * reader->sampleRate)));
        juce::AudioBuffer<float> raw (2, count);
        reader->read (&raw, 0, count, 0, true, true);

        std::vector<std::vector<float>> channels;

        for (int c = 0; c < 2; ++c)
        {
            juce::AudioBuffer<float> one (1, count);
            one.copyFrom (0, 0, raw, c, 0, count);
            channels.push_back (listening::resampleMono (one, reader->sampleRate, rate));
        }

        juce::AudioBuffer<float> sample (2, static_cast<int> (channels[0].size()));

        for (int c = 0; c < 2; ++c)
            sample.copyFrom (c, 0, channels[static_cast<size_t> (c)].data(), sample.getNumSamples());

        const double beat = 60 / tempo;
        const auto length = static_cast<int> (std::ceil (bars * 4 * beat * rate));
        juce::AudioBuffer<float> out (2, length);
        out.clear();

        if (e.classification.loop)
        {
            const auto loopTempo = e.classification.tempo ? e.classification.tempo : e.features.estimatedTempo;
            const auto stretched = loopTempo && *loopTempo > 0 ? stretchAudio (sample, rate, tempo / *loopTempo, 0) : sample;

            for (int at = 0; at < length && stretched.getNumSamples() > 0; at += stretched.getNumSamples())
                for (int c = 0; c < 2; ++c)
                    out.copyFrom (c, at, stretched, c, 0, std::min (stretched.getNumSamples(), length - at));
        }
        else
        {
            if (pattern.size() != 16)
                pattern = "x...x...x...x...";

            for (int bar = 0; bar < bars; ++bar)
                for (int step = 0; step < 16; ++step)
                {
                    if (pattern[static_cast<size_t> (step)] != 'x')
                        continue;

                    const auto at = static_cast<int> ((bar * 16 + step) * beat / 4 * rate);
                    const auto n = std::min (sample.getNumSamples(), length - at);

                    for (int c = 0; c < 2 && n > 0; ++c)
                        out.addFrom (c, at, sample, c, 0, n);
                }
        }

        if (const auto peak = out.getMagnitude (0, length); peak > 0)
            out.applyGain (0.708f / peak);

        return out;
    }

    void writeWav (const juce::AudioBuffer<float>& audio, double rate, const std::filesystem::path& file)
    {
        std::filesystem::create_directories (file.parent_path());
        juce::File out (file.string());
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.createOutputStream().release(), rate, 2, 24, {}, 0));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            throw std::runtime_error ("Couldn't write " + file.string());
    }

    /** What to change so a kick comes closer to the reference's, from how they differ. */
    Json kickSuggestions (const listening::KickSound& c, const listening::KickSound& r)
    {
        auto list = Json::array();
        std::ostringstream out;
        out.setf (std::ios::fixed);
        out.precision (1);

        auto add = [&] (const std::string& what, const std::string& how) { list.push_back ({ { "what", what }, { "how", how } }); };

        if (c.bodyHz > 0 && r.bodyHz > 0)
        {
            const auto st = 12 * std::log2 (c.bodyHz / r.bodyHz);

            if (std::abs (st) >= 0.5)
                add ("tune", "transpose the layer " + std::to_string (static_cast<int> (-std::lround (st))) + " semitones (fineTune for the rest) to bring the body to "
                     + listening::noteName (r.bodyHz));
        }

        const auto decay = c.decayMs - r.decayMs;

        if (decay > 30)
            add ("shorter body", "shorten it: lower the layer's tail, or length/fadeOut (" + std::to_string (static_cast<int> (decay)) + " ms too long)");
        else if (decay < -30)
            add ("longer body", "let it ring: raise the layer's tail, or pick a longer kick (" + std::to_string (static_cast<int> (-decay)) + " ms too short)");

        const auto click = c.clickDb - r.clickDb;

        if (click > 3)
            add ("less click", "lower the layer's punch or cut above 2 kHz with the eq");
        else if (click < -3)
            add ("more click", "raise the layer's punch or boost around 3-5 kHz with the eq");

        const auto grit = c.gritDb - r.gritDb;

        if (grit < -3)
            add ("more grit", "drive it: distortion or the exciter on its insert, then check the low end stays round");
        else if (grit > 3)
            add ("cleaner", "less drive: lower the distortion, or a cleaner kick");

        if (r.rumbleDb && ! c.rumbleDb && *r.rumbleDb > -24)
            add ("rumble", "make one from the kick: a send to a new insert with reverb (fully wet, long), distortion, a lowpass around 100-150 Hz and duck keyed by the kick, mixed about "
                           + std::to_string (static_cast<int> (-*r.rumbleDb)) + " dB under the hits");
        else if (r.rumbleDb && c.rumbleDb)
        {
            const auto rumble = *c.rumbleDb - *r.rumbleDb;

            if (std::abs (rumble) > 3)
                add (rumble > 0 ? "quieter rumble" : "louder rumble", std::string ("move the rumble's level ") + (rumble > 0 ? "down" : "up") + " about "
                     + std::to_string (static_cast<int> (std::abs (rumble))) + " dB");
        }

        if (c.spectrum.size() == r.spectrum.size() && ! c.spectrum.empty())
        {
            static const char* centres[] = { "40 Hz", "85 Hz", "170 Hz", "350 Hz", "700 Hz", "1.4 kHz", "3 kHz", "7 kHz" };
            size_t worst = 0;

            // The kick's own bands, as compareKicks weighs them.
            for (size_t i = 1; i < std::min<size_t> (c.spectrum.size(), 6); ++i)
                if (std::abs (c.spectrum[i] - r.spectrum[i]) > std::abs (c.spectrum[worst] - r.spectrum[worst]))
                    worst = i;

            const auto diff = c.spectrum[worst] - r.spectrum[worst];

            if (std::abs (diff) >= 3)
            {
                out.str ("");
                out << (diff > 0 ? "cut" : "boost") << " about " << std::abs (diff) << " dB around " << centres[std::min<size_t> (worst, 7)] << " with the eq";
                add ("eq", out.str());
            }
        }

        return list;
    }
}

// Sounds like a reference's: the library's kicks against a song map's (or a sample's), heard in a
// groove, put to the user, and the song's own kick measured against it.
Json ApiHandler::callMatch (const std::string& method, const Json& params)
{
    if (method.starts_with ("picks."))
    {
        if (method == "picks.offer")
        {
            const auto candidates = params.value ("candidates", Json::array());

            if (candidates.empty() || candidates.size() > 20)
                throw RpcError (invalidParams, "candidates: one to twenty");

            for (auto& c : candidates)
                if (! c.contains ("path") && ! c.contains ("instrument"))
                    throw RpcError (invalidParams, "candidates: each needs a path or an instrument");

            const auto id = ++offerCount;
            auto offer = params;
            offer["id"] = id;

            // A candidate tried in the song has the sound alone and the rest beside its audition:
            // the window fades the rest under it.
            for (auto& c : offer["candidates"])
            {
                if (! c.contains ("audition") || c.contains ("alone"))
                    continue;

                const std::filesystem::path audition = c["audition"].get<std::string>();
                auto sibling = [&] (const char* suffix) { return audition.parent_path() / (audition.stem().string() + suffix + ".wav"); };

                if (std::filesystem::exists (sibling ("-alone")) && std::filesystem::exists (sibling ("-rest")))
                {
                    c["alone"] = sibling ("-alone").string();
                    c["rest"] = sibling ("-rest").string();
                }
            }

            // The others are tried in the song now, so they play in time with it, from the same bar: in the
            // channel they're for, or in a phantom slot of their own (loops over the bars, one-shots and
            // instruments playing notes). An instrument can only be heard that way.
            const auto instruments = std::any_of (offer["candidates"].begin(), offer["candidates"].end(), [] (auto& c) { return c.contains ("instrument"); });
            auto steps = std::make_shared<Steps>();   // the tries, when there are any
            std::vector<size_t> tried;                 // which candidates they're for

            if (! session.getProject().playlist.clips.empty() || params.contains ("channel") || instruments)
            {
                auto tryThese = Json::array();
                std::vector<size_t> which;

                for (size_t n = 0; n < offer["candidates"].size(); ++n)
                {
                    const auto& c = offer["candidates"][n];

                    if (c.contains ("alone"))
                        continue;

                    Json candidate = Json::object();

                    for (auto* k : { "path", "instrument", "notes", "part" })
                        if (c.contains (k))
                            candidate[k] = c[k];

                    tryThese.push_back (candidate);
                    which.push_back (n);
                }

                if (! tryThese.empty())
                {
                    Json trial { { "candidates", tryThese } };

                    for (auto* k : { "channel", "start", "end", "notes", "part" })
                        if (params.contains (k))
                            trial[k] = params[k];

                    try
                    {
                        steps = auditionSteps (trial, true);
                        tried = which;
                    }
                    catch (const std::exception&)
                    {
                        // Heard on their own, then.
                    }
                }
            }

            auto* s = steps.get();

            steps->here ([this, s, offer = std::move (offer), id, tried] () mutable
            {
                if (! tried.empty() && s->result.contains ("candidates"))
                {
                    for (size_t k = 0; k < tried.size(); ++k)
                    {
                        const auto& t = s->result["candidates"][k];
                        auto& c = offer["candidates"][tried[k]];

                        if (! t.contains ("alone"))
                            continue;

                        c["alone"] = t["alone"];
                        c["rest"] = t["rest"];

                        if (! c.contains ("audition"))
                            c["audition"] = t["file"];
                    }
                }

                offers[id] = { offer, std::nullopt };

                // Answered offers aren't asked for again after a while: keep the last few dozen.
                while (offers.size() > 24 && offers.begin()->second.choice)
                    offers.erase (offers.begin());

                if (broadcast)
                    broadcast (notification ("picks", offer).dump());

                s->result = { { "id", id } };
            });

            deferred = steps;
            return {};
        }

        const auto id = static_cast<int> (optionalNumber (params, "id").value_or (0));
        auto it = offers.find (id);

        if (method == "picks.list")
        {
            auto waiting = Json::array();

            for (auto& [n, o] : offers)
                if (! o.choice)
                    waiting.push_back (o.offer);

            return { { "offers", waiting } };
        }

        if (it == offers.end())
            throw RpcError (invalidParams, "No offer " + std::to_string (id));

        if (method == "picks.get")
        {
            Json j { { "id", id }, { "offer", it->second.offer }, { "status", it->second.choice ? "chosen" : "waiting" } };

            if (it->second.choice)
                j["choice"] = *it->second.choice;

            if (! it->second.note.empty())
                j["note"] = it->second.note;

            if (it->second.more)
                j["more"] = true;

            return j;
        }

        if (method == "picks.choose")
        {
            const auto choice = static_cast<int> (optionalNumber (params, "choice").value_or (-1));
            const auto& candidates = it->second.offer["candidates"];
            const auto note = juce::String (params.value ("note", std::string())).trim().substring (0, 2000).toStdString();
            // "Another round": none of these, and the user wants to hear as many again.
            const bool more = params.value ("more", false);

            if (choice >= static_cast<int> (candidates.size()))
                throw RpcError (invalidParams, "choice: an index into the candidates, or -1 for none of them");

            if (! note.empty() && choice >= 0)
                throw RpcError (invalidParams, "note: only with choice -1 (Other)");

            if (more && (choice >= 0 || ! note.empty()))
                throw RpcError (invalidParams, "more: only with choice -1 and no note");

            it->second.choice = choice;
            it->second.note = note;
            it->second.more = more;

            // The pick fits the style; with none picked, none of them does. Passed over isn't a verdict, and
            // neither is "Other": the user wants another kind of sound, which says nothing about these.
            if (listening != nullptr && note.empty())
            {
                const auto style = it->second.offer.value ("style", std::string());

                for (int i = 0; i < static_cast<int> (candidates.size()); ++i)
                {
                    const auto path = candidates[static_cast<size_t> (i)].value ("path", std::string());

                    if (path.empty())
                        continue;       // an instrument: no sample to rate

                    const auto known = library != nullptr ? library->find (path) : std::nullopt;
                    const auto key = known ? known->path : path;

                    if (choice == i)
                        listening->getTaste().rate (key, style, true);
                    else if (choice < 0)
                        listening->getTaste().rate (key, style, false);
                }
            }

            Json chosen { { "id", id }, { "choice", choice } };

            if (! note.empty())
                chosen["note"] = note;

            if (more)
                chosen["more"] = true;

            if (broadcast)
                broadcast (notification ("picks", chosen).dump());
            return chosen;
        }

        throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
    }

    if (listening == nullptr || library == nullptr)
        throw RpcError (operationFailed, "listening isn't available in this engine");

    // The reference: a song map's kick, or a sample's.
    listening::KickSound reference;
    Json about;
    std::shared_ptr<const listening::SongMap> map;

    if (params.contains ("id"))
    {
        const auto id = readParam<std::string> (params, "id");
        map = listening->getStyles().getMaps().get (id);

        if (map == nullptr)
            throw RpcError (invalidParams, "No map for \"" + id + "\" (song_maps list)");

        if (! map->track.kick)
            throw RpcError (operationFailed, "\"" + id + "\" was heard before kicks were measured: hear it again (song_maps hear)");

        const auto& k = *map->track.kick;
        reference = k.sound;
        about = { { "id", id }, { "title", map->title }, { "style", map->style }, { "bars", { k.from + 1, k.to } }, { "clean", k.clean },
                  { "kick", reference.describe() }, { "tempo", map->track.tempo } };

        if (map->track.arrangement)
        {
            const auto& a = *map->track.arrangement;
            about["from"] = a.downbeat + k.from * a.barSeconds();
            about["to"] = a.downbeat + k.to * a.barSeconds();
        }

        if (std::filesystem::exists (map->track.path))
            about["file"] = map->track.path;
    }
    else
    {
        const auto path = readParam<std::string> (params, "path");
        auto entry = library->find (path);

        if (! entry)
            throw RpcError (invalidParams, "Not in the library: " + path);

        auto sound = listening::KickIndex::measureSample (*entry);

        if (! sound)
            throw RpcError (operationFailed, "No kick heard in " + path);

        reference = *sound;
        about = { { "path", entry->path }, { "kick", reference.describe() } };
    }

    if (method == "match.kicks")
    {
        const auto limit = std::clamp (static_cast<int> (optionalNumber (params, "limit").value_or (20)), 1, 100);
        std::optional<bool> loops;

        if (params.contains ("loops") && ! params["loops"].is_null())
            loops = readParam<bool> (params, "loops");

        // The reference's own fingerprints, for loops and for single hits.
        std::vector<listening::Embedding> loopPrints, hitPrints;

        if (map != nullptr)
        {
            if (auto it = map->track.sounds.find (listening::Role::kick); it != map->track.sounds.end())
                loopPrints = it->second;

            if (auto it = map->track.sounds.find (listening::Role::kickHit); it != map->track.sounds.end())
                hitPrints = it->second;
        }

        struct Found { std::string path; listening::KickSound sound; listening::KickComparison comparison; std::optional<float> print; float score; };
        std::vector<Found> found;
        int searched = 0;
        auto& index = listening->getIndex();

        listening->getKicks().forEach ([&] (const std::string& path, const listening::KickSound& sound)
        {
            // The kick index holds library keys: no canonicalising per kick.
            const auto entry = library->findByKey (path);

            if (! entry || (loops && entry->classification.loop != *loops))
                return;

            ++searched;
            auto comparison = listening::compareKicks (sound, reference);
            std::optional<float> print;
            const auto& prints = entry->classification.loop ? loopPrints : hitPrints;

            if (! prints.empty())
                if (auto e = index.find (path))
                {
                    float best = -1;

                    for (auto& p : prints)
                        best = std::max (best, listening::similarity (*e, p));

                    print = std::clamp ((best - 0.3f) / 0.5f, 0.0f, 1.0f) * 100;
                }

            const auto score = print ? 0.75f * comparison.score + 0.25f * *print : comparison.score;
            found.push_back ({ path, sound, std::move (comparison), print, score });
        });

        std::sort (found.begin(), found.end(), [] (auto& a, auto& b) { return a.score > b.score; });
        auto results = Json::array();
        std::set<std::string> seen;

        for (size_t i = 0; i < found.size() && static_cast<int> (results.size()) < limit; ++i)
        {
            const auto& f = found[i];
            const auto entry = library->findByKey (f.path);

            // The same sound in several folders (or under another name), once.
            if (! seen.insert (std::filesystem::path (f.path).filename().string() + "@" + std::to_string (entry->size)).second
                || ! seen.insert (f.sound.toJson().dump()).second)
                continue;

            Json j { { "path", f.path }, { "name", std::filesystem::path (f.path).filename().string() }, { "category", entry->classification.category },
                     { "loop", entry->classification.loop }, { "score", std::round (f.score) }, { "measured", std::round (f.comparison.score) },
                     { "differences", f.comparison.notes }, { "kick", f.sound.describe() } };

            if (f.print)
                j["fingerprint"] = std::round (*f.print);

            if (entry->classification.tempo)
                j["tempo"] = *entry->classification.tempo;

            results.push_back (std::move (j));
        }

        const auto progress = listening->getKicks().getProgress();
        return { { "reference", about }, { "results", results }, { "searched", searched }, { "measuring", progress.running } };
    }

    if (method == "match.audition")
    {
        const auto paths = readParam<std::vector<std::string>> (params, "paths");
        const auto tempo = optionalNumber (params, "tempo").value_or (map != nullptr && map->track.tempo > 0 ? map->track.tempo : session.getProject().tempo);
        const auto bars = std::clamp (static_cast<int> (optionalNumber (params, "bars").value_or (4)), 1, 16);
        constexpr double rate = 44100;

        if (paths.empty() || paths.size() > 20)
            throw RpcError (invalidParams, "paths: one to ten samples");

        listening::KickTiming timing;
        timing.beat = 60 / tempo;
        timing.downbeat = 0.0;
        auto results = Json::array();
        const auto folder = getDefaultCacheDir() / "auditions";

        for (auto& path : paths)
        {
            const auto entry = library->find (path);

            if (! entry)
            {
                results.push_back ({ { "path", path }, { "error", "not in the library" } });
                continue;
            }

            const auto audio = auditionOf (*entry, tempo, reference.pattern, rate, bars);
            const auto file = folder / (std::to_string (std::hash<std::string> {} (entry->path + "@" + std::to_string (tempo) + reference.pattern + std::to_string (bars))) + ".wav");
            writeWav (audio, rate, file);
            Json j { { "path", entry->path }, { "name", std::filesystem::path (entry->path).filename().string() }, { "audition", file.string() } };

            if (auto heard = listening::measureKick (audio, rate, timing))
            {
                const auto comparison = listening::compareKicks (*heard, reference);
                j["score"] = std::round (comparison.score);
                j["differences"] = comparison.notes;
                j["kick"] = heard->describe();
                j["changes"] = kickSuggestions (*heard, reference);
            }
            else
            {
                j["error"] = "no kick heard in it";
            }

            results.push_back (std::move (j));
        }

        return { { "reference", about }, { "tempo", tempo }, { "results", results } };
    }

    if (method == "match.channel")
    {
        const auto channel = readParam<std::string> (params, "channel");
        auto project = session.getProject();

        if (model::findChannel (project, channel) == nullptr)
            throw RpcError (invalidParams, "No channel \"" + channel + "\"");

        for (auto& c : project.channels)
            c.solo = c.id == channel;

        for (auto& i : project.mixer.inserts)
            i.solo = false;

        for (auto& r : project.playlist.rows)
            r.solo = false;

        constexpr model::Ticks bar = 3840;
        RenderOptions options;
        options.sampleRate = 44100;
        options.start = static_cast<model::Ticks> (optionalNumber (params, "start").value_or (0));
        options.end = static_cast<model::Ticks> (optionalNumber (params, "end").value_or (static_cast<double> (*options.start + 8 * bar)));

        const auto projectDir = projectDirOf (session);
        const auto file = projectDir / "renders" / "listening" / ("match-" + channel + ".wav");
        std::filesystem::create_directories (file.parent_path());
        auto compiled = compileProject (context.getEngine(), project, { projectDir, tracktion::Edit::forRendering });
        renderToWav (compiled, project, file, options);

        auto audio = analysis::loadAudio (file);
        listening::KickTiming timing;
        timing.beat = 60 / project.tempo;
        timing.downbeat = 0.0;
        auto heard = listening::measureKick (audio.buffer, audio.sampleRate, timing);

        if (! heard)
            throw RpcError (operationFailed, "No kick heard from \"" + channel + "\" there: give start and end (ticks) where it plays");

        const auto comparison = listening::compareKicks (*heard, reference);
        Json result = comparison.toJson();
        result["reference"] = about;
        result["kick"] = heard->describe();
        result["changes"] = kickSuggestions (*heard, reference);
        return result;
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::compareToStyle (const Json& params)
{
    auto profile = listening->getStyles().get (readParam<std::string> (params, "name"));

    if (profile == nullptr)
        throw RpcError (invalidParams, "No style named \"" + params["name"].get<std::string>() + "\" (styles.list)");

    if (! listening->getModels().getStatus().ready)
        throw RpcError (operationFailed, "The listening models aren't downloaded yet (listening.download)");

    RenderOptions options;

    if (auto v = optionalNumber (params, "start"))  options.start = static_cast<model::Ticks> (*v);
    if (auto v = optionalNumber (params, "end"))    options.end = static_cast<model::Ticks> (*v);

    const auto& project = session.getProject();
    const auto projectDir = projectDirOf (session);
    const auto workDir = projectDir / "renders" / "listening";
    std::filesystem::create_directories (workDir);

    // Which part each channel mostly plays, from its first sample's category: a hint for acting
    // on the comparison, which hears the mix itself split into parts.
    auto partOf = [&] (const model::Channel& c)
    {
        if (auto* sampler = std::get_if<model::SamplerInstrument> (&c.instrument); sampler != nullptr && ! sampler->layers.empty())
        {
            std::filesystem::path sample = sampler->layers.front().sample;

            if (sample.is_relative())
                sample = projectDir / sample;

            std::string category = "other";

            try { category = entryFor (sample).classification.category; } catch (const std::exception&) {}

            const auto role = listening::roleOf (category, true);
            return role == listening::Role::kick || role == listening::Role::hats ? listening::Role::drums : role;
        }

        return juce::String (c.name).containsIgnoreCase ("bass") ? listening::Role::bass : listening::Role::music;
    };

    std::map<listening::Role, std::vector<std::string>> parts;

    for (auto& c : project.channels)
        if (! c.mute)
            parts[partOf (c)].push_back (c.id);

    auto partsJson = Json::object();

    for (auto& [role, channels] : parts)
        partsJson[listening::roleName (role)] = channels;

    struct Run
    {
        std::optional<CompiledEdit> compiled;
        model::Project project;
        std::filesystem::path file;
    };

    auto run = std::make_shared<Run>();
    run->project = project;
    run->file = workDir / "mix.wav";
    run->compiled.emplace (compileProject (context.getEngine(), project, { projectDir, tracktion::Edit::forRendering }));

    auto steps = std::make_shared<Steps>();
    auto* result = &steps->result;

    steps->pooled ([run, options, profile, partsJson, result, listening = listening]
    {
        struct Cleanup { std::filesystem::path file; ~Cleanup() { std::error_code e; std::filesystem::remove (file, e); } } cleanup { run->file };
        renderToWav (*run->compiled, run->project, run->file, options);
        *result = listening->compare (*profile, run->file, run->project.tempo, run->project.swing);
        (*result)["channels"] = partsJson;
    });

    steps->here ([run] { run->compiled.reset(); });
    deferred = steps;
    return {};
}

} // namespace gk
