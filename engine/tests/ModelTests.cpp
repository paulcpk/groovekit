#include "model/Project.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace gk::model;
using Catch::Matchers::ContainsSubstring;

namespace
{
    Json minimalProject()
    {
        return Json::parse (R"({
            "schemaVersion": 2,
            "tempo": 120,
            "channels": [
                { "id": "kick", "instrument": { "type": "sampler", "layers": [ { "id": "a", "sample": "kick.wav" } ] } }
            ],
            "patterns": [
                { "id": "beat", "notes": [ { "channel": "kick", "start": 0, "length": 240 } ] }
            ],
            "playlist": {
                "tracks": [ { "id": "t1" } ],
                "clips": [ { "type": "pattern", "id": "c1", "track": "t1", "start": 0, "length": 3840, "pattern": "beat" } ]
            },
            "mixer": { "inserts": [ { "id": "master" } ] }
        })");
    }

    std::string readErrorPath (const Json& j)
    {
        try
        {
            parseProject (j);
        }
        catch (const ReadError& e)
        {
            return e.path;
        }

        return "<no error>";
    }

    std::vector<std::string> issuePaths (const Project& p)
    {
        std::vector<std::string> paths;

        for (auto& i : validate (p))
            paths.push_back (i.path);

        return paths;
    }
}

TEST_CASE ("A minimal project parses and gets defaults")
{
    auto project = parseProject (minimalProject());

    CHECK (project.tempo == 120);
    CHECK (project.timeSignature.numerator == 4);
    CHECK (project.channels[0].insert == "master");
    CHECK (project.channels[0].volumeDb == 0);

    auto& sampler = std::get<SamplerInstrument> (project.channels[0].instrument);
    CHECK (sampler.polyphony == 32);
    CHECK (sampler.layers[0].rootKey == 60);
    CHECK (sampler.layers[0].keyRange.high == 127);
    CHECK (sampler.layers[0].mode == SamplerLayerMode::oneShot);
    CHECK (sampler.layers[0].envelope.release == 0.01);

    auto& note = project.patterns[0].notes[0];
    CHECK (note.key == 60);
    CHECK (note.velocity == 100);

    CHECK (validate (project).empty());
}

TEST_CASE ("Projects round-trip through JSON")
{
    auto project = parseProject (minimalProject());
    auto again = parseProject (projectToJson (project));

    CHECK (again == project);
    CHECK (projectToJson (again) == projectToJson (project));
}

TEST_CASE ("Schema errors report a JSON pointer")
{
    auto j = minimalProject();

    SECTION ("unknown property")
    {
        j["channels"][0]["volme"] = 1;
        CHECK (readErrorPath (j) == "/channels/0");
    }

    SECTION ("out of range")
    {
        j["channels"][0]["instrument"]["layers"][0]["rootKey"] = 128;
        CHECK (readErrorPath (j) == "/channels/0/instrument/layers/0/rootKey");
    }

    SECTION ("wrong type")
    {
        j["patterns"][0]["notes"][0]["start"] = 1.5;
        CHECK (readErrorPath (j) == "/patterns/0/notes/0/start");
    }

    SECTION ("missing required property")
    {
        j["playlist"]["clips"][0].erase ("pattern");
        CHECK (readErrorPath (j) == "/playlist/clips/0");
    }

    SECTION ("unknown union type")
    {
        j["playlist"]["clips"][0]["type"] = "video";
        CHECK (readErrorPath (j) == "/playlist/clips/0");
    }

    SECTION ("bad id")
    {
        j["channels"][0]["id"] = "has space";
        CHECK (readErrorPath (j) == "/channels/0/id");
    }

    SECTION ("wrong schema version")
    {
        j["schemaVersion"] = 3;
        CHECK (readErrorPath (j) == "/schemaVersion");
    }
}

TEST_CASE ("Validation finds dangling references")
{
    auto j = minimalProject();
    j["patterns"][0]["notes"][0]["channel"] = "snare";
    j["playlist"]["clips"][0]["pattern"] = "nope";
    j["playlist"]["clips"][0]["track"] = "t9";
    j["channels"][0]["insert"] = "drums";

    auto paths = issuePaths (parseProject (j));

    CHECK (paths == std::vector<std::string> {
        "/channels/0/insert",
        "/patterns/0/notes/0/channel",
        "/playlist/clips/0/track",
        "/playlist/clips/0/pattern",
    });
}

TEST_CASE ("Validation finds duplicate ids")
{
    auto j = minimalProject();
    j["channels"].push_back (j["channels"][0]);

    CHECK (issuePaths (parseProject (j)) == std::vector<std::string> { "/channels/1/id" });
}

TEST_CASE ("Validation requires a master insert")
{
    auto j = minimalProject();
    j["mixer"]["inserts"][0]["id"] = "main";
    j["channels"][0]["insert"] = "main";

    auto issues = validate (parseProject (j));
    REQUIRE (issues.size() == 2); // missing master, and main's default output points at it
    CHECK (issues[0].message == "a \"master\" insert is required");
    CHECK (issues[1].path == "/mixer/inserts/0/output");
}

TEST_CASE ("Validation finds routing cycles through outputs and sends")
{
    auto j = minimalProject();
    j["mixer"]["inserts"].push_back (Json::parse (R"({ "id": "a", "output": "b" })"));
    j["mixer"]["inserts"].push_back (Json::parse (R"({ "id": "b", "sends": [ { "id": "s", "target": "a" } ] })"));

    auto issues = validate (parseProject (j));
    REQUIRE (issues.size() == 1);
    CHECK_THAT (issues[0].message, ContainsSubstring ("routing cycle: a -> b -> a"));
}

TEST_CASE ("Sends to other inserts are fine")
{
    auto j = minimalProject();
    j["mixer"]["inserts"].push_back (Json::parse (R"({ "id": "reverb" })"));
    j["mixer"]["inserts"].push_back (Json::parse (R"({ "id": "drums", "sends": [ { "id": "s", "target": "reverb", "levelDb": -6 } ] })"));

    CHECK (validate (parseProject (j)).empty());
}

TEST_CASE ("Automation targets are checked")
{
    auto j = minimalProject();
    j["playlist"]["clips"].push_back (Json::parse (R"({
        "type": "automation", "id": "a1", "track": "t1", "start": 0, "length": 960,
        "target": { "scope": "channel", "channel": "kick", "param": "cutoff" },
        "points": [ { "time": 0, "value": 0 } ]
    })"));

    CHECK (issuePaths (parseProject (j)) == std::vector<std::string> { "/playlist/clips/1/target/param" });
}

TEST_CASE ("Pattern length defaults to whole bars")
{
    TimeSignature fourFour;
    Pattern p;
    p.notes.push_back ({ "kick", 0, 240 });
    CHECK (getPatternLength (p, fourFour) == 3840);

    p.notes.push_back ({ "kick", 3840, 240 });
    CHECK (getPatternLength (p, fourFour) == 7680);

    p.length = 1920;
    CHECK (getPatternLength (p, fourFour) == 1920);

    CHECK (getPatternLength (Pattern {}, TimeSignature { 3, 4 }) == 2880);
}

TEST_CASE ("Songs from schema 1 get audio channels for their audio clips")
{
    auto j = minimalProject();
    j["schemaVersion"] = 1;
    auto& clips = j["playlist"]["clips"];
    clips.push_back ({ { "type", "audio" }, { "id", "a1" }, { "track", "t1" }, { "start", 0 }, { "length", 960 }, { "sample", "loops/Drum Loop.wav" }, { "insert", "master" } });
    clips.push_back ({ { "type", "audio" }, { "id", "a2" }, { "track", "t1" }, { "start", 960 }, { "length", 960 }, { "sample", "loops/Drum Loop.wav" } });
    clips.push_back ({ { "type", "audio" }, { "id", "a3" }, { "track", "t1" }, { "start", 1920 }, { "length", 960 }, { "sample", "loops/Drum Loop.wav" }, { "reverse", true } });

    CHECK (migrateProject (j));
    CHECK (j["schemaVersion"] == 2);
    CHECK_FALSE (migrateProject (j));

    auto project = parseProject (j);
    CHECK (issuePaths (project).empty());
    REQUIRE (project.channels.size() == 3);

    // The same file played the same way shares a channel; reversed is another.
    auto& a1 = std::get<AudioClip> (project.playlist.clips[1]);
    auto& a2 = std::get<AudioClip> (project.playlist.clips[2]);
    auto& a3 = std::get<AudioClip> (project.playlist.clips[3]);
    CHECK (a1.channel == "Drum-Loop");
    CHECK (a2.channel == a1.channel);
    CHECK (a3.channel == "Drum-Loop-2");

    auto& reversed = std::get<AudioInstrument> (findChannel (project, a3.channel)->instrument);
    CHECK (reversed.sample == "loops/Drum Loop.wav");
    CHECK (reversed.reverse);
    CHECK (findChannel (project, a1.channel)->name == "Drum Loop");
}

TEST_CASE ("Notes can't play audio channels")
{
    auto j = minimalProject();
    j["channels"][0]["instrument"] = { { "type", "audio" }, { "sample", "kick.wav" } };
    CHECK (issuePaths (parseProject (j)) == std::vector<std::string> { "/patterns/0/notes/0/channel" });
}
