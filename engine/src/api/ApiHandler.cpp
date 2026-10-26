#include "ApiHandler.h"

#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "compiler/EditCompiler.h"
#include "effects/BuiltinEffects.h"
#include "library/SampleLibrary.h"
#include "render/Render.h"
#include "session/Summary.h"

#include <juce_events/juce_events.h>

namespace gk
{

using model::Json;

namespace
{
    /** JSON-RPC error codes. */
    enum ErrorCode
    {
        parseError = -32700,
        invalidRequest = -32600,
        methodNotFound = -32601,
        invalidParams = -32602,
        commandFailed = 1,
        operationFailed = 2,
    };

    struct RpcError : std::runtime_error
    {
        RpcError (int c, const std::string& m) : std::runtime_error (m), code (c) {}
        int code;
    };

    Json notification (const char* method, Json params)
    {
        return { { "jsonrpc", "2.0" }, { "method", method }, { "params", std::move (params) } };
    }

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

    Json headJson (const std::optional<int64_t>& head)
    {
        return head ? Json (*head) : Json();
    }

    template <typename T>
    T readParam (const Json& params, const char* key)
    {
        model::Reader reader;
        T value;

        try
        {
            reader.required (params, key, [&] (const Json& v) { readValue (reader, v, value); });
        }
        catch (const model::ReadError& e)
        {
            throw RpcError (invalidParams, std::string ("params") + e.what());
        }

        return value;
    }

    std::optional<double> optionalNumber (const Json& params, const char* key)
    {
        if (! params.contains (key) || params[key].is_null())
            return {};

        if (! params[key].is_number())
            throw RpcError (invalidParams, std::string ("params/") + key + ": expected number");

        return params[key].get<double>();
    }
}

ApiHandler::ApiHandler (Session& s, EngineContext& c, library::SampleLibrary* l)
    : session (s), context (c), library (l)
{
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
    session.removeListener (listenerId);
}

void ApiHandler::setBroadcast (Send b)
{
    broadcast = std::move (b);
}

std::string ApiHandler::getSnapshotMessage() const
{
    return notification ("snapshot", { { "document", session.getDocument() }, { "head", headJson (session.getHead()) } }).dump();
}

void ApiHandler::handleMessage (const std::string& text, Client& client)
{
    Json id;
    Json response;

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
        auto params = request.value ("params", Json::object());

        if (! params.is_object())
            throw RpcError (invalidParams, "params must be an object");

        auto result = call (request["method"].get<std::string>(), params, client);

        if (id.is_null())
            return; // a notification: no response

        response = { { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move (result) } };
    }
    catch (const RpcError& e)
    {
        response = { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", e.code }, { "message", e.what() } } } };
    }
    catch (const CommandError& e)
    {
        response = { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", commandFailed }, { "message", e.what() } } } };
    }
    catch (const std::exception& e)
    {
        response = { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", operationFailed }, { "message", e.what() } } } };
    }

    if (client.send)
        client.send (response.dump());
}

Json ApiHandler::call (const std::string& method, const Json& params, Client& client)
{
    if (method == "hello")
    {
        if (params.contains ("origin"))
            client.origin = readParam<std::string> (params, "origin");

        return { { "server", "groovekit-engine" }, { "protocol", 1 } };
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

    if (method == "render")
        return render (params);

    if (method == "analyze")
    {
        auto audio = analysis::loadAudio (readParam<std::string> (params, "path"));
        return analysis::analyse (analysis::slice (audio, { optionalNumber (params, "start").value_or (0),
                                                            optionalNumber (params, "end"), {} }));
    }

    if (method == "listEffects")
        return effects::catalogueToJson();

    if (method.starts_with ("library."))
        return callLibrary (method.substr (8), params);

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::render (const Json& params)
{
    RenderOptions options;

    if (auto v = optionalNumber (params, "start"))          options.start = static_cast<model::Ticks> (*v);
    if (auto v = optionalNumber (params, "end"))            options.end = static_cast<model::Ticks> (*v);
    if (auto v = optionalNumber (params, "tailSeconds"))    options.tailSeconds = *v;

    const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");

    std::filesystem::path output;

    if (params.contains ("path"))
    {
        output = readParam<std::string> (params, "path");
    }
    else
    {
        std::filesystem::create_directories (projectDir / "renders");
        output = projectDir / "renders" / ("render-" + std::to_string (++renderCount) + ".wav");
    }

    auto compiled = compileProject (context.getEngine(), session.getProject(), { projectDir, tracktion::Edit::forRendering });
    renderToWav (*compiled.edit, session.getProject(), output, options);

    return { { "path", std::filesystem::absolute (output).string() },
             { "analysis", analysis::analyse (analysis::loadAudio (output)) } };
}

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

    if (method == "folders" || method == "status")
        return { { "folders", folders() }, { "progress", progressJson() } };

    if (method == "addFolder")
    {
        library->addFolder (readParam<std::string> (params, "path"));

        if (params.value ("scan", true))
            startLibraryScan();

        return { { "folders", folders() }, { "progress", progressJson() } };
    }

    if (method == "removeFolder")
    {
        library->removeFolder (readParam<std::string> (params, "path"));
        return { { "folders", folders() }, { "progress", progressJson() } };
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
        if (auto v = optionalNumber (params, "tempo"))          q.tempo = v;
        if (auto v = optionalNumber (params, "minDuration"))    q.minDuration = v;
        if (auto v = optionalNumber (params, "maxDuration"))    q.maxDuration = v;

        auto results = Json::array();

        for (auto& e : library->search (q))
            results.push_back (library::SampleLibrary::toJson (e, false));

        return { { "results", results }, { "librarySize", library->size() } };
    }

    if (method == "describe")
    {
        auto path = readParam<std::string> (params, "path");
        auto entry = library->find (path);
        return library::SampleLibrary::toJson (entry ? *entry : library::SampleLibrary::analyseFile (path), true);
    }

    throw RpcError (methodNotFound, "unknown method \"library." + method + "\"");
}

void ApiHandler::startLibraryScan()
{
    library->startScan ([this, alive = alive] (const library::SampleLibrary::Progress& p)
    {
        Json params { { "running", p.running }, { "filesFound", p.filesFound }, { "analysed", p.analysed },
                      { "unchanged", p.unchanged }, { "failed", p.failed } };

        // Called on the scan thread: broadcast from the message thread.
        juce::MessageManager::callAsync ([this, alive, params]
        {
            if (*alive && broadcast)
                broadcast (notification ("libraryProgress", params).dump());
        });
    });
}

} // namespace gk
