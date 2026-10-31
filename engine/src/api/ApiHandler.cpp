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
    const char* toString (Change::Kind k)
    {
        switch (k)
        {
            case Change::Kind::execute: return "execute";
            case Change::Kind::undo:    return "undo";
            case Change::Kind::redo:    return "redo";
        }

        return "";
    }




    /** Copies a project folder without its renders (the song's output, not its material, often
        large, and numbered afresh in the copy) or the renderer's temporary files. */
}

ApiHandler::ApiHandler (Session& s, EngineContext& c, library::SampleLibrary* l, LiveEngine* le, PluginCatalog* p, listening::Listening* li,
                        library::StarterPack* sp)
    : session (s), context (c), library (l), live (le), plugins (p), listening (li), starter (sp)
{
    // The starter samples' download reports from its thread: broadcast from the message thread,
    // and once they're in, scan them into the library.
    if (starter != nullptr)
        starter->setObserver ([this, alive = alive] (const library::StarterPack::Status& status)
        {
            juce::MessageManager::callAsync ([this, alive, status]
            {
                if (! *alive)
                    return;

                if (broadcast)
                    broadcast (notification ("starter", starterJson (status)).dump());

                if (status.installed && ! status.downloading && library != nullptr)
                    startLibraryScan();
            });
        });

    // Progress comes from download, indexing and analysis threads: broadcast from the message
    // thread. (A handler for another project replaces this observer.)
    if (listening != nullptr)
        listening->setObserver ([this, alive = alive] (const Json& status)
        {
            juce::MessageManager::callAsync ([this, alive, status]
            {
                if (*alive && broadcast)
                    broadcast (notification ("listening", status).dump());
            });
        });

    if (live != nullptr)
        live->onTransportChange = [this] (const LiveEngine::TransportState& t)
        {
            if (broadcast)
                broadcast (notification ("transport", transportJson (t)).dump());
        };

    if (live != nullptr)
        live->onLevels = [this] (const LiveEngine::Levels& levels)
        {
            if (! broadcast)
                return;

            auto round = [] (float v) { return std::round (v * 1000.0f) / 1000.0f; };

            // Built as lists of pairs: an object's [] scans its keys for each one, thirty times a second.
            Json::object_t inserts, channels;
            inserts.reserve (levels.inserts.size());
            channels.reserve (levels.channels.size());

            for (auto& [id, peak] : levels.inserts)
                inserts.push_back ({ id, Json::array ({ round (peak.first), round (peak.second) }) });

            for (auto& [id, peak] : levels.channels)
                channels.push_back ({ id, round (peak) });

            broadcast (notification ("levels", { { "inserts", Json (std::move (inserts)) }, { "channels", Json (std::move (channels)) } }).dump());
        };

    // What was done in a plugin's window goes into the project when the window closes.
    if (live != nullptr)
        PluginWindowBehaviour::onWindowClosed = [this, alive = alive] (tracktion::Plugin& plugin)
        {
            if (! *alive)
                return;

            if (const auto key = live->findPluginKey (plugin); ! key.empty())
            {
                try
                {
                    captureState (key, "ui");
                }
                catch (const std::exception& e)
                {
                    juce::Logger::writeToLog ("Couldn't save the plugin's state: " + juce::String (e.what()));
                }
            }
        };

    // The bar above plugin windows edits the project like the UI, and asks the UI for automation clips.
    if (live != nullptr)
        PluginWindowBehaviour::host = {
            .findKey = [this] (tracktion::Plugin& plugin) { return live->findPluginKey (plugin); },
            .getProject = [this] { return &session.getProject(); },
            .execute = [this] (const std::vector<model::Command>& commands, const std::string& label, const std::string& group)
            {
                try
                {
                    lastOrigin = "ui";
                    session.execute (commands, { "ui", group, label });
                }
                catch (const std::exception& e)
                {
                    juce::Logger::writeToLog ("Plugin window: " + juce::String (e.what()));
                }
            },
            .createAutomation = [this] (const model::AutomationTarget& target, double value)
            {
                if (broadcast)
                    broadcast (notification ("createAutomation", { { "target", model::toJson (target) }, { "value", value } }).dump());
            },
            .typedKey = [this] (const std::string& key, bool down)
            {
                if (broadcast)
                    broadcast (notification ("typingKey", { { "key", key }, { "down", down } }).dump());
            },
        };

    listenerId = session.addListener ([this] (const Change& change)
    {
        if (! broadcast)
            return;

        Json params {
            { "kind", toString (change.kind) },
            { "entry", change.entry },
            { "head", headJson (change.head) },
            { "ops", model::toJson (change.ops) },
        };

        auto& entry = session.getHistory().at (static_cast<size_t> (change.entry - 1));
        params["origin"] = change.kind == Change::Kind::execute ? entry.origin : lastOrigin;
        params["label"] = entry.label;

        broadcast (notification ("patch", std::move (params)).dump());
    });
}

ApiHandler::~ApiHandler()
{
    *alive = false;

    if (live != nullptr)
    {
        live->onTransportChange = {};
        live->onLevels = {};
        PluginWindowBehaviour::onWindowClosed = {};
        PluginWindowBehaviour::host = {};
    }

    session.removeListener (listenerId);
}

void ApiHandler::setBroadcast (Send b)
{
    broadcast = std::move (b);
}

std::string ApiHandler::getSnapshotMessage() const
{
    Json params { { "document", session.getDocument() }, { "head", headJson (session.getHead()) }, { "saved", headJson (session.getSavedHead()) } };

    if (auto dir = session.getProjectDir())
        params["project"] = dir->string();

    return notification ("snapshot", std::move (params)).dump();
}

Json ApiHandler::errorResponse (const Json& id, std::exception_ptr error)
{
    try
    {
        std::rethrow_exception (error);
    }
    catch (const RpcError& e)
    {
        return { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", e.code }, { "message", e.what() } } } };
    }
    catch (const CommandError& e)
    {
        return { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", commandFailed }, { "message", e.what() } } } };
    }
    catch (const std::invalid_argument& e)
    {
        // How stores, parsers and the encoders say the input was wrong.
        return { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", invalidParams }, { "message", e.what() } } } };
    }
    catch (const std::exception& e)
    {
        return { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", operationFailed }, { "message", e.what() } } } };
    }
}

void ApiHandler::handleMessage (const std::string& text, Client& client)
{
    Json id;
    Json response;
    deferred = nullptr;

    try
    {
        Json request;

        try
        {
            request = Json::parse (text);
        }
        catch (const Json::parse_error& e)
        {
            throw RpcError (parseError, e.what());
        }

        if (! request.is_object() || ! request.contains ("method") || ! request["method"].is_string())
            throw RpcError (invalidRequest, "expected a JSON-RPC request");

        id = request.value ("id", Json());
        Json params = request.contains ("params") ? std::move (request["params"]) : Json::object();

        if (! params.is_object())
            throw RpcError (invalidParams, "params must be an object");

        auto result = dispatch (request["method"].get<std::string>(), params, client);

        if (auto steps = std::exchange (deferred, nullptr))
        {
            // Answered when the steps are done; the message thread goes on serving meanwhile. The
            // client's send outlives this handler (it holds the connection weakly), so a project
            // switched in between still gets its answer.
            steps->runLater (context, alive, [steps, send = client.send, id] (std::exception_ptr error)
            {
                if (id.is_null() || ! send)
                    return;

                send ((error ? errorResponse (id, error)
                             : Json { { "jsonrpc", "2.0" }, { "id", id }, { "result", steps->result } }).dump());
            });
            return;
        }

        if (id.is_null())
            return; // a notification: no response

        response = { { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move (result) } };
    }
    catch (...)
    {
        response = errorResponse (id, std::current_exception());
    }

    if (client.send)
        client.send (response.dump());
}

Json ApiHandler::call (const std::string& method, const Json& params, Client& client)
{
    deferred = nullptr;
    auto result = dispatch (method, params, client);

    if (auto steps = std::exchange (deferred, nullptr))
    {
        steps->runNow();
        return steps->result;
    }

    return result;
}

Json ApiHandler::dispatch (const std::string& method, const Json& params, Client& client)
{
    using Route = Json (*) (ApiHandler&, const std::string&, const Json&, Client&);

    // The domain each method belongs to: by its whole name, else by its prefix. Each handler gets
    // the method name as it came in.
    static const std::map<std::string, Route> names {
        { "hello",       [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callSession (m, p, c); } },
        { "execute",     [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callSession (m, p, c); } },
        { "undo",        [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callSession (m, p, c); } },
        { "redo",        [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callSession (m, p, c); } },
        { "get",         [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callSession (m, p, c); } },
        { "getSummary",  [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callSession (m, p, c); } },
        { "getHistory",  [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callSession (m, p, c); } },
        { "listEffects", [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callSession (m, p, c); } },
        { "render",      [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callRender (m, p, c); } },
        { "master",      [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callRender (m, p, c); } },
        { "analyzeMix",  [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callRender (m, p, c); } },
        { "analyze",     [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callRender (m, p, c); } },
        { "audio.peaks", [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callRender (m, p, c); } },
    };

    static const std::vector<std::pair<std::string, Route>> prefixes {
        { "project.",    [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callProject (m, p); } },
        { "voice.",      [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callVoice (m, p); } },
        { "melody.",     [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callMelody (m, p); } },
        { "listening.",  [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callListening (m, p); } },
        { "styles.",     [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callListening (m, p); } },
        { "maps.",       [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callMaps (m, p); } },
        { "voices.",     [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callVoices (m, p); } },
        { "match.",      [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callMatch (m, p); } },
        { "picks.",      [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callMatch (m, p); } },
        { "mix.",        [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callMix (m, p, c); } },
        { "chats.",      [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callChats (m, p); } },
        { "notes.",      [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callNotes (m, p); } },
        { "guides.",     [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callGuides (m, p); } },
        { "presets.",    [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callPresets (m, p); } },
        { "library.",    [] (ApiHandler& h, const std::string& m, const Json& p, Client&) { return h.callLibrary (m.substr (8), p); } },
        { "plugins.",    [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callPlugins (m.substr (8), p, c); } },
        { "transport.",  [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callLive (m, p, c); } },
        { "record.",     [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callLive (m, p, c); } },
        { "audio.",      [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callLive (m, p, c); } },
        { "midi.",       [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callLive (m, p, c); } },
        { "live.",       [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callLive (m, p, c); } },
        { "preview.",    [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callLive (m, p, c); } },
        { "mixer.",      [] (ApiHandler& h, const std::string& m, const Json& p, Client& c) { return h.callLive (m, p, c); } },
    };

    if (auto it = names.find (method); it != names.end())
        return it->second (*this, method, params, client);

    for (auto& [prefix, route] : prefixes)
        if (method.starts_with (prefix))
            return route (*this, method, params, client);

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::callSession (const std::string& method, const Json& params, Client& client)
{
    if (method == "hello")
    {
        if (params.contains ("origin"))
            client.origin = readParam<std::string> (params, "origin");

        // Goes up when clients need methods an older engine lacks (2: song maps and sound matching).
        Json result { { "server", "groovekit-engine" }, { "protocol", 2 } };

        if (auto dir = session.getProjectDir())
            result["project"] = dir->string();

        return result;
    }

    if (method == "execute")
    {
        ExecuteOptions options;
        options.origin = client.origin;

        if (params.contains ("group"))  options.group = readParam<std::string> (params, "group");
        if (params.contains ("label"))  options.label = readParam<std::string> (params, "label");

        auto& entry = session.execute (readParam<std::vector<model::Command>> (params, "commands"), options);
        return { { "entry", entry.id }, { "label", entry.label } };
    }

    if (method == "undo" || method == "redo")
    {
        const bool wholeGroup = params.value ("wholeGroup", true);
        lastOrigin = client.origin;
        const bool changed = method == "undo" ? session.undo (wholeGroup) : session.redo (wholeGroup);
        return { { "changed", changed }, { "head", headJson (session.getHead()) } };
    }

    if (method == "get")
    {
        auto path = params.value ("path", std::string());

        try
        {
            return session.getDocument().at (Json::json_pointer (path));
        }
        catch (const Json::exception&)
        {
            throw RpcError (invalidParams, "nothing at \"" + path + "\"");
        }
    }

    if (method == "getSummary")
        return summarizeProject (session.getProject());

    if (method == "getHistory")
    {
        auto entries = Json::array();

        for (auto& e : session.getHistory())
        {
            Json j { { "id", e.id }, { "time", e.time }, { "origin", e.origin }, { "label", e.label } };

            if (e.parent)   j["parent"] = *e.parent;
            if (e.group)    j["group"] = *e.group;

            entries.push_back (std::move (j));
        }

        return { { "entries", entries }, { "head", headJson (session.getHead()) } };
    }

    // The effect catalogue, with each effect's factory presets.
    if (method == "listEffects")
    {
        auto catalogue = effects::catalogueToJson();

        for (auto& [kind, info] : catalogue.items())
            if (presets::factory().contains (kind))
                info["presets"] = presets::factory()[kind];

        return catalogue;
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::callPresets (const std::string& method, const Json& params)
{
    const auto folder = presetsFolder.empty() ? EngineContext::getAppDataFolder() / "presets" : presetsFolder;
    const auto kind = readParam<std::string> (params, "kind");

    try
    {
        if (method == "presets.list")
            return presets::list (kind, folder);

        if (method == "presets.save")
        {
            auto preset = params.contains ("preset") ? params["preset"] : Json();
            presets::save (kind, preset, folder);
            return presets::list (kind, folder);
        }

        if (method == "presets.delete")
        {
            presets::remove (kind, readParam<std::string> (params, "name"), folder);
            return presets::list (kind, folder);
        }
    }
    catch (const std::runtime_error& e)
    {
        throw RpcError (invalidParams, e.what());
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

} // namespace gk
