#include "TestHelpers.h"

#include "session/Session.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <random>

using namespace gk;
using model::Json;
using Catch::Matchers::ContainsSubstring;

namespace
{
    model::Command command (const std::string& json)
    {
        model::Reader reader;
        model::Command c;
        readValue (reader, Json::parse (json), c);
        return c;
    }

    void run (Session& s, std::initializer_list<const char*> commands, ExecuteOptions options = {})
    {
        std::vector<model::Command> list;

        for (auto c : commands)
            list.push_back (command (c));

        s.execute (list, options);
    }

    std::string errorOf (Session& s, const char* json)
    {
        try
        {
            s.execute ({ command (json) });
        }
        catch (const CommandError& e)
        {
            return e.what();
        }

        return "<no error>";
    }

    constexpr const char* beepChannel = R"({ "type": "addChannel", "channel": {
        "id": "beep", "instrument": { "type": "sampler", "layers": [ { "id": "l", "sample": "beep.wav" } ] } } })";
}

TEST_CASE ("Commands change the document and record their inverse")
{
    Session s (Session::createEmptyProject());
    const auto initial = s.getDocument();

    run (s, { R"({ "type": "setTempo", "tempo": 128 })" });
    CHECK (s.getProject().tempo == 128);
    CHECK (s.getHistory().size() == 1);
    CHECK (s.getHistory()[0].label == "Set tempo to 128");

    REQUIRE (s.undo());
    CHECK (s.getDocument() == initial);
    CHECK (s.getProject().tempo == 120);

    REQUIRE (s.redo());
    CHECK (s.getProject().tempo == 128);
}

TEST_CASE ("Channels, patterns, notes and clips")
{
    Session s (Session::createEmptyProject());

    run (s, {
        beepChannel,
        R"({ "type": "addPattern", "pattern": { "id": "p", "notes": [] } })",
        R"({ "type": "addNotes", "pattern": "p", "notes": [
                { "channel": "beep", "start": 0, "length": 240 },
                { "channel": "beep", "start": 960, "length": 240, "key": 62 } ] })",
        R"({ "type": "addTrack", "track": { "id": "t" } })",
        R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "track": "t", "start": 0, "length": 3840, "pattern": "p" } })",
    });

    auto& project = s.getProject();
    REQUIRE (project.channels.size() == 1);
    REQUIRE (project.patterns[0].notes.size() == 2);
    CHECK (project.playlist.clips.size() == 1);

    SECTION ("updates merge fields")
    {
        run (s, { R"({ "type": "updateChannel", "channel": "beep", "changes": { "volumeDb": -6, "color": "#ff0000" } })" });
        CHECK (s.getProject().channels[0].volumeDb == -6);
        CHECK (s.getProject().channels[0].color == "#ff0000");

        run (s, { R"({ "type": "updateChannel", "channel": "beep", "changes": { "color": null } })" });
        CHECK (! s.getProject().channels[0].color);
    }

    SECTION ("notes can be filtered and replaced")
    {
        run (s, { R"({ "type": "removeNotes", "pattern": "p", "key": 62 })" });
        CHECK (s.getProject().patterns[0].notes.size() == 1);

        run (s, { R"({ "type": "setNotes", "pattern": "p", "channel": "beep", "notes": [
                      { "channel": "beep", "start": 0, "length": 120 },
                      { "channel": "beep", "start": 480, "length": 120 },
                      { "channel": "beep", "start": 960, "length": 120 } ] })" });
        CHECK (s.getProject().patterns[0].notes.size() == 3);
    }

    SECTION ("removing a channel removes its notes")
    {
        run (s, { R"({ "type": "removeChannel", "channel": "beep" })" });
        CHECK (s.getProject().channels.empty());
        CHECK (s.getProject().patterns[0].notes.empty());
    }

    SECTION ("removing a pattern removes its clips")
    {
        run (s, { R"({ "type": "removePattern", "pattern": "p" })" });
        CHECK (s.getProject().playlist.clips.empty());
    }

    SECTION ("a clip can be moved but not change type")
    {
        run (s, { R"({ "type": "updateClip", "clip": "c", "changes": { "start": 960 } })" });
        CHECK (model::getClipStart (s.getProject().playlist.clips[0]) == 960);
        CHECK_THAT (errorOf (s, R"({ "type": "updateClip", "clip": "c", "changes": { "type": "audio" } })"),
                    ContainsSubstring ("can't be changed"));
    }
}

TEST_CASE ("Removing an insert reroutes to master and drops sends and automation")
{
    Session s (Session::createEmptyProject());

    run (s, {
        R"({ "type": "addInsert", "insert": { "id": "drums", "effects": [ { "type": "builtin", "id": "f", "kind": "lowpass" } ] } })",
        R"({ "type": "addInsert", "insert": { "id": "verb" } })",
        R"({ "type": "addSend", "insert": "drums", "send": { "id": "s", "target": "verb" } })",
        R"({ "type": "addInsert", "insert": { "id": "sub", "output": "drums" } })",
        beepChannel,
        R"({ "type": "updateChannel", "channel": "beep", "changes": { "insert": "drums" } })",
        R"({ "type": "addTrack", "track": { "id": "t" } })",
        R"({ "type": "addClip", "clip": { "type": "automation", "id": "a", "track": "t", "start": 0, "length": 960,
              "target": { "scope": "effect", "insert": "drums", "effect": "f", "param": "frequency" },
              "points": [ { "time": 0, "value": 200 } ] } })",
    });

    run (s, { R"({ "type": "removeInsert", "insert": "verb" })" });
    CHECK (model::findInsert (s.getProject(), "drums")->sends.empty());

    run (s, { R"({ "type": "removeInsert", "insert": "drums" })" });
    CHECK (s.getProject().channels[0].insert == "master");
    CHECK (model::findInsert (s.getProject(), "sub")->output == "master");
    CHECK (s.getProject().playlist.clips.empty());

    CHECK_THAT (errorOf (s, R"({ "type": "removeInsert", "insert": "master" })"), ContainsSubstring ("can't be removed"));
}

TEST_CASE ("Effect params merge and effects can be reordered")
{
    Session s (Session::createEmptyProject());

    run (s, {
        R"({ "type": "addEffect", "insert": "master", "effect": { "type": "builtin", "id": "eq", "kind": "eq", "params": { "band1GainDb": 3 } } })",
        R"({ "type": "addEffect", "insert": "master", "effect": { "type": "builtin", "id": "comp", "kind": "compressor" } })",
        R"({ "type": "updateEffect", "insert": "master", "effect": "eq", "changes": { "params": { "band7GainDb": -2 }, "bypass": true } })",
        R"({ "type": "moveEffect", "insert": "master", "effect": "comp", "index": 0 })",
    });

    auto& effects = model::findInsert (s.getProject(), "master")->effects;
    REQUIRE (effects.size() == 2);
    CHECK (model::getEffectId (effects[0]) == "comp");

    auto& eq = std::get<model::BuiltinEffect> (effects[1]);
    CHECK (eq.params == std::map<std::string, double> { { "band1GainDb", 3 }, { "band7GainDb", -2 } });
    CHECK (eq.bypass);
}

TEST_CASE ("Invalid results roll back the whole transaction")
{
    Session s (Session::createEmptyProject());
    run (s, { beepChannel });
    const auto before = s.getDocument();

    CHECK_THAT (errorOf (s, R"({ "type": "updateChannel", "channel": "beep", "changes": { "volme": 1 } })"),
                ContainsSubstring ("unknown property \"volme\""));
    CHECK_THAT (errorOf (s, R"({ "type": "updateChannel", "channel": "beep", "changes": { "insert": "nowhere" } })"),
                ContainsSubstring ("no mixer insert \"nowhere\""));
    CHECK_THAT (errorOf (s, R"({ "type": "updateChannel", "channel": "nope", "changes": {} })"),
                ContainsSubstring ("no channel \"nope\""));
    CHECK_THAT (errorOf (s, beepChannel), ContainsSubstring ("already a channel \"beep\""));
    CHECK_THAT (errorOf (s, R"({ "type": "patch", "ops": [ { "op": "remove", "path": "/mixer/inserts/0" } ] })"),
                ContainsSubstring ("/mixer/inserts"));

    // Several commands are all-or-nothing.
    CHECK_THROWS_AS (s.execute ({ command (R"({ "type": "setTempo", "tempo": 90 })"),
                                  command (R"({ "type": "removeClip", "clip": "missing" })") }),
                     CommandError);

    CHECK (s.getDocument() == before);
    CHECK (s.getHistory().size() == 1);
}

TEST_CASE ("Raw patches work and are recorded with real indices")
{
    Session s (Session::createEmptyProject());
    run (s, { R"({ "type": "patch", "ops": [
        { "op": "add", "path": "/mixer/inserts/-", "value": { "id": "bus" } },
        { "op": "replace", "path": "/tempo", "value": 100 } ] })" });

    CHECK (s.getProject().mixer.inserts.size() == 2);
    CHECK (s.getHistory()[0].forward[0].path == "/mixer/inserts/1");
}

TEST_CASE ("Groups undo and redo together")
{
    Session s (Session::createEmptyProject());
    run (s, { R"({ "type": "setTempo", "tempo": 100 })" });
    run (s, { R"({ "type": "setTempo", "tempo": 110 })" }, { "agent", "request-1", {} });
    run (s, { R"({ "type": "renameProject", "name": "Agent song" })" }, { "agent", "request-1", {} });

    REQUIRE (s.undo());
    CHECK (s.getProject().tempo == 100);
    CHECK (s.getProject().name.empty());

    REQUIRE (s.redo());
    CHECK (s.getProject().tempo == 110);
    CHECK (s.getProject().name == "Agent song");

    REQUIRE (s.undo (false));
    CHECK (s.getProject().tempo == 110);
    CHECK (s.getProject().name.empty());
}

TEST_CASE ("Changing something after an undo starts a branch; redo follows the newest")
{
    Session s (Session::createEmptyProject());
    run (s, { R"({ "type": "setTempo", "tempo": 100 })" });
    run (s, { R"({ "type": "setTempo", "tempo": 110 })" });
    s.undo();
    run (s, { R"({ "type": "setTempo", "tempo": 140 })" });

    CHECK (s.getHistory().size() == 3);
    CHECK (s.getHistory()[2].parent == 1);

    s.undo();
    CHECK (s.getProject().tempo == 100);
    s.redo();
    CHECK (s.getProject().tempo == 140);
    CHECK_FALSE (s.canRedo());
}

TEST_CASE ("Listeners get patches that keep a mirror in sync")
{
    Session s (Session::createEmptyProject());
    auto mirror = s.getDocument();
    int changes = 0;

    s.addListener ([&] (const Change& c)
    {
        applyPatch (mirror, c.ops);
        ++changes;
    });

    run (s, { beepChannel, R"({ "type": "addInsert", "insert": { "id": "bus" } })" });
    run (s, { R"({ "type": "updateChannel", "channel": "beep", "changes": { "insert": "bus" } })" });
    s.undo();
    s.redo();
    run (s, { R"({ "type": "removeInsert", "insert": "bus" })" });

    CHECK (changes == 5);
    CHECK (mirror == s.getDocument());
}

TEST_CASE ("Sessions persist the song and history to their folder")
{
    auto dir = test::tempDir() / "session-persistence";
    std::filesystem::remove_all (dir);

    {
        auto s = Session::open (dir);
        run (*s, { R"({ "type": "setTempo", "tempo": 100 })" });
        run (*s, { R"({ "type": "setTempo", "tempo": 110 })" });
        s->undo();
    }

    auto s = Session::open (dir);
    CHECK (s->getProject().tempo == 100);
    CHECK (s->getHistory().size() == 2);
    CHECK (s->getHead() == 1);

    REQUIRE (s->redo());
    CHECK (s->getProject().tempo == 110);
}

TEST_CASE ("Random command sequences undo back to the start")
{
    std::mt19937 rng (1234);
    auto pick = [&] (int n) { return static_cast<int> (rng() % static_cast<unsigned> (n)); };

    for (int round = 0; round < 20; ++round)
    {
        Session s (Session::createEmptyProject());
        const auto initial = s.getDocument();
        int applied = 0;

        for (int i = 0; i < 60; ++i)
        {
            const auto n = std::to_string (pick (4));
            const auto m = std::to_string (pick (4));
            std::string c;

            switch (pick (14))
            {
                case 0:  c = R"({ "type": "addChannel", "channel": { "id": "ch)" + n + R"(", "instrument": { "type": "sampler", "layers": [] } } })"; break;
                case 1:  c = R"({ "type": "removeChannel", "channel": "ch)" + n + R"(" })"; break;
                case 2:  c = R"({ "type": "updateChannel", "channel": "ch)" + n + R"(", "changes": { "volumeDb": -)" + m + R"(, "insert": "bus)" + m + R"(" } })"; break;
                case 3:  c = R"({ "type": "addPattern", "pattern": { "id": "p)" + n + R"(", "notes": [] } })"; break;
                case 4:  c = R"({ "type": "addNotes", "pattern": "p)" + n + R"(", "notes": [ { "channel": "ch)" + m + R"(", "start": )" + std::to_string (pick (8) * 240) + R"(, "length": 240 } ] })"; break;
                case 5:  c = R"({ "type": "removeNotes", "pattern": "p)" + n + R"(", "channel": "ch)" + m + R"(" })"; break;
                case 6:  c = R"({ "type": "removePattern", "pattern": "p)" + n + R"(" })"; break;
                case 7:  c = R"({ "type": "addTrack", "track": { "id": "t)" + n + R"(" } })"; break;
                case 8:  c = R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c)" + n + m + R"(", "track": "t)" + n + R"(", "start": 0, "length": 960, "pattern": "p)" + m + R"(" } })"; break;
                case 9:  c = R"({ "type": "removeTrack", "track": "t)" + n + R"(" })"; break;
                case 10: c = R"({ "type": "addInsert", "insert": { "id": "bus)" + n + R"(" } })"; break;
                case 11: c = R"({ "type": "removeInsert", "insert": "bus)" + n + R"(" })"; break;
                case 12: c = R"({ "type": "addSend", "insert": "bus)" + n + R"(", "send": { "id": "s)" + m + R"(", "target": "bus)" + m + R"(" } })"; break;
                case 13: c = R"({ "type": "setTempo", "tempo": )" + std::to_string (60 + pick (100)) + " }"; break;
            }

            try
            {
                s.execute ({ command (c) });
                ++applied;
            }
            catch (const CommandError&)
            {
                // Plenty of random commands are invalid; they must leave nothing behind.
            }
        }

        const auto final = s.getDocument();

        while (s.undo())
        {}

        INFO ("round " << round << ", " << applied << " commands applied");
        CHECK (s.getDocument() == initial);

        while (s.redo())
        {}

        CHECK (s.getDocument() == final);
    }
}
