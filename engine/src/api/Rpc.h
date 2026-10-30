#pragma once

// What the API handler's files share: JSON-RPC errors, parameter readers and the JSON shapes
// several of them send.

#include "library/StarterPack.h"
#include "live/LiveEngine.h"
#include "model/JsonIO.h"
#include "session/Session.h"

#include <filesystem>
#include <optional>
#include <stdexcept>

namespace gk
{

using model::Json;

/** Transport state as clients get it; mode "pattern" plays one pattern on a loop. */
inline model::Json transportJson (const LiveEngine::TransportState& t)
{
    model::Json j { { "playing", t.playing }, { "recording", t.recording }, { "position", t.position }, { "seconds", t.seconds },
                    { "mode", t.pattern ? "pattern" : "song" }, { "metronome", t.metronome }, { "countInBars", t.countInBars },
                    { "time", t.time }, { "outputLatencyMs", std::round (t.outputLatencyMs * 10) / 10 } };

    if (t.pattern)
        j["pattern"] = *t.pattern;

    return j;
}

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

inline Json starterJson (const library::StarterPack::Status& s)
{
    return { { "installed", s.installed }, { "downloading", s.downloading }, { "progress", s.progress }, { "totalBytes", s.totalBytes },
             { "files", s.files }, { "folder", s.folder }, { "error", s.error.empty() ? Json() : Json (s.error) } };
}

inline Json notification (const char* method, Json params)
{
    return { { "jsonrpc", "2.0" }, { "method", method }, { "params", std::move (params) } };
}

inline Json headJson (const std::optional<int64_t>& head)
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

inline std::optional<double> optionalNumber (const Json& params, const char* key)
{
    if (! params.contains (key) || params[key].is_null())
        return {};

    if (! params[key].is_number())
        throw RpcError (invalidParams, std::string ("params/") + key + ": expected number");

    return params[key].get<double>();
}

/** The folder relative sample paths and renders resolve against: the project's, or a scratch one. */
inline std::filesystem::path projectDirOf (const Session& session)
{
    return session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
}

} // namespace gk
