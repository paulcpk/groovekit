#include "TestHelpers.h"

#include "api/ApiHandler.h"
#include "library/SampleLibrary.h"
#include "session/RecentProjects.h"

#include <fstream>

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

TEST_CASE ("The API searches the sample library")
{
    auto root = test::tempDir() / "api-library";
    auto index = test::tempDir() / "api-library.json";
    std::filesystem::remove_all (root);
    std::filesystem::remove (index);
    std::filesystem::create_directories (root / "Kicks");
    std::filesystem::copy_file (test::fixture ("kick.wav"), root / "Kicks" / "Deep Kick.wav");
    std::filesystem::copy_file (test::fixture ("bass_a2.wav"), root / "Sub A.wav");

    library::SampleLibrary library (index);
    Session session (Session::createEmptyProject());
    ApiHandler handler (session, test::getEngineContext(), &library);

    Json response;
    ApiHandler::Client client;
    client.send = [&] (const std::string& m) { response = Json::parse (m); };

    auto call = [&] (const std::string& method, Json params)
    {
        handler.handleMessage (Json { { "jsonrpc", "2.0" }, { "id", 1 }, { "method", method }, { "params", params } }.dump(), client);
        return response;
    };

    call ("library.addFolder", { { "path", root.string() } });
    library.waitForScan();

    auto r = call ("library.search", { { "query", "kick" } });
    REQUIRE (r["result"]["results"].size() == 1);
    CHECK (r["result"]["results"][0]["category"] == "kick");

    r = call ("library.search", { { "category", "bass" } });
    REQUIRE (r["result"]["results"].size() == 1);
    CHECK (r["result"]["results"][0]["rootKey"] == 45);

    r = call ("library.describe", { { "path", (root / "Sub A.wav").string() } });
    CHECK (r["result"]["features"]["pitch"]["key"] == 45);

    CHECK (call ("library.status", Json::object())["result"]["progress"]["samples"] == 2);
}

TEST_CASE ("Mix analysis finds sources that mask each other")
{
    Harness h;
    auto layer = [] (const char* id, const char* sample, bool pitched)
    {
        return Json::parse (std::string (R"({ "type": "addChannel", "channel": { "id": ")") + id + R"(", "instrument": { "type": "sampler", "layers": [
            { "id": "l", "pitchTracking": )" + (pitched ? "true" : "false") + R"(, "rootKey": 45, "sample": ")" + test::fixture (sample).string() + R"(" } ] } } })");
    };

    h.call ("execute", { { "commands", Json::array ({
        layer ("bassA", "bass_a2.wav", true),
        layer ("bassB", "bass_a2.wav", true),
        layer ("hat", "hat.wav", false),
        Json::parse (R"({ "type": "addPattern", "pattern": { "id": "p", "notes": [
            { "channel": "bassA", "start": 0, "length": 960, "key": 45 },
            { "channel": "bassB", "start": 0, "length": 960, "key": 45 },
            { "channel": "hat", "start": 0, "length": 120 }, { "channel": "hat", "start": 480, "length": 120 } ] } })"),
        Json::parse (R"({ "type": "addTrack", "track": { "id": "t" } })"),
        Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "track": "t", "start": 0, "length": 1920, "pattern": "p" } })"),
    }) } });

    auto r = h.call ("analyzeMix");
    REQUIRE (r.contains ("result"));
    auto& result = r["result"];

    CHECK (result["stems"].size() == 3);
    CHECK (result["mix"].contains ("truePeakDb"));

    REQUIRE (result["masking"].size() == 1);
    CHECK (result["masking"][0]["a"] == "bassA");
    CHECK (result["masking"][0]["b"] == "bassB");
    CHECK (result["masking"][0]["bands"][0] == "60-120 Hz");
}

TEST_CASE ("Mastering reaches a target loudness with the limiter")
{
    Harness h;
    auto bass = test::fixture ("bass_a2.wav").string();

    h.call ("execute", { { "commands", Json::array ({
        Json::parse (R"({ "type": "addChannel", "channel": { "id": "b", "instrument": { "type": "sampler", "layers": [
                         { "id": "l", "sample": ")" + bass + R"(", "rootKey": 45 } ] } } })"),
        Json::parse (R"({ "type": "addPattern", "pattern": { "id": "p", "notes": [ { "channel": "b", "start": 0, "length": 960, "key": 45 },
                         { "channel": "b", "start": 1920, "length": 960, "key": 48 } ] } })"),
        Json::parse (R"({ "type": "addTrack", "track": { "id": "t" } })"),
        Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "track": "t", "start": 0, "length": 15360, "pattern": "p" } })"),
    }) } });

    auto r = h.call ("master", { { "targetLufs", -10 }, { "ceilingDb", -1 } });
    REQUIRE (r.contains ("result"));
    CHECK (r["result"]["reachedTarget"] == true);
    CHECK (std::abs (r["result"]["integratedLufs"].get<double>() + 10) <= 0.2);
    CHECK (r["result"]["truePeakDb"].get<double>() < -0.5);

    auto& effects = model::findInsert (h.session.getProject(), "master")->effects;
    REQUIRE (effects.size() == 1);
    CHECK (model::getEffectId (effects[0]) == "master-limiter");

    // One undo removes it again.
    h.call ("undo");
    CHECK (model::findInsert (h.session.getProject(), "master")->effects.empty());
}

TEST_CASE ("Mastering converges on quiet material and keeps true peak under the ceiling")
{
    Harness h;
    auto kick = test::fixture ("kick.wav").string();
    auto bass = test::fixture ("bass_a2.wav").string();

    // Quiet, like raw Digitakt takes: everything 24 dB down.
    h.call ("execute", { { "commands", Json::array ({
        Json::parse (R"({ "type": "addChannel", "channel": { "id": "k", "volumeDb": -24, "instrument": { "type": "sampler", "layers": [
                         { "id": "l", "sample": ")" + kick + R"(", "pitchTracking": false } ] } } })"),
        Json::parse (R"({ "type": "addChannel", "channel": { "id": "b", "volumeDb": -24, "instrument": { "type": "sampler", "layers": [
                         { "id": "l", "sample": ")" + bass + R"(", "rootKey": 45, "mode": "gate" } ] } } })"),
        Json::parse (R"({ "type": "addPattern", "pattern": { "id": "p", "notes": [
                         { "channel": "k", "start": 0, "length": 240 }, { "channel": "k", "start": 960, "length": 240 },
                         { "channel": "k", "start": 1920, "length": 240 }, { "channel": "k", "start": 2880, "length": 240 },
                         { "channel": "b", "start": 480, "length": 400, "key": 45 }, { "channel": "b", "start": 2400, "length": 400, "key": 48 } ] } })"),
        Json::parse (R"({ "type": "addTrack", "track": { "id": "t" } })"),
        Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "track": "t", "start": 0, "length": 15360, "pattern": "p" } })"),
    }) } });

    auto r = h.call ("master", { { "targetLufs", -9 }, { "ceilingDb", -1 } });
    REQUIRE (r.contains ("result"));
    INFO (r.dump());
    CHECK (r["result"]["reachedTarget"] == true);
    CHECK (r["result"]["truePeakDb"].get<double>() <= -0.9);
}

TEST_CASE ("The API gives waveform peaks of audio files")
{
    Harness h;
    auto r = h.call ("audio.peaks", { { "path", test::fixture ("beep.wav").string() }, { "resolution", 100 } });
    INFO (r.dump());
    REQUIRE (r.contains ("result"));

    auto& peaks = r["result"]["peaks"];
    const auto duration = r["result"]["durationSeconds"].get<double>();
    CHECK (duration > 0);
    CHECK (std::abs (static_cast<double> (peaks.size()) - duration * 100) <= 1.0);
    CHECK (std::any_of (peaks.begin(), peaks.end(), [] (auto& p) { return p.template get<double>() > 0.1; }));

    CHECK (h.call ("audio.peaks", { { "path", "/nowhere.wav" } })["error"]["code"] == 2);
}

TEST_CASE ("The API opens and creates projects through its host")
{
    Harness h;
    const auto root = test::tempDir() / "api-projects";
    std::filesystem::remove_all (root);
    std::filesystem::create_directories (root / "existing");
    Session::open (root / "existing");
    std::filesystem::create_directories (root / "busy");
    std::ofstream (root / "busy" / "notes.txt") << "x";

    h.handler.recentFile = root / "recent.json";
    CHECK (h.call ("project.open", { { "path", (root / "existing").string() } })["error"]["message"] == "this engine can't switch projects");

    std::vector<std::filesystem::path> opened;
    h.handler.onOpenProject = [&] (const std::filesystem::path& dir) { opened.push_back (dir); };

    CHECK (h.call ("project.open", { { "path", (root / "existing").string() } })["result"]["project"] == (root / "existing").string());
    CHECK_THAT (h.call ("project.open", { { "path", (root / "busy").string() } })["error"]["message"].get<std::string>(), ContainsSubstring ("isn't a GrooveKit project"));
    CHECK_THAT (h.call ("project.new", { { "path", (root / "existing").string() } })["error"]["message"].get<std::string>(), ContainsSubstring ("already a project"));
    CHECK_THAT (h.call ("project.new", { { "path", (root / "busy").string() } })["error"]["message"].get<std::string>(), ContainsSubstring ("isn't empty"));

    CHECK (h.call ("project.new", { { "path", (root / "fresh" / "song").string() } })["result"]["project"] == (root / "fresh" / "song").string());
    CHECK (std::filesystem::is_directory (root / "fresh" / "song"));

    REQUIRE (opened.size() == 2);
    CHECK (opened[1] == root / "fresh" / "song");

    recent::add (root / "recent.json", root / "existing");
    auto recent = h.call ("project.recent")["result"]["projects"];
    REQUIRE (recent.size() == 1);
    CHECK (recent[0]["name"] == "existing");
}
