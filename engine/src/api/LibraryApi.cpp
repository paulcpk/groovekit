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

Json ApiHandler::callLibrary (const std::string& method, const Json& params)
{
    if (library == nullptr)
        throw RpcError (operationFailed, "no sample library is configured");

    auto progressJson = [this]
    {
        auto p = library->getProgress();
        return Json { { "running", p.running }, { "filesFound", p.filesFound }, { "analysed", p.analysed },
                      { "unchanged", p.unchanged }, { "failed", p.failed }, { "samples", library->size() } };
    };

    auto folders = [this]
    {
        auto list = Json::array();

        for (auto& f : library->getFolders())
            list.push_back (f.string());

        return list;
    };

    auto status = [&] { return Json { { "folders", folders() }, { "progress", progressJson() }, { "autoScan", library->getAutoScan() } }; };

    if (method == "folders" || method == "status")
        return status();

    if (method == "starter" || method == "downloadStarter")
    {
        if (starter == nullptr)
            throw RpcError (operationFailed, "the starter samples aren't available in this engine");

        if (method == "downloadStarter")
            starter->download();

        return starterJson (starter->getStatus());
    }

    if (method == "setAutoScan")
    {
        library->setAutoScan (readParam<bool> (params, "enabled"));
        return status();
    }

    if (method == "addFolder")
    {
        library->addFolder (readParam<std::string> (params, "path"));

        if (params.value ("scan", true))
            startLibraryScan();

        return status();
    }

    if (method == "removeFolder")
    {
        library->removeFolder (readParam<std::string> (params, "path"));
        return status();
    }

    if (method == "scan")
    {
        startLibraryScan();
        return progressJson();
    }

    if (method == "search")
    {
        library::SampleQuery q;
        q.text = params.value ("query", "");
        q.limit = std::clamp (params.value ("limit", 20), 1, 200);

        if (params.contains ("category"))       q.category = readParam<std::string> (params, "category");
        if (params.contains ("loop"))           q.loop = readParam<bool> (params, "loop");
        if (params.contains ("key"))            q.key = readParam<std::string> (params, "key");

        // "song" is the song's key.
        if (auto fits = params.value ("fitsKey", std::string()); ! fits.empty())
        {
            const auto name = fits == "song" ? session.getProject().key.value_or ("") : fits;
            q.fitsKey = music::parseKey (name);

            if (! q.fitsKey)
                throw RpcError (invalidParams, fits == "song" ? "The song has no key yet (setKey)" : "\"" + fits + "\" isn't a key; name one like \"A minor\"");
        }

        if (auto v = optionalNumber (params, "tempo"))          q.tempo = v;
        if (auto v = optionalNumber (params, "minDuration"))    q.minDuration = v;
        if (auto v = optionalNumber (params, "maxDuration"))    q.maxDuration = v;

        if (listening != nullptr)
        {
            listening::Listening::SoundQuery sound;
            sound.sound = params.value ("sound", "");
            sound.like = params.value ("like", "");
            sound.style = params.value ("style", "");

            if (params.contains ("role"))
            {
                sound.role = listening::roleFromName (readParam<std::string> (params, "role"));

                if (! sound.role)
                    throw RpcError (invalidParams, "role must be kick, hats, drums, bass, music, kickHit, hatHit or percHit");
            }

            return listening->search (q, sound);
        }

        auto results = Json::array();

        for (auto& e : library->search (q))
            results.push_back (library::SampleLibrary::toJson (e, false, q.fitsKey));

        return { { "results", results }, { "librarySize", library->size() } };
    }

    if (method == "list")
    {
        const auto listing = library->list (params.contains ("path") ? readParam<std::string> (params, "path") : std::string());
        auto folders = Json::array(), samples = Json::array();

        for (auto& f : listing.folders)
            folders.push_back ({ { "path", f.path.string() }, { "name", f.path.filename().string() }, { "samples", f.samples } });

        for (auto& e : listing.samples)
            samples.push_back (library::SampleLibrary::toJson (e, false));

        return { { "folders", folders }, { "samples", samples } };
    }

    if (method == "describe")
    {
        // A path relative to the project folder works, like in the song; so does a bare file
        // name that one library sample has.
        const std::filesystem::path requested = readParam<std::string> (params, "path");
        auto path = requested;
        std::error_code ec;

        if (path.is_relative())
            if (auto dir = session.getProjectDir())
                path = *dir / path;

        if (! std::filesystem::is_regular_file (path, ec))
        {
            library::SampleQuery query;
            query.text = requested.stem().string();
            query.limit = 200;
            std::vector<std::filesystem::path> named;

            for (auto& e : library->search (query))
                if (std::filesystem::path (e.path).filename() == requested.filename())
                    named.push_back (e.path);

            if (named.size() != 1)
                throw RpcError (invalidParams, "No file at " + path.string()
                                + (named.empty() ? "; pass the sample's full path, as get_summary or search_samples give it"
                                                 : "; several library samples are named " + requested.filename().string() + ", pass the full path"));

            path = named.front();
        }

        auto result = library::SampleLibrary::toJson (entryFor (path), true);

        // What it sounds like in words, and how it fits a style, once GrooveKit can listen.
        if (listening != nullptr && listening->getModels().getStatus().ready && listening->getIndex().size() > 0)
        {
            const auto embedding = listening->getIndex().embedFile (path);
            result["sounds"] = listening->describe (embedding, 5);

            if (params.contains ("style"))
            {
                auto profile = listening->getStyles().get (readParam<std::string> (params, "style"));

                if (profile == nullptr)
                    throw RpcError (invalidParams, "No style named \"" + params["style"].get<std::string>() + "\" (styles.list)");

                const auto role = listening::roleOf (result.value ("category", "other"), result.value ("loop", false));
                result["fit"] = std::lround (listening->fitPercent (*profile, role, embedding));
            }
        }

        return result;
    }

    if (method == "rate")
    {
        if (listening == nullptr)
            throw RpcError (operationFailed, "listening isn't available");

        const auto path = readParam<std::string> (params, "path");
        const auto known = library->find (path);
        const auto key = known ? known->path : path;
        std::optional<bool> fits;

        if (params.contains ("fits") && ! params["fits"].is_null())
            fits = readParam<bool> (params, "fits");

        listening->getTaste().rate (key, params.value ("style", ""), fits);
        return { { "path", key }, { "style", params.value ("style", "") }, { "fits", fits ? Json (*fits) : Json() } };
    }

    if (method == "ratings")
    {
        auto list = Json::array();

        if (listening != nullptr)
            for (auto& r : listening->getTaste().all())
                list.push_back ({ { "path", r.path }, { "style", r.style }, { "fits", r.fits } });

        return { { "ratings", list } };
    }

    throw RpcError (methodNotFound, "unknown method \"library." + method + "\"");
}

library::SampleEntry ApiHandler::entryFor (const std::filesystem::path& path)
{
    if (library != nullptr)
        if (auto e = library->find (path))
            return *e;

    // Analysed once per file as it is: decoding and classifying take a good part of a second,
    // and the mix methods ask about every channel on every call.
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time (path, ec);
    const auto key = path.string();

    if (auto it = entryCache.find (key); ! ec && it != entryCache.end() && it->second.first == modified)
        return it->second.second;

    auto entry = library::SampleLibrary::analyseFile (path);

    if (! ec)
        entryCache[key] = { modified, entry };

    return entry;
}

void ApiHandler::startLibraryScan()
{
    library->startScan ([this, alive = alive] (const library::SampleLibrary::Progress& p)
    {
        Json params { { "running", p.running }, { "filesFound", p.filesFound }, { "analysed", p.analysed },
                      { "unchanged", p.unchanged }, { "failed", p.failed } };

        // Called on the scan thread: broadcast from the message thread. New samples are then heard.
        juce::MessageManager::callAsync ([this, alive, params]
        {
            if (! *alive)
                return;

            if (broadcast)
                broadcast (notification ("libraryProgress", params).dump());

            if (listening != nullptr && ! params.value ("running", true))
                listening->startIndexing();
        });
    });
}

} // namespace gk
