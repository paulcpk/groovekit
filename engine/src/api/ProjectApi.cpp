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

namespace
{
    std::error_code copyProjectFolder (const std::filesystem::path& from, const std::filesystem::path& to)
    {
        std::error_code ec;
        std::filesystem::create_directories (to, ec);

        for (std::filesystem::recursive_directory_iterator it (from, ec), end; ! ec && it != end; it.increment (ec))
        {
            const auto relative = it->path().lexically_relative (from);

            if (relative.string() == "renders" || it->path().filename().string().ends_with (".render.wav"))
            {
                it.disable_recursion_pending();
                continue;
            }

            if (it->is_directory (ec))
                std::filesystem::create_directories (to / relative, ec);
            else if (! ec)
                std::filesystem::copy_file (it->path(), to / relative, std::filesystem::copy_options::skip_existing, ec);
        }

        return ec;
    }
}

Json ApiHandler::callProject (const std::string& method, const Json& params)
{
    const auto recents = recentFile.empty() ? EngineContext::getAppDataFolder() / "recent.json" : recentFile;

    if (method == "project.recent")
    {
        auto list = Json::array();

        for (auto& dir : recent::load (recents))
        {
            const juce::File song ((dir / "song.json").string());
            list.push_back ({ { "path", dir.string() }, { "name", dir.filename().string() },
                              { "modified", song.getLastModificationTime().toMilliseconds() } });
        }

        Json result { { "projects", list } };

        if (auto dir = session.getProjectDir())
            result["current"] = dir->string();

        return result;
    }

    // The projects in a folder (those with a song.json): the open project's neighbours unless one is named.
    if (method == "project.browse")
    {
        std::filesystem::path folder;

        if (params.contains ("folder"))
            folder = params["folder"].get<std::string>();
        else if (auto dir = session.getProjectDir())
            folder = dir->parent_path();
        else
            folder = std::filesystem::path (juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName().toStdString()) / "GrooveKit";

        auto list = Json::array();
        std::error_code ec;
        std::vector<std::filesystem::directory_entry> entries;

        for (auto& entry : std::filesystem::directory_iterator (folder, ec))
            if (entry.is_directory (ec) && std::filesystem::exists (entry.path() / "song.json", ec))
                entries.push_back (entry);

        std::sort (entries.begin(), entries.end(), [] (auto& a, auto& b) { return a.path().filename() < b.path().filename(); });

        for (auto& entry : entries)
        {
            const juce::File song ((entry.path() / "song.json").string());
            list.push_back ({ { "path", entry.path().string() }, { "name", entry.path().filename().string() }, { "modified", song.getLastModificationTime().toMilliseconds() } });
        }

        Json result { { "folder", folder.string() }, { "projects", list } };

        if (auto dir = session.getProjectDir())
            result["current"] = dir->string();

        return result;
    }

    if (method == "project.importFile" || method == "project.importData" || method == "project.collectSamples")
        return importIntoProject (method, params);

    // Every change is written as it's made; saving marks where the song was saved, which
    // closing without saving goes back to.
    if (method == "project.save" || method == "project.revert")
    {
        if (method == "project.save")
            session.markSaved();
        else
            session.revertToSaved();

        Json state { { "saved", headJson (session.getSavedHead()) }, { "head", headJson (session.getHead()) } };

        if (broadcast)
            broadcast (notification ("saved", state).dump());

        return state;
    }

    if (method != "project.open" && method != "project.new" && method != "project.saveAs")
        throw RpcError (methodNotFound, "unknown method \"" + method + "\"");

    if (! onOpenProject)
        throw RpcError (operationFailed, "this engine can't switch projects");

    const auto dir = std::filesystem::absolute (readParam<std::string> (params, "path")).lexically_normal();
    std::error_code ec;

    if (method == "project.open")
    {
        if (! recent::isProject (dir))
            throw RpcError (invalidParams, dir.string() + " isn't a GrooveKit project (it has no song.json)");
    }
    else
    {
        if (recent::isProject (dir))
            throw RpcError (invalidParams, dir.string() + (method == "project.new" ? " is already a project; open it instead"
                                                                                   : " is already a project; choose a new folder"));

        if (std::filesystem::exists (dir, ec) && ! std::filesystem::is_empty (dir, ec))
            throw RpcError (invalidParams, dir.string() + " isn't empty; choose a new folder for the project");

        // A file system that's out of reach shows up here rather than after the switch.
        if (! std::filesystem::create_directories (dir, ec) && ec)
            throw RpcError (operationFailed, "Can't create " + dir.string() + ": " + ec.message());

        // Save as: the folder (song, history, recordings, plugin states) goes along, so paths
        // relative to the project still work, less the renders; then the copy is opened.
        if (method == "project.saveAs")
        {
            const auto current = session.getProjectDir();

            if (! current)
                throw RpcError (operationFailed, "this session has no project folder to copy");

            // The copy takes the song as it is now, not as it was last written.
            session.flushSave (true);
            ec = copyProjectFolder (*current, dir);

            if (ec)
                throw RpcError (operationFailed, "Can't copy the project to " + dir.string() + ": " + ec.message());

            // The copy is the song as it is now, saved.
            session.markCopySaved (dir);
        }
    }

    if (auto current = session.getProjectDir(); current && std::filesystem::weakly_canonical (*current) == std::filesystem::weakly_canonical (dir))
        return { { "project", dir.string() } };

    onOpenProject (dir);
    return { { "project", dir.string() } };
}

Json ApiHandler::importIntoProject (const std::string& method, const Json& params)
{
    const auto projectDir = session.getProjectDir();

    if (! projectDir)
        throw RpcError (operationFailed, "this session has no project folder");

    const auto samplesDir = *projectDir / "samples";
    std::filesystem::create_directories (samplesDir);

    // A free name in samples/ ("kick.wav", "kick-2.wav", ...), or the same file if it's already there.
    auto destinationFor = [&] (const std::string& fileName, std::uintmax_t size) -> std::pair<std::filesystem::path, bool>
    {
        const auto name = std::filesystem::path (fileName);
        auto target = samplesDir / name.filename();
        std::error_code ec;

        for (int n = 2; std::filesystem::exists (target); ++n)
        {
            if (std::filesystem::file_size (target, ec) == size)
                return { target, true };

            target = samplesDir / (name.stem().string() + "-" + std::to_string (n) + name.extension().string());
        }

        return { target, false };
    };

    auto relative = [&] (const std::filesystem::path& p) { return p.lexically_relative (*projectDir).generic_string(); };

    auto importFile = [&] (const std::filesystem::path& source)
    {
        std::error_code ec;

        if (! std::filesystem::is_regular_file (source, ec))
            throw RpcError (invalidParams, "No file at " + source.string());

        auto [target, exists] = destinationFor (source.filename().string(), std::filesystem::file_size (source, ec));

        if (! exists)
            std::filesystem::copy_file (source, target, ec);

        if (ec)
            throw RpcError (operationFailed, "Can't copy " + source.string() + ": " + ec.message());

        return relative (target);
    };

    if (method == "project.importFile")
        return { { "path", importFile (readParam<std::string> (params, "path")) } };

    if (method == "project.importData")
    {
        juce::MemoryOutputStream decoded;

        if (! juce::Base64::convertFromBase64 (decoded, readParam<std::string> (params, "data")))
            throw RpcError (invalidParams, "data isn't base64");

        auto [target, exists] = destinationFor (readParam<std::string> (params, "name"), decoded.getDataSize());

        if (! exists && ! juce::File (target.string()).replaceWithData (decoded.getData(), decoded.getDataSize()))
            throw RpcError (operationFailed, "Can't write " + target.string());

        return { { "path", relative (target) } };
    }

    // collectSamples: every sample outside the project folder is copied in, and the song uses the copies.
    std::vector<model::Command> commands;
    std::map<std::string, std::string> copied;
    const auto& project = session.getProject();

    auto collect = [&] (const std::string& sample) -> std::optional<std::string>
    {
        std::filesystem::path p (sample);

        if (p.is_relative())
            return {};

        if (auto it = copied.find (sample); it != copied.end())
            return it->second;

        if (! std::filesystem::exists (p))
            return {};

        return copied[sample] = importFile (p);
    };

    for (auto& channel : project.channels)
        if (auto sampler = std::get_if<model::SamplerInstrument> (&channel.instrument))
            for (auto& layer : sampler->layers)
                if (auto path = collect (layer.sample))
                    commands.push_back (model::UpdateSamplerLayer { channel.id, layer.id, Json { { "sample", *path } } });

    if (! commands.empty())
        session.execute (commands, { "ui", {}, "Collect samples into the project" });

    return { { "copied", copied.size() }, { "updated", commands.size() } };
}

Json ApiHandler::callChats (const std::string& method, const Json& params)
{
    ChatStore store (chatsFolder.empty() ? ChatStore::getDefaultFolder() : chatsFolder);

    auto summaryJson = [] (const ChatStore::Summary& c)
    {
        return Json { { "id", c.id }, { "title", c.title }, { "project", c.project }, { "projectName", c.projectName },
                      { "created", c.created }, { "updated", c.updated }, { "messages", c.messages } };
    };

    if (method == "chats.list")
    {
        auto list = Json::array();

        for (auto& c : store.list())
            list.push_back (summaryJson (c));

        return { { "chats", list } };
    }

    if (method == "chats.get")
    {
        if (auto chat = store.get (readParam<std::string> (params, "id")))
            return *chat;

        throw RpcError (invalidParams, "No chat " + params["id"].dump());
    }

    if (method == "chats.save")
    {
        if (! params.contains ("chat") || ! params["chat"].is_object())
            throw RpcError (invalidParams, "chat must be an object with an id");

        return summaryJson (store.save (params["chat"]));
    }

    if (method == "chats.delete")
        return { { "deleted", store.remove (readParam<std::string> (params, "id")) } };

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::callNotes (const std::string& method, const Json& params)
{
    AgentNotes notes (notesFile.empty() ? AgentNotes::getDefaultFile() : notesFile);
    const auto here = session.getProjectDir() ? session.getProjectDir()->string() : std::string();

    // A note's song: this one ("song"), or every song ("all").
    auto scope = [&] () -> std::optional<std::pair<std::string, std::string>>
    {
        if (! params.contains ("scope"))
            return {};

        const auto value = readParam<std::string> (params, "scope");

        if (value == "all")
            return std::pair<std::string, std::string>();

        if (value != "song")
            throw RpcError (invalidParams, "scope must be \"all\" or \"song\"");

        if (here.empty())
            throw RpcError (operationFailed, "No project is open to keep a note for");

        auto name = session.getProject().name;
        return std::pair (here, name.empty() ? session.getProjectDir()->filename().string() : name);
    };

    // here: whether the note applies to the open song (and so to the agent working on it).
    auto noteJson = [&] (const AgentNotes::Note& n)
    {
        auto j = toJson (n);
        j["here"] = n.project.empty() || n.project == here;
        return j;
    };

    auto listJson = [&]
    {
        auto list = Json::array();

        for (auto& n : notes.list())
            list.push_back (noteJson (n));

        return Json { { "notes", list } };
    };

    auto changed = [&] (Json result)
    {
        if (broadcast)
            broadcast (notification ("notes", listJson()).dump());

        return result;
    };

    if (method == "notes.list")
        return listJson();

    if (method == "notes.add")
    {
        const auto where = scope();
        return changed (noteJson (notes.add (readParam<std::string> (params, "text"), where ? where->first : "", where ? where->second : "")));
    }

    if (method == "notes.update")
    {
        AgentNotes::Change change;

        if (params.contains ("text"))       change.text = readParam<std::string> (params, "text");
        if (params.contains ("enabled"))    change.enabled = readParam<bool> (params, "enabled");
        change.project = scope();

        if (auto note = notes.update (readParam<std::string> (params, "id"), change))
            return changed (noteJson (*note));

        throw RpcError (invalidParams, "No note " + params["id"].dump() + " (notes.list)");
    }

    if (method == "notes.delete")
    {
        if (notes.remove (readParam<std::string> (params, "id")))
            return changed ({ { "deleted", true } });

        return { { "deleted", false } };
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::guideFor (const std::vector<guides::Guide>& list, const std::string& styleName)
{
    if (auto* guide = guides::GuideStore::findIn (list, styleName))
        return guide->id;

    return {};
}

Json ApiHandler::callGuides (const std::string& method, const Json& params)
{
    guides::GuideStore store (guides::GuideStore::getDefaultFolder());
    const auto style = session.getProject().style;

    auto listJson = [&]
    {
        auto list = Json::array();

        for (auto& g : store.list())
        {
            auto j = guides::toJson (g, false);
            j["song"] = style && *style == g.id;
            list.push_back (j);
        }

        return Json { { "guides", list }, { "style", style ? Json (*style) : Json() } };
    };

    auto changed = [&] (Json result)
    {
        if (broadcast)
            broadcast (notification ("guides", listJson()).dump());

        return result;
    };

    if (method == "guides.list")
        return listJson();

    // One guide in full: by id or name, or the song's.
    if (method == "guides.get")
    {
        const auto wanted = params.contains ("id") ? readParam<std::string> (params, "id") : style.value_or ("");

        if (wanted.empty())
            throw RpcError (invalidParams, "The song has no style yet; name a guide (guides.list) or set one (setStyle)");

        if (auto guide = store.find (wanted))
            return guides::toJson (*guide, true);

        throw RpcError (invalidParams, "No style guide \"" + wanted + "\" (guides.list)");
    }

    if (method == "guides.save")
        return changed (guides::toJson (store.save (params.value ("id", std::string()), readParam<std::string> (params, "text")), true));

    // Out of the list: one of the user's own is deleted, a built-in one hidden (unhide brings
    // it back).
    if (method == "guides.delete")
    {
        const auto id = readParam<std::string> (params, "id");
        auto guide = store.find (id);

        if (! guide)
            throw RpcError (invalidParams, "No style guide \"" + id + "\" (guides.list)");

        if (guide->builtIn)
        {
            store.setHidden (guide->id, true);
            return changed ({ { "removed", "hidden" } });
        }

        store.remove (guide->id);
        return changed ({ { "removed", "deleted" } });
    }

    if (method == "guides.unhide")
    {
        store.setHidden (readParam<std::string> (params, "id"), false);
        return changed ({ { "unhidden", params["id"] } });
    }

    if (method == "guides.remove")
    {
        const auto removed = store.remove (readParam<std::string> (params, "id"));

        if (removed == guides::GuideStore::Removed::nothing)
            return { { "removed", "nothing" } };

        return changed ({ { "removed", removed == guides::GuideStore::Removed::restored ? "restored" : "deleted" } });
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

// The song maps: every reference track heard once, curated in the References tab.

} // namespace gk
