#include "TestHelpers.h"

#include "api/ApiHandler.h"
#include "analysis/Analysis.h"
#include "library/SampleLibrary.h"
#include "listening/Listening.h"
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
        Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "row": 0, "start": 0, "length": 960, "pattern": "p" } })"),
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

    // A bare file name finds the library's sample of that name; a missing one says what to pass.
    r = call ("library.describe", { { "path", "Sub A.wav" } });
    CHECK (r["result"]["features"]["pitch"]["key"] == 45);
    r = call ("library.describe", { { "path", "Nowhere.wav" } });
    CHECK_THAT (r["error"]["message"].get<std::string>(), ContainsSubstring ("full path"));

    CHECK (call ("library.status", Json::object())["result"]["progress"]["samples"] == 2);
}

namespace
{
    /** Ten seconds of a kick on every beat at 140 BPM over a held saw bass, at 44.1 kHz. */
    void writeTrack (const std::filesystem::path& file, double bassHz)
    {
        const int rate = 44100, length = 10 * rate;
        juce::AudioBuffer<float> buffer (2, length);

        for (int i = 0; i < length; ++i)
        {
            const auto t = static_cast<double> (i) / rate;
            const auto beat = std::fmod (t, 60.0 / 140.0);
            const auto kick = std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 150.0 * std::exp (-beat * 30.0)) * beat) * std::exp (-beat * 12.0);
            const auto bass = 0.25 * (2.0 * std::fmod (t * bassHz, 1.0) - 1.0);

            for (int c = 0; c < 2; ++c)
                buffer.setSample (c, i, static_cast<float> (0.6 * kick + bass));
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (juce::File (file.string())),
                                                                              rate, 2, 16, {}, 0));
        writer->writeFromAudioSampleBuffer (buffer, 0, length);
    }
}

TEST_CASE ("The API listens to samples and styles once the models are there")
{
    auto root = test::tempDir() / "api-listening";
    std::filesystem::remove_all (root);
    std::filesystem::create_directories (root / "samples");
    std::filesystem::copy_file (test::fixture ("kick.wav"), root / "samples" / "Deep Kick.wav");
    std::filesystem::copy_file (test::fixture ("bass_a2.wav"), root / "samples" / "Sub A.wav");

    library::SampleLibrary library (root / "library.json");
    library.addFolder (root / "samples");
    library.startScan();
    library.waitForScan();

    // Without the models: plain searches work, listening says what it needs.
    listening::Listening::Files files { root / "no-models", root / "sounds.bin", root / "styles", root / "taste.json" };
    listening::Listening without (library, files);
    Session session (Session::createEmptyProject());
    ApiHandler handler (session, test::getEngineContext(), &library, nullptr, nullptr, &without);

    Json response;
    ApiHandler::Client client;
    client.send = [&] (const std::string& m) { response = Json::parse (m); };

    auto call = [&] (ApiHandler& h, const std::string& method, Json params)
    {
        h.handleMessage (Json { { "jsonrpc", "2.0" }, { "id", 1 }, { "method", method }, { "params", params } }.dump(), client);
        return response;
    };

    CHECK (call (handler, "listening.status", Json::object())["result"]["models"]["ready"] == false);
    CHECK (call (handler, "library.search", { { "query", "kick" } })["result"]["results"].size() == 1);
    CHECK_THAT (call (handler, "library.search", { { "style", "Techno" } })["error"]["message"].get<std::string>(), ContainsSubstring ("listening models"));
    CHECK_THAT (call (handler, "styles.add", { { "path", root.string() } })["error"]["message"].get<std::string>(), ContainsSubstring ("downloaded"));
    CHECK (call (handler, "styles.list", Json::object())["result"]["styles"].empty());

    // The references folder: offered until it exists, then one entry per style folder in it
    // (not albums, mixes or leftovers), each with the guide it goes with.
    handler.referencesFolder = root / "References";
    std::filesystem::remove_all (handler.referencesFolder);
    CHECK (call (handler, "styles.references", Json::object())["result"]["exists"] == false);
    auto references = call (handler, "styles.createReferences", Json::object())["result"];
    CHECK (references["exists"] == true);
    CHECK (references["folders"].empty());

    for (auto folder : { "Berlin Techno", "Albums", "Mixes", "Not used" })
        std::filesystem::create_directories (handler.referencesFolder / folder / "From mixes");

    std::filesystem::copy_file (test::repoRoot() / "tests" / "fixtures" / "samples" / "kick.wav", handler.referencesFolder / "Berlin Techno" / "kick.wav");
    std::filesystem::copy_file (test::repoRoot() / "tests" / "fixtures" / "samples" / "hat.wav", handler.referencesFolder / "Berlin Techno" / "From mixes" / "hat.wav");
    std::filesystem::copy_file (test::repoRoot() / "tests" / "fixtures" / "samples" / "hat.wav", handler.referencesFolder / "Albums" / "hat.wav");
    references = call (handler, "styles.references", Json::object())["result"];
    REQUIRE (references["folders"].size() == 1);
    CHECK (references["folders"][0]["name"] == "Berlin Techno");
    CHECK (references["folders"][0]["files"] == 2);
    CHECK (references["folders"][0]["unheard"] == 2);
    CHECK (references["folders"][0]["style"].is_null());
    CHECK (references["folders"][0]["guide"] == "berlin-techno");

    // Verdicts are kept, under the library's name for the file.
    auto kick = (root / "samples" / "Deep Kick.wav").string();
    CHECK (call (handler, "library.rate", { { "path", kick }, { "style", "Techno" }, { "fits", false } })["result"]["fits"] == false);
    CHECK (call (handler, "library.ratings", Json::object())["result"]["ratings"].size() == 1);

    // With the models (on a Mac that has downloaded them): samples are heard, and a style learnt.
    if (! listening::Models (listening::Models::getDefaultFolder()).getStatus().ready)
        SKIP ("The listening models aren't downloaded");

    files.models = listening::Models::getDefaultFolder();
    listening::Listening with (library, files);
    ApiHandler heard (session, test::getEngineContext(), &library, nullptr, nullptr, &with);

    with.startIndexing();

    for (int i = 0; i < 600 && (with.getIndex().size() < 2 || with.getIndex().getProgress().running); ++i)
        juce::Thread::sleep (100);

    INFO (with.getStatus().dump());
    REQUIRE (with.getIndex().size() == 2);

    auto r = call (heard, "library.search", { { "sound", "a deep low bass note" } });
    REQUIRE (r["result"]["results"].size() == 2);
    CHECK (r["result"]["results"][0]["match"].is_number());
    CHECK (r["result"]["results"][0]["sounds"].size() == 3);

    std::filesystem::create_directories (root / "refs");
    writeTrack (root / "refs" / "one.wav", 55);
    writeTrack (root / "refs" / "two.wav", 49);
    CHECK (call (heard, "styles.add", { { "path", (root / "refs").string() }, { "name", "Synthetic" } })["result"]["name"] == "Synthetic");
    with.getStyles().wait();

    auto style = call (heard, "styles.describe", { { "name", "Synthetic" } })["result"];
    REQUIRE (style["tracks"] == 2);
    CHECK (std::abs (style["tempo"]["median"].get<double>() - 140.0) < 1.5);
    CHECK (style["sounds"].contains ("kick"));

    r = call (heard, "library.search", { { "style", "Synthetic" } });
    REQUIRE (r["result"]["results"].size() == 2);
    CHECK (r["result"]["results"][0]["fit"].is_number());

    // The kick that doesn't fit Techno still has a verdict only there.
    r = call (heard, "library.search", { { "style", "Synthetic" }, { "query", "kick" } });
    CHECK_FALSE (r["result"]["results"][0].contains ("rating"));
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
        Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "row": 0, "start": 0, "length": 1920, "pattern": "p" } })"),
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
        Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "row": 0, "start": 0, "length": 15360, "pattern": "p" } })"),
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
        Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "row": 0, "start": 0, "length": 15360, "pattern": "p" } })"),
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

    // Written directly: recent::add leaves out projects in the temp folder, where tests run.
    std::ofstream (root / "recent.json") << Json { { "projects", Json::array ({ (root / "existing").string() }) } }.dump();
    auto recent = h.call ("project.recent")["result"]["projects"];
    REQUIRE (recent.size() == 1);
    CHECK (recent[0]["name"] == "existing");
}

TEST_CASE ("Save as copies the project folder and opens the copy")
{
    const auto root = test::tempDir() / "api-save-as";
    std::filesystem::remove_all (root);
    auto session = Session::open (root / "original");
    std::filesystem::create_directories (root / "original" / "recordings");
    std::ofstream (root / "original" / "recordings" / "take.wav") << "x";

    ApiHandler handler (*session, test::getEngineContext());
    std::vector<std::filesystem::path> opened;
    handler.onOpenProject = [&] (const std::filesystem::path& dir) { opened.push_back (dir); };

    ApiHandler::Client client;
    Json response;
    client.send = [&] (const std::string& m) { response = Json::parse (m); };
    auto call = [&] (const std::string& method, Json params)
    {
        handler.handleMessage (Json { { "jsonrpc", "2.0" }, { "id", 1 }, { "method", method }, { "params", params } }.dump(), client);
        return response;
    };

    CHECK (call ("project.saveAs", { { "path", (root / "copy").string() } })["result"]["project"] == (root / "copy").string());
    CHECK (std::filesystem::is_regular_file (root / "copy" / "song.json"));
    CHECK (std::filesystem::is_regular_file (root / "copy" / "recordings" / "take.wav"));
    REQUIRE (opened.size() == 1);
    CHECK (opened[0] == root / "copy");

    CHECK_THAT (call ("project.saveAs", { { "path", (root / "copy").string() } })["error"]["message"].get<std::string>(), ContainsSubstring ("choose a new folder"));
}

TEST_CASE ("Files are imported and collected into the project")
{
    const auto root = test::tempDir() / "api-import";
    std::filesystem::remove_all (root);
    auto session = Session::open (root / "song");
    ApiHandler handler (*session, test::getEngineContext());

    ApiHandler::Client client;
    Json response;
    client.send = [&] (const std::string& m) { response = Json::parse (m); };
    auto call = [&] (const std::string& method, Json params)
    {
        handler.handleMessage (Json { { "jsonrpc", "2.0" }, { "id", 1 }, { "method", method }, { "params", params } }.dump(), client);
        return response;
    };

    const auto beep = test::fixture ("beep.wav");
    CHECK (call ("project.importFile", { { "path", beep.string() } })["result"]["path"] == "samples/beep.wav");
    CHECK (std::filesystem::is_regular_file (root / "song" / "samples" / "beep.wav"));

    // The same file again is the same import; another with the name gets its own.
    CHECK (call ("project.importFile", { { "path", beep.string() } })["result"]["path"] == "samples/beep.wav");
    CHECK (call ("project.importData", { { "name", "beep.wav" }, { "data", juce::Base64::toBase64 ("not a wav", 9).toStdString() } })["result"]["path"] == "samples/beep-2.wav");

    model::Reader r;
    model::Command c;
    readValue (r, Json::parse (R"({ "type": "addChannel", "channel": { "id": "k", "instrument": { "type": "sampler", "layers": [
                  { "id": "l", "sample": ")" + test::fixture ("kick.wav").string() + R"(" } ] } } })"), c);
    session->execute ({ c });

    auto collected = call ("project.collectSamples", Json::object())["result"];
    CHECK (collected["copied"] == 1);
    CHECK (std::get<model::SamplerInstrument> (session->getProject().channels[0].instrument).layers[0].sample == "samples/kick.wav");
    CHECK (std::filesystem::is_regular_file (root / "song" / "samples" / "kick.wav"));
}

TEST_CASE ("Renders take a format and can include stems")
{
    Harness h;
    model::Reader r;
    std::vector<model::Command> commands;

    for (auto json : {
             Json::parse (R"({ "type": "addInsert", "insert": { "id": "drums" } })"),
             Json::parse (R"({ "type": "addChannel", "channel": { "id": "b", "insert": "drums", "instrument": { "type": "sampler", "layers": [
                              { "id": "l", "pitchTracking": false, "sample": ")" + test::fixture ("beep.wav").string() + R"(" } ] } } })"),
             Json::parse (R"({ "type": "addPattern", "pattern": { "id": "p", "notes": [ { "channel": "b", "start": 0, "length": 240 } ] } })"),
             Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "row": 0, "start": 0, "length": 960, "pattern": "p" } })") })
    {
        readValue (r, json, commands.emplace_back());
    }

    h.session.execute (commands);

    const auto output = test::tempDir() / "api-stems" / "mix.wav";
    std::filesystem::create_directories (output.parent_path());
    auto result = h.call ("render", { { "path", output.string() }, { "bitDepth", 16 }, { "sampleRate", 44100 }, { "stems", true } })["result"];

    REQUIRE (result["stems"].size() == 1);
    CHECK (result["stems"][0]["insert"] == "drums");
    CHECK (std::filesystem::is_regular_file (output.parent_path() / "mix-drums.wav"));

    auto mix = analysis::loadAudio (output);
    CHECK (mix.sampleRate == 44100);
    CHECK (h.call ("render", { { "bitDepth", 12 } })["error"]["code"] == -32602);

    // Silence before and after, and peaks brought to -1 dB, the stems by the same gain.
    const auto padded = output.parent_path() / "padded.wav";
    result = h.call ("render", { { "path", padded.string() }, { "sampleRate", 44100 }, { "silenceBefore", 0.5 }, { "silenceAfter", 0.25 },
                                 { "normalize", -1.0 }, { "stems", true } })["result"];
    auto audio = analysis::loadAudio (padded);
    CHECK (std::abs (audio.buffer.getNumSamples() / 44100.0 - (mix.buffer.getNumSamples() / 44100.0 + 0.75)) < 0.01);
    CHECK (audio.buffer.getMagnitude (0, 22000) == 0.0f);
    CHECK (std::abs (juce::Decibels::gainToDecibels (audio.buffer.getMagnitude (0, audio.buffer.getNumSamples())) + 1.0f) < 0.05f);
    CHECK (result["gainDb"].is_number());
    CHECK (std::filesystem::is_regular_file (output.parent_path() / "padded-drums.wav"));

    // FLAC and MP3.
    const auto flac = output.parent_path() / "mix.flac";
    CHECK (h.call ("render", { { "path", flac.string() }, { "format", "flac" }, { "bitDepth", 16 } })["result"]["format"] == "flac");
    CHECK (std::abs (analysis::loadAudio (flac).buffer.getNumSamples() - analysis::loadAudio (output).buffer.getNumSamples() * 48000.0 / 44100.0) < 64);

    const auto mp3 = output.parent_path() / "mix.mp3";
    CHECK (h.call ("render", { { "path", mp3.string() }, { "format", "mp3" }, { "bitrate", 192 }, { "sampleRate", 44100 } })["result"]["format"] == "mp3");
    std::ifstream in (mp3, std::ios::binary);
    unsigned char head[2] {};
    in.read (reinterpret_cast<char*> (head), 2);
    CHECK (head[0] == 0xFF);
    CHECK ((head[1] & 0xE0) == 0xE0);
    CHECK (std::filesystem::file_size (mp3) > 1000);

    CHECK (h.call ("render", { { "format", "mp3" }, { "sampleRate", 96000 } })["error"]["code"] == -32602);
    CHECK (h.call ("render", { { "format", "flac" }, { "bitDepth", 32 } })["error"]["code"] == -32602);
    CHECK (h.call ("render", { { "format", "ogg" } })["error"]["code"] == -32602);
}

TEST_CASE ("Rows are numbered, need no adding, and move with their clips and settings")
{
    Harness h;
    auto r = h.call ("execute", Json::parse (R"({ "commands": [
        { "type": "addPattern", "pattern": { "id": "p", "notes": [] } },
        { "type": "addClip", "clip": { "type": "pattern", "id": "a", "row": 0, "start": 0, "length": 960, "pattern": "p" } },
        { "type": "addClip", "clip": { "type": "pattern", "id": "b", "row": 5, "start": 0, "length": 960, "pattern": "p" } },
        { "type": "updateRow", "row": 3, "changes": { "name": "Drums", "mute": true } } ] })"));
    REQUIRE (r.contains ("result"));

    auto& rows = h.session.getProject().playlist.rows;
    REQUIRE (rows.size() == 4);
    CHECK (rows[0] == model::PlaylistRow {});
    CHECK (rows[3].name == "Drums");
    CHECK (rows[3].mute);

    auto rowOf = [&] (const char* id)
    {
        for (auto& clip : h.session.getProject().playlist.clips)
            if (model::getClipId (clip) == id)
                return model::getClipRow (clip);

        return -1;
    };

    // Inserting moves the rows below down, with their clips and settings.
    h.call ("execute", Json::parse (R"({ "commands": [ { "type": "insertRow", "row": 1 } ] })"));
    CHECK (rowOf ("a") == 0);
    CHECK (rowOf ("b") == 6);
    CHECK (h.session.getProject().playlist.rows[4].name == "Drums");

    // Removing takes the row's clips and moves the ones below up.
    h.call ("execute", Json::parse (R"({ "commands": [ { "type": "removeRow", "row": 0 } ] })"));
    CHECK (rowOf ("a") == -1);
    CHECK (rowOf ("b") == 5);
    CHECK (h.session.getProject().playlist.rows[3].name == "Drums");

    // Moving rows takes their clips, names, mute and solo along; the rows in between close up.
    // Now: a in no row, b in 5, Drums (muted) in 3.
    h.call ("execute", Json::parse (R"({ "commands": [
        { "type": "addClip", "clip": { "type": "pattern", "id": "c", "row": 3, "start": 0, "length": 960, "pattern": "p" } },
        { "type": "addClip", "clip": { "type": "pattern", "id": "d", "row": 4, "start": 0, "length": 960, "pattern": "p" } },
        { "type": "updateRow", "row": 4, "changes": { "solo": true } } ] })"));
    REQUIRE (h.call ("execute", Json::parse (R"({ "commands": [ { "type": "moveRows", "row": 3, "count": 2, "to": 0 } ] })")).contains ("result"));
    CHECK (rowOf ("c") == 0);
    CHECK (rowOf ("d") == 1);
    CHECK (rowOf ("b") == 5);
    CHECK (h.session.getProject().playlist.rows[0].name == "Drums");
    CHECK (h.session.getProject().playlist.rows[0].mute);
    CHECK (h.session.getProject().playlist.rows[1].solo);
    CHECK (h.session.getProject().playlist.rows.size() == 2); // the empty rows after them are left out

    // And down again, past an empty row.
    h.call ("execute", Json::parse (R"({ "commands": [ { "type": "moveRows", "row": 0, "to": 2 } ] })"));
    CHECK (rowOf ("c") == 2);
    CHECK (rowOf ("d") == 0);
    CHECK (h.session.getProject().playlist.rows[2].name == "Drums");
    CHECK (h.session.getProject().playlist.rows[0].solo);
    CHECK (h.call ("execute", Json::parse (R"({ "commands": [ { "type": "moveRows", "row": 0, "count": 5, "to": 997 } ] })")).contains ("error"));
}

TEST_CASE ("The agent's conversations are kept, listed, reopened and deleted")
{
    const auto folder = test::tempDir() / "chats";
    std::filesystem::remove_all (folder);

    Harness h;
    h.handler.chatsFolder = folder;

    auto chat = [] (const std::string& id, const std::string& project)
    {
        return Json { { "id", id }, { "title", "Make it darker" }, { "project", project }, { "projectName", "Song" },
                      { "items", Json::array ({ { { "kind", "user" }, { "text", "Make it darker" } }, { { "kind", "assistant" }, { "text", "Done." } } }) },
                      { "history", Json::array ({ { { "role", "user" }, { "content", "Make it darker" } } }) } };
    };

    auto first = h.call ("chats.save", { { "chat", chat ("first", "/songs/a") } })["result"];
    CHECK (first["messages"] == 1);
    CHECK (first["created"].get<int64_t>() > 0);
    juce::Thread::sleep (5);
    h.call ("chats.save", { { "chat", chat ("second", "/songs/b") } });

    auto list = h.call ("chats.list")["result"]["chats"];
    REQUIRE (list.size() == 2);
    CHECK (list[0]["id"] == "second"); // most recent first
    CHECK (list[1]["project"] == "/songs/a");

    // Saving again keeps when it was created; the whole transcript comes back.
    auto again = chat ("first", "/songs/b");
    again["created"] = first["created"];
    juce::Thread::sleep (5);   // the list is ordered by when a chat was last saved, in milliseconds
    h.call ("chats.save", { { "chat", again } });
    auto loaded = h.call ("chats.get", { { "id", "first" } })["result"];
    CHECK (loaded["project"] == "/songs/b");
    CHECK (loaded["created"] == first["created"]);
    CHECK (loaded["items"].size() == 2);
    CHECK (loaded["history"][0]["content"] == "Make it darker");
    CHECK (h.call ("chats.list")["result"]["chats"][0]["id"] == "first");

    // The index is rebuilt from the files if it goes missing.
    std::filesystem::remove (folder / "index.json");
    CHECK (h.call ("chats.list")["result"]["chats"].size() == 2);

    CHECK (h.call ("chats.delete", { { "id", "second" } })["result"]["deleted"] == true);
    CHECK (h.call ("chats.list")["result"]["chats"].size() == 1);
    CHECK (h.call ("chats.get", { { "id", "second" } }).contains ("error"));
    CHECK (h.call ("chats.save", { { "chat", chat ("../escape", "/x") } }).contains ("error"));
}

TEST_CASE ("The user's notes for the agent are added, listed, changed and deleted")
{
    const auto file = test::tempDir() / "agent-notes.json";
    std::filesystem::remove (file);

    Harness h;
    h.handler.notesFile = file;

    auto loops = h.call ("notes.add", { { "text", "  Prefer loops over one-shots " } })["result"];
    CHECK (loops["text"] == "Prefer loops over one-shots");
    CHECK (loops["enabled"] == true);
    CHECK (loops["here"] == true);
    CHECK_FALSE (loops.contains ("project"));
    CHECK (h.broadcasts.back()["method"] == "notes");
    CHECK (h.broadcasts.back()["params"]["notes"].size() == 1);

    h.call ("notes.add", { { "text", "Techno here means hard, industrial, 140 BPM" } });
    CHECK (h.call ("notes.add", { { "text", "   " } }).contains ("error"));

    // This session has no project folder to keep a note for.
    CHECK (h.call ("notes.add", { { "text", "Keep the Digitakt kick" }, { "scope", "song" } }).contains ("error"));

    const auto id = loops["id"].get<std::string>();
    auto changed = h.call ("notes.update", { { "id", id }, { "text", "Prefer loops" }, { "enabled", false } })["result"];
    CHECK (changed["text"] == "Prefer loops");
    CHECK (changed["enabled"] == false);
    CHECK (changed["created"] == loops["created"]);

    auto list = h.call ("notes.list")["result"]["notes"];
    REQUIRE (list.size() == 2);
    CHECK (list[0]["id"] == id); // oldest first
    CHECK (list[0]["enabled"] == false);

    CHECK (h.call ("notes.update", { { "id", "nope" }, { "enabled", true } }).contains ("error"));
    CHECK (h.call ("notes.delete", { { "id", id } })["result"]["deleted"] == true);
    CHECK (h.call ("notes.delete", { { "id", id } })["result"]["deleted"] == false);
    CHECK (h.call ("notes.list")["result"]["notes"].size() == 1);

    // A note for one song applies there only.
    const auto folder = test::tempDir() / "notes-song";
    std::filesystem::remove_all (folder);
    auto session = Session::open (folder);
    ApiHandler songHandler (*session, test::getEngineContext());
    songHandler.notesFile = file;
    ApiHandler::Client client;

    auto kick = songHandler.call ("notes.add", { { "text", "Keep the Digitakt kick" }, { "scope", "song" } }, client);
    CHECK (kick["project"] == folder.string());
    CHECK (kick["projectName"] == "notes-song");
    CHECK (kick["here"] == true);

    list = h.call ("notes.list")["result"]["notes"];
    REQUIRE (list.size() == 2);
    CHECK (list[1]["here"] == false);

    auto everywhere = songHandler.call ("notes.update", { { "id", kick["id"] }, { "scope", "all" } }, client);
    CHECK_FALSE (everywhere.contains ("project"));
}

TEST_CASE ("Style guides are listed, read, edited, restored, and set as the song's style")
{
    const auto folder = test::tempDir() / "guides";
    std::filesystem::remove_all (folder);
    ::setenv ("GROOVEKIT_GUIDES", folder.string().c_str(), 1);

    Harness h;
    auto list = h.call ("guides.list")["result"];
    CHECK (list["guides"].size() == 12);
    CHECK (list["style"].is_null());

    // By id or by name.
    auto dnb = h.call ("guides.get", { { "id", "Drum & Bass" } })["result"];
    CHECK (dnb["id"] == "drum-and-bass");
    CHECK (dnb["builtIn"] == true);
    CHECK (dnb["targets"].contains ("tempo"));
    CHECK (h.call ("guides.get", { { "id", "polka" } }).contains ("error"));

    // By the other name a guide goes by: schranz is hard techno, Berghain is Berlin techno.
    CHECK (h.call ("guides.get", { { "id", "Schranz" } })["result"]["id"] == "hard-techno");
    CHECK (h.call ("guides.get", { { "id", "Berghain" } })["result"]["id"] == "berlin-techno");
    CHECK (h.call ("guides.get", { { "id", "tresor" } })["result"]["id"] == "industrial-techno");

    // No style yet, then the song's.
    CHECK (h.call ("guides.get").contains ("error"));
    CHECK (h.call ("execute", { { "commands", { { { "type", "setStyle" }, { "style", "Berlin Techno" } } } } }).contains ("result"));
    CHECK (h.call ("guides.get")["result"]["id"] == "berlin-techno");
    CHECK (h.call ("getSummary")["result"]["style"] == "berlin-techno");
    CHECK (h.call ("execute", { { "commands", { { { "type", "setStyle" }, { "style", "polka" } } } } }).contains ("error"));

    // The user's version replaces the built-in one until it's removed.
    auto text = h.call ("guides.get", { { "id", "berlin-techno" } })["result"]["text"].get<std::string>();
    const auto edited = text + "\nNo claps at all.\n";
    auto saved = h.call ("guides.save", { { "id", "berlin-techno" }, { "text", edited } })["result"];
    CHECK (saved["edited"] == true);
    CHECK (h.broadcasts.back()["method"] == "guides");
    CHECK (h.call ("guides.get", { { "id", "berlin-techno" } })["result"]["text"] == edited);
    CHECK (h.call ("guides.save", { { "id", "berlin-techno" }, { "text", "# Broken\n\n```targets\n{ nope\n```\n" } }).contains ("error"));
    CHECK (h.call ("guides.remove", { { "id", "berlin-techno" } })["result"]["removed"] == "restored");
    CHECK (h.call ("guides.get", { { "id", "berlin-techno" } })["result"]["text"] == text);

    // And guides of their own.
    auto own = h.call ("guides.save", { { "text", "# Dub techno\n\nDeep chords in fog.\n" } })["result"];
    CHECK (own["id"] == "dub-techno");
    CHECK (own["builtIn"] == false);
    CHECK (h.call ("guides.list")["result"]["guides"].size() == 13);
    CHECK (h.call ("guides.remove", { { "id", "dub-techno" } })["result"]["removed"] == "deleted");

    // Deleting from the list: one of their own goes, a built-in one is hidden (and still found
    // for songs in its style) until it's brought back.
    h.call ("guides.save", { { "text", "# Dub techno\n\nDeep chords in fog.\n" } });
    CHECK (h.call ("guides.delete", { { "id", "dub-techno" } })["result"]["removed"] == "deleted");
    CHECK (h.call ("guides.delete", { { "id", "trap" } })["result"]["removed"] == "hidden");
    auto hiddenList = h.call ("guides.list")["result"]["guides"];
    CHECK (std::any_of (hiddenList.begin(), hiddenList.end(), [] (auto& g) { return g["id"] == "trap" && g.value ("hidden", false); }));
    CHECK (h.call ("guides.get", { { "id", "trap" } })["result"]["id"] == "trap");
    h.call ("guides.unhide", { { "id", "trap" } });
    hiddenList = h.call ("guides.list")["result"]["guides"];
    CHECK (std::none_of (hiddenList.begin(), hiddenList.end(), [] (auto& g) { return g.value ("hidden", false); }));

    ::unsetenv ("GROOVEKIT_GUIDES");
}
