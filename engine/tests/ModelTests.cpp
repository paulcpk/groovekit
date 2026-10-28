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
            "schemaVersion": 5,
            "tempo": 120,
            "channels": [
                { "id": "kick", "instrument": { "type": "sampler", "layers": [ { "id": "a", "sample": "kick.wav" } ] } }
            ],
            "patterns": [
                { "id": "beat", "notes": [ { "channel": "kick", "start": 0, "length": 240 } ] }
            ],
            "playlist": {
                "rows": [],
                "clips": [ { "type": "pattern", "id": "c1", "row": 0, "start": 0, "length": 3840, "pattern": "beat" } ]
            },
            "mixer": { "inserts": [ { "id": "master" } ] }
        })");
    }

    /** The minimal project as schema 2 wrote it: tracks with ids. */
    Json schema2Project()
    {
        auto j = minimalProject();
        j["schemaVersion"] = 2;
        j["playlist"]["tracks"] = Json::parse (R"([ { "id": "t1" } ])");
        j["playlist"]["clips"][0]["track"] = "t1";
        return j;
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
        j["schemaVersion"] = 6;
        CHECK (readErrorPath (j) == "/schemaVersion");
    }
}

TEST_CASE ("Validation finds dangling references")
{
    auto j = minimalProject();
    j["patterns"][0]["notes"][0]["channel"] = "snare";
    j["playlist"]["clips"][0]["pattern"] = "nope";
    j["channels"][0]["insert"] = "drums";

    auto paths = issuePaths (parseProject (j));

    CHECK (paths == std::vector<std::string> {
        "/channels/0/insert",
        "/patterns/0/notes/0/channel",
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
        "type": "automation", "id": "a1", "row": 0, "start": 0, "length": 960,
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

TEST_CASE ("Songs from schema 1 get channels for their audio clips")
{
    auto j = schema2Project();
    j["schemaVersion"] = 1;
    auto& clips = j["playlist"]["clips"];
    clips.push_back ({ { "type", "audio" }, { "id", "a1" }, { "track", "t1" }, { "start", 0 }, { "length", 960 }, { "sample", "loops/Drum Loop.wav" }, { "insert", "master" } });
    clips.push_back ({ { "type", "audio" }, { "id", "a2" }, { "track", "t1" }, { "start", 960 }, { "length", 960 }, { "sample", "loops/Drum Loop.wav" } });
    clips.push_back ({ { "type", "audio" }, { "id", "a3" }, { "track", "t1" }, { "start", 1920 }, { "length", 960 }, { "sample", "loops/Drum Loop.wav" }, { "reverse", true } });

    CHECK (migrateProject (j));
    CHECK (j["schemaVersion"] == 5);
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

    auto& reversed = *getClipLayer (*findChannel (project, a3.channel));
    CHECK (reversed.sample == "loops/Drum Loop.wav");
    CHECK (reversed.reverse);
    CHECK (findChannel (project, a1.channel)->name == "Drum Loop");
    CHECK (findChannel (project, a1.channel)->group == ChannelGroup::arrangement);
}

TEST_CASE ("Schema 2 tracks become numbered lanes, and how they and their clips sounded moves to the channels")
{
    auto j = schema2Project();
    j["mixer"]["inserts"].push_back ({ { "id", "bus" } });
    j["playlist"]["tracks"] = Json::parse (R"([
        { "id": "t1", "name": "Beat" },
        { "id": "vox", "insert": "bus", "volumeDb": -6, "mute": true }
    ])");
    j["channels"].push_back (Json::parse (R"({ "id": "loop", "insert": "bus", "instrument": { "type": "audio", "sample": "loop.wav", "sourceTempo": 100,
                                               "envelope": { "attack": 0.1 }, "filter": { "type": "lowpass", "cutoff": 500 } } })"));
    auto& clips = j["playlist"]["clips"];
    clips.push_back (Json::parse (R"({ "type": "audio", "id": "a1", "track": "vox", "start": 0, "length": 960, "channel": "loop" })"));
    clips.push_back (Json::parse (R"({ "type": "audio", "id": "a2", "track": "vox", "start": 960, "length": 960, "channel": "loop", "gainDb": 2, "pan": 0.5 })"));
    clips.push_back (Json::parse (R"({ "type": "automation", "id": "fade", "track": "t1", "start": 0, "length": 960,
                                       "target": { "scope": "channel", "channel": "loop", "param": "volume" }, "points": [ { "time": 0, "value": 0 } ] })"));

    CHECK (migrateProject (j));
    auto project = parseProject (j);
    CHECK (issuePaths (project).empty());

    REQUIRE (project.playlist.rows.size() == 2);
    CHECK (project.playlist.rows[0].name == "Beat");
    CHECK (project.playlist.rows[1].mute);
    CHECK (getClipRow (project.playlist.clips[0]) == 0);
    CHECK (getClipRow (project.playlist.clips[1]) == 1);

    // The audio channel is a sampler whose first layer holds the file and its settings.
    auto& loop = *findChannel (project, "loop");
    auto& sampler = std::get<SamplerInstrument> (loop.instrument);
    REQUIRE (sampler.layers.size() == 1);
    CHECK (sampler.layers[0].sample == "loop.wav");
    CHECK (sampler.layers[0].sourceTempo == 100);
    CHECK (sampler.layers[0].envelope.attack == 0.1);
    CHECK (sampler.filter.cutoff == 500);
    CHECK (loop.group == ChannelGroup::arrangement);

    // The first clip's sound (the track's level) goes onto the channel; the second clip, louder
    // and panned, gets a copy, which follows the channel's automation too.
    CHECK (sampler.layers[0].gainDb == -6);
    auto& a2 = std::get<AudioClip> (project.playlist.clips[2]);
    CHECK (a2.channel == "loop-2");
    auto& copy = *findChannel (project, "loop-2");
    CHECK (getClipLayer (copy)->gainDb == -4);
    CHECK (getClipLayer (copy)->pan == 0.5);
    CHECK (copy.insert == "bus");

    auto& followed = std::get<AutomationClip> (project.playlist.clips.back());
    CHECK (followed.target.channel == "loop-2");
    CHECK (followed.row == 0);
}

TEST_CASE ("Audio clips play a sampler's first layer")
{
    auto j = minimalProject();
    j["channels"].push_back (Json::parse (R"({ "id": "synth", "instrument": { "type": "plugin", "plugin": { "format": "VST3", "uid": "x", "name": "Synth" } } })"));
    j["channels"].push_back (Json::parse (R"({ "id": "empty", "instrument": { "type": "sampler", "layers": [] } })"));
    j["playlist"]["clips"].push_back (Json::parse (R"({ "type": "audio", "id": "a", "row": 2, "start": 0, "length": 960, "channel": "kick" })"));
    j["playlist"]["clips"].push_back (Json::parse (R"({ "type": "audio", "id": "b", "row": 2, "start": 0, "length": 960, "channel": "synth" })"));
    j["playlist"]["clips"].push_back (Json::parse (R"({ "type": "audio", "id": "c", "row": 2, "start": 0, "length": 960, "channel": "empty" })"));

    CHECK (issuePaths (parseProject (j)) == std::vector<std::string> { "/playlist/clips/2/channel", "/playlist/clips/3/channel" });
}

TEST_CASE ("Schema 1 effects carry over to the new EQ, reverb and delay")
{
    auto j = schema2Project();
    j["schemaVersion"] = 1;
    j["mixer"]["inserts"][0]["effects"] = {
        { { "type", "builtin" }, { "id", "eq" }, { "kind", "eq" }, { "params", { { "lowGainDb", 3 }, { "highFreq", 9000 } } } },
        { { "type", "builtin" }, { "id", "verb" }, { "kind", "reverb" }, { "params", { { "roomSize", 0.5 }, { "wet", 1 }, { "dry", 3 } } } },
        { { "type", "builtin" }, { "id", "echo" }, { "kind", "delay" }, { "params", { { "timeMs", 300 }, { "feedbackDb", -6 } } } },
    };
    j["playlist"]["clips"].push_back ({ { "type", "automation" }, { "id", "auto" }, { "track", "t1" }, { "start", 0 }, { "length", 960 },
                                        { "target", { { "scope", "effect" }, { "insert", "master" }, { "effect", "verb" }, { "param", "roomSize" } } },
                                        { "points", { { { "time", 0 }, { "value", 0.2 } } } } });

    migrateProject (j);
    auto project = parseProject (j);
    CHECK (issuePaths (project).empty());

    auto& effects = project.mixer.inserts[0].effects;
    auto& eq = std::get<BuiltinEffect> (effects[0]);
    CHECK (eq.params == std::map<std::string, double> { { "band1GainDb", 3 }, { "band7Freq", 9000 } });

    auto& verb = std::get<BuiltinEffect> (effects[1]);
    CHECK (verb.mix == 0.25);
    CHECK (verb.params.at ("size") == 50);

    auto& echo = std::get<BuiltinEffect> (effects[2]);
    CHECK (echo.mix == 0.3);
    CHECK (echo.params.at ("sync") == 0);
    CHECK (echo.params.at ("timeMs") == 300);
    CHECK (echo.params.at ("feedback") == 50);

    auto& automation = std::get<AutomationClip> (project.playlist.clips.back());
    CHECK (automation.target.param == "size");
    CHECK (automation.points[0].value == 20);
}

TEST_CASE ("Songs from schema 3 follow the keys played where they sound the same")
{
    auto j = minimalProject();
    j["schemaVersion"] = 3;
    auto& channels = j["channels"];
    channels[0]["instrument"]["layers"][0]["pitchTracking"] = false;
    channels[0]["instrument"]["layers"][0]["rootKey"] = 31;

    // A clap played on D2 only, a hat on two keys, a pad that follows already, and a channel
    // with no notes.
    channels.push_back (Json::parse (R"({ "id": "clap", "instrument": { "type": "sampler", "layers": [ { "id": "a", "sample": "clap.wav", "pitchTracking": false } ] } })"));
    channels.push_back (Json::parse (R"({ "id": "hat", "instrument": { "type": "sampler", "layers": [ { "id": "a", "sample": "hat.wav", "pitchTracking": false } ] } })"));
    channels.push_back (Json::parse (R"({ "id": "pad", "instrument": { "type": "sampler", "layers": [ { "id": "a", "sample": "pad.wav", "rootKey": 48 } ] } })"));
    channels.push_back (Json::parse (R"({ "id": "loop", "instrument": { "type": "sampler", "layers": [ { "id": "a", "sample": "loop.wav", "pitchTracking": false, "rootKey": 57 } ] } })"));

    auto& notes = j["patterns"][0]["notes"];
    notes.push_back ({ { "channel", "clap" }, { "start", 960 }, { "length", 240 }, { "key", 38 } });
    notes.push_back ({ { "channel", "hat" }, { "start", 0 }, { "length", 240 }, { "key", 60 } });
    notes.push_back ({ { "channel", "hat" }, { "start", 480 }, { "length", 240 }, { "key", 62 } });
    notes.push_back ({ { "channel", "pad" }, { "start", 0 }, { "length", 960 }, { "key", 55 } });

    CHECK (migrateProject (j));
    CHECK (j["schemaVersion"] == 5);
    const auto layer = [&] (int channel) { return j["channels"][static_cast<size_t> (channel)]["instrument"]["layers"][0]; };

    // The kick's notes are on C4 (the default key): it now plays on C4 at its own pitch.
    CHECK_FALSE (layer (0).contains ("pitchTracking"));
    CHECK_FALSE (layer (0).contains ("rootKey"));
    CHECK_FALSE (layer (1).contains ("pitchTracking"));
    CHECK (layer (1)["rootKey"] == 38);
    CHECK (layer (2)["pitchTracking"] == false);
    CHECK (layer (3)["rootKey"] == 48);
    CHECK_FALSE (layer (4).contains ("pitchTracking"));
    CHECK (layer (4)["rootKey"] == 57);

    CHECK (issuePaths (parseProject (j)).empty());
    CHECK_FALSE (migrateProject (j));
}

TEST_CASE ("Schema 4 lanes become rows")
{
    auto j = minimalProject();
    j["schemaVersion"] = 4;
    j["playlist"].erase ("rows");
    j["playlist"]["tracks"] = Json::parse (R"([ {}, { "name": "Drums", "mute": true } ])");
    j["playlist"]["clips"][0].erase ("row");
    j["playlist"]["clips"][0]["track"] = 1;

    CHECK (migrateProject (j));
    CHECK (j["schemaVersion"] == 5);
    CHECK_FALSE (j["playlist"].contains ("tracks"));
    CHECK (j["playlist"]["rows"][1]["name"] == "Drums");
    CHECK (j["playlist"]["clips"][0]["row"] == 1);
    CHECK_FALSE (j["playlist"]["clips"][0].contains ("track"));

    auto project = parseProject (j);
    CHECK (getClipRow (project.playlist.clips[0]) == 1);
    CHECK (findRow (project, 1)->mute);
}
