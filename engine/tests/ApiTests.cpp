#include "TestHelpers.h"

#include "api/ApiHandler.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace gk;
using model::Json;
using Catch::Matchers::ContainsSubstring;

namespace
{
    struct Harness
    {
        Session session { Session::createEmptyProject() };
        ApiHandler handler { session, test::getEngineContext() };
        std::vector<Json> broadcasts;
        Json lastResponse;
        ApiHandler::Client client;
        int nextId = 1;

        Harness()
        {
            handler.setBroadcast ([this] (const std::string& m) { broadcasts.push_back (Json::parse (m)); });
            client.send = [this] (const std::string& m) { lastResponse = Json::parse (m); };
        }

        Json call (const std::string& method, Json params = Json::object())
        {
            handler.handleMessage (Json { { "jsonrpc", "2.0" }, { "id", nextId++ }, { "method", method }, { "params", params } }.dump(), client);
            return lastResponse;
        }
    };
}

TEST_CASE ("The API executes commands and broadcasts patches")
{
    Harness h;
    h.call ("hello", { { "origin", "agent" } });

    auto r = h.call ("execute", Json::parse (R"({ "commands": [ { "type": "setTempo", "tempo": 140 } ], "group": "g1" })"));
    CHECK (r["result"]["entry"] == 1);
    CHECK (h.session.getProject().tempo == 140);

    REQUIRE (h.broadcasts.size() == 1);
    auto& patch = h.broadcasts[0];
    CHECK (patch["method"] == "patch");
    CHECK (patch["params"]["origin"] == "agent");
    CHECK (patch["params"]["ops"][0]["path"] == "/tempo");

    CHECK (h.session.getHistory()[0].group == "g1");

    r = h.call ("undo");
    CHECK (r["result"]["changed"] == true);
    CHECK (h.broadcasts.back()["params"]["kind"] == "undo");
}

TEST_CASE ("API errors are JSON-RPC errors")
{
    Harness h;

    CHECK (h.call ("nope")["error"]["code"] == -32601);
    CHECK_THAT (h.call ("execute", { { "commands", Json::array ({ { { "type", "explode" } } }) } })["error"]["message"].get<std::string>(),
                ContainsSubstring ("params/commands/0"));
    CHECK_THAT (h.call ("execute", Json::parse (R"({ "commands": [ { "type": "removeClip", "clip": "x" } ] })"))["error"]["message"].get<std::string>(),
                ContainsSubstring ("no clip \"x\""));

    h.handler.handleMessage ("not json", h.client);
    CHECK (h.lastResponse["error"]["code"] == -32700);
    CHECK (h.broadcasts.empty());
}

TEST_CASE ("The API reads documents, summaries and history")
{
    Harness h;
    h.call ("execute", Json::parse (R"({ "commands": [ { "type": "addInsert", "insert": { "id": "drums", "volumeDb": -3 } } ] })"));

    CHECK (h.call ("get", { { "path", "/mixer/inserts/1/id" } })["result"] == "drums");
    CHECK (h.call ("get", { { "path", "/nothing" } })["error"]["code"] == -32602);
    CHECK (h.call ("getSummary")["result"]["mixer"][1]["volumeDb"] == -3);
    CHECK (h.call ("getHistory")["result"]["entries"][0]["label"] == "Add insert drums");
    CHECK (h.call ("listEffects")["result"].contains ("reverb"));
}

TEST_CASE ("The API renders and analyses")
{
    Harness h;
    auto beep = test::fixture ("beep.wav").string();

    h.call ("execute", { { "commands", Json::array ({
        Json::parse (R"({ "type": "addChannel", "channel": { "id": "b", "instrument": { "type": "sampler", "layers": [
                         { "id": "l", "pitchTracking": false, "sample": ")" + beep + R"(" } ] } } })"),
        Json::parse (R"({ "type": "addPattern", "pattern": { "id": "p", "notes": [ { "channel": "b", "start": 0, "length": 240, "velocity": 127 } ] } })"),
        Json::parse (R"({ "type": "addTrack", "track": { "id": "t" } })"),
        Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "track": "t", "start": 0, "length": 960, "pattern": "p" } })"),
    }) } });

    auto output = (test::tempDir() / "api-render.wav").string();
    auto r = h.call ("render", { { "path", output } });
    REQUIRE (r.contains ("result"));
    CHECK (r["result"]["analysis"]["peakDb"] == -6.02);
    CHECK (r["result"]["analysis"]["durationSeconds"] == 0.5);

    r = h.call ("analyze", { { "path", output }, { "start", 0.2 } });
    CHECK (r["result"]["peakDb"].get<double>() < -100);
}
