// The rhythm machines' link with the song: their step keys edit GrooveKit's patterns.

#include "TestHelpers.h"

#include "analysis/Analysis.h"
#include "live/LiveEngine.h"
#include "plugins/MachineBridge.h"
#include "plugins/PluginCatalog.h"

#include "common/GrooveKitMachine.h"

#include <catch2/catch_test_macros.hpp>

using namespace gk;
using model::Json;
namespace te = tracktion;

namespace
{
    model::Command command (const std::string& json)
    {
        model::Reader reader;
        model::Command c;
        readValue (reader, Json::parse (json), c);
        return c;
    }

    void run (Session& s, std::initializer_list<std::string> commands)
    {
        std::vector<model::Command> list;

        for (auto& c : commands)
            list.push_back (command (c));

        s.execute (list);
    }

    /** Stands in for a machine: keeps what GrooveKit shows it and the host it's given. */
    struct FakeMachine : public gkmachine::IMachine
    {
        Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID, void** obj) override     { *obj = nullptr; return Steinberg::kNoInterface; }
        Steinberg::uint32 PLUGIN_API addRef() override      { return static_cast<Steinberg::uint32> (++references); }
        Steinberg::uint32 PLUGIN_API release() override     { return static_cast<Steinberg::uint32> (--references); }

        gkmachine::int32 PLUGIN_API getVersion() override   { return gkmachine::version; }
        void PLUGIN_API setHost (gkmachine::IHost* h) override  { host = h; }

        void PLUGIN_API showPattern (const gkmachine::Pattern& p) override
        {
            name = p.name;
            exists = p.exists != 0;
            stepTicks = p.stepTicks;
            lengthSteps = p.lengthSteps;
            swing = p.swing;
            hits.assign (p.hits, p.hits + p.numHits);
            std::sort (hits.begin(), hits.end(), [] (auto& a, auto& b) { return std::tie (a.step, a.key) < std::tie (b.step, b.key); });
        }

        void PLUGIN_API setPlayhead (gkmachine::int32 s, gkmachine::int32 p) override   { step = s; playing = p != 0; }

        void PLUGIN_API showNotes (const gkmachine::Note* n, gkmachine::int32 count) override
        {
            notes.assign (n, n + count);
            std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return std::tie (a.start, a.key) < std::tie (b.start, b.key); });
        }

        int references = 1;
        gkmachine::IHost* host = nullptr;
        std::string name;
        bool exists = false, playing = false;
        int stepTicks = 0, lengthSteps = 0, step = -2;
        float swing = 0;
        std::vector<gkmachine::Hit> hits;
        std::vector<gkmachine::Note> notes;
    };

    bool same (const gkmachine::Hit& h, int key, int step, int velocity, bool flam)
    {
        return h.key == key && h.step == step && h.velocity == velocity && (h.flam != 0) == flam;
    }

    /** Stands in for an audio interface, so the live Edit plays block by block. */
    struct HostedAudio
    {
        static constexpr double rate = 48000;
        static constexpr int block = 512;
        te::Engine& engine;
        te::HostedAudioDeviceInterface& io;

        HostedAudio() : engine (test::getEngine()), io (engine.getDeviceManager().getHostedAudioDeviceInterface())
        {
            te::HostedAudioDeviceInterface::Parameters p;
            p.sampleRate = rate;
            p.blockSize = block;
            p.inputChannels = 0;
            p.outputChannels = 2;
            io.initialise (p);
            io.prepareToPlay (rate, block);
            engine.getDeviceManager().dispatchPendingUpdates();
        }

        ~HostedAudio()
        {
            engine.getDeviceManager().deviceManager.closeAudioDevice();
            engine.getDeviceManager().removeHostedAudioDeviceInterface();
        }

        /** The loudest sample over some seconds of output. */
        float peak (double seconds)
        {
            float loudest = 0;
            juce::MidiBuffer midi;

            for (int done = 0; done < static_cast<int> (seconds * rate); done += block)
            {
                juce::AudioBuffer<float> buffer (2, block);
                buffer.clear();
                io.processBlock (buffer, midi);
                loudest = std::max (loudest, buffer.getMagnitude (0, block));
            }

            return loudest;
        }
    };

    std::vector<model::Note> notesOf (const Session& s, const std::string& pattern, const std::string& channel)
    {
        std::vector<model::Note> notes;

        if (auto* p = model::findPattern (s.getProject(), pattern))
            for (auto& n : p->notes)
                if (n.channel == channel)
                    notes.push_back (n);

        std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return std::tie (a.start, a.key) < std::tie (b.start, b.key); });
        return notes;
    }
}

TEST_CASE ("A rhythm machine's steps edit the pattern selected in GrooveKit")
{
    auto doc = Session::createEmptyProject();
    doc["tempo"] = 120;
    Session session (doc);
    run (session, { R"({ "type": "addChannel", "channel": { "id": "drums", "instrument": { "type": "sampler", "layers": [] } } })",
                    R"({ "type": "addChannel", "channel": { "id": "bass", "instrument": { "type": "sampler", "layers": [] } } })" });

    LiveEngine live (test::getEngineContext(), session, test::tempDir() / "machine-recording.json");
    auto& bridge = live.getMachines();
    FakeMachine machine;
    bridge.attach ("drums", &machine);
    REQUIRE (machine.host != nullptr);
    auto& host = *machine.host;
    host.setStepTicks (240);
    bridge.updateNow();

    // No pattern yet: a bar of steps to set, and the first one set makes the pattern.
    CHECK_FALSE (machine.exists);
    CHECK (machine.lengthSteps == 16);
    host.setStep (36, 0, 1, 0, 0, 0);
    REQUIRE (session.getProject().patterns.size() == 1);
    const auto pattern = session.getProject().patterns[0].id;
    CHECK (notesOf (session, pattern, "drums") == std::vector<model::Note> { { "drums", 0, 240, 36, 100 } });
    bridge.updateNow();
    CHECK (machine.exists);
    CHECK (machine.name == session.getProject().patterns[0].name);
    REQUIRE (machine.hits.size() == 1);
    CHECK (same (machine.hits[0], 36, 0, 100, false));

    SECTION ("a drag across the keys is one change to undo")
    {
        host.beginEdit();
        host.setStep (42, 2, 1, 0, 0, 0);
        host.setStep (42, 6, 1, 1, 0, 0);
        host.endEdit();
        CHECK (notesOf (session, pattern, "drums").size() == 3);
        CHECK (notesOf (session, pattern, "drums")[2].velocity == 127);
        CHECK (session.getHistory().back().origin == "ui");

        REQUIRE (session.undo());
        CHECK (notesOf (session, pattern, "drums").size() == 1);
        REQUIRE (session.redo());
        CHECK (notesOf (session, pattern, "drums").size() == 3);

        // Each click on its own is a change of its own.
        host.setStep (42, 2, 0, 0, 0, 0);
        host.setStep (42, 6, 0, 0, 0, 0);
        CHECK (notesOf (session, pattern, "drums").size() == 1);
        REQUIRE (session.undo());
        CHECK (notesOf (session, pattern, "drums").size() == 2);
    }

    SECTION ("accents and flams")
    {
        host.setAccent (0, 1);
        CHECK (notesOf (session, pattern, "drums")[0].velocity == 127);
        host.setAccent (0, 0);
        CHECK (notesOf (session, pattern, "drums")[0].velocity == 100);

        // 20 ms at 120 BPM is 38 ticks: the grace note sits that much before its hit.
        host.setStep (38, 4, 1, 0, 1, 20.0f);
        auto notes = notesOf (session, pattern, "drums");
        REQUIRE (notes.size() == 3);
        CHECK (notes[1] == model::Note { "drums", 960 - 38, 38, 38, 64 });
        CHECK (notes[2] == model::Note { "drums", 960, 240, 38, 100 });
        bridge.updateNow();
        REQUIRE (machine.hits.size() == 2);
        CHECK (same (machine.hits[1], 38, 4, 100, true));

        host.setFlamTime (10.0f);
        CHECK (notesOf (session, pattern, "drums")[1].start == 960 - 19);

        host.setFlam (4, 0, 0);
        CHECK (notesOf (session, pattern, "drums").size() == 2);

        // On the first step the grace note can't come earlier: it's played with the hit.
        host.setFlam (0, 1, 20.0f);
        notes = notesOf (session, pattern, "drums");
        REQUIRE (notes.size() == 3);
        CHECK (notes[0].start == 0);
        CHECK (notes[1].start == 0);
        bridge.updateNow();
        CHECK (same (machine.hits[0], 36, 0, 100, true));
    }

    SECTION ("notes from elsewhere show, and those off the steps are kept")
    {
        run (session, { R"({ "type": "addNotes", "pattern": ")" + pattern + R"(", "notes": [
                            { "channel": "drums", "start": 480, "length": 120, "key": 46, "velocity": 90 },
                            { "channel": "drums", "start": 100, "length": 60, "key": 46 },
                            { "channel": "bass", "start": 0, "length": 960, "key": 36 } ] })" });
        bridge.updateNow();
        REQUIRE (machine.hits.size() == 2);
        CHECK (same (machine.hits[1], 46, 2, 90, false));

        host.setStep (36, 0, 0, 0, 0, 0);
        CHECK (notesOf (session, pattern, "drums").size() == 2);
        CHECK (notesOf (session, pattern, "bass").size() == 1);

        // In 32nds the same notes fall on other steps.
        host.setStepTicks (120);
        bridge.updateNow();
        CHECK (machine.stepTicks == 120);
        CHECK (machine.lengthSteps == 32);
        REQUIRE (machine.hits.size() == 1);
        CHECK (machine.hits[0].step == 4);
    }

    SECTION ("rows, length, swing and grooves")
    {
        host.pasteRow (39, 0, 16, std::vector<gkmachine::Hit> { { 39, 1, 127, 0 }, { 39, 3, 100, 0 } }.data(), 2, 0);
        CHECK (notesOf (session, pattern, "drums").size() == 3);
        host.clearRow (39, 0, 2);
        CHECK (notesOf (session, pattern, "drums").size() == 2);

        host.setLength (32);
        CHECK (model::findPattern (session.getProject(), pattern)->length == 7680);
        bridge.updateNow();
        CHECK (machine.lengthSteps == 32);

        // A turn of the knob is one change.
        host.setShuffle (0.5f);
        host.beginEdit();
        host.setShuffle (0.2f);
        host.setShuffle (0.3f);
        host.endEdit();
        CHECK (session.getProject().swing == 0.3);
        REQUIRE (session.undo());
        CHECK (session.getProject().swing == 0.5);

        // Beside the bass's notes the groove repeats through the pattern's two bars.
        run (session, { R"({ "type": "addNotes", "pattern": ")" + pattern + R"(", "notes": [ { "channel": "bass", "start": 0, "length": 960 } ] })" });
        const std::vector<gkmachine::Hit> groove { { 36, 0, 100, 0 }, { 38, 8, 127, 1 } };
        host.loadGroove ("Test", groove.data(), 2, 16, 20.0f);
        auto notes = notesOf (session, pattern, "drums");
        REQUIRE (notes.size() == 6);
        CHECK (notes[0] == model::Note { "drums", 0, 240, 36, 100 });
        CHECK (notes[2] == model::Note { "drums", 1920, 240, 38, 127 });
        CHECK (notes[3] == model::Note { "drums", 3840, 240, 36, 100 });
        CHECK (model::findPattern (session.getProject(), pattern)->length == 7680);

        // In a pattern of its own it sets the length.
        run (session, { R"({ "type": "addPattern", "pattern": { "id": "empty", "length": 15360, "notes": [] } })" });
        bridge.setFocus ("empty");
        host.loadGroove ("Test", groove.data(), 2, 16, 20.0f);
        CHECK (notesOf (session, "empty", "drums").size() == 3);
        CHECK (model::findPattern (session.getProject(), "empty")->length == 3840);
        bridge.updateNow();
        CHECK (machine.lengthSteps == 16);

        // A pattern that's gone: the first is shown.
        bridge.setFocus ("gone");
        bridge.updateNow();
        CHECK (machine.name == session.getProject().patterns[0].name);
    }

    bridge.disconnect();
    CHECK (machine.host == nullptr);
    CHECK (machine.references == 0);
}

TEST_CASE ("A machine playing lines edits notes with their lengths")
{
    auto doc = Session::createEmptyProject();
    doc["tempo"] = 120;
    Session session (doc);
    run (session, { R"({ "type": "addChannel", "channel": { "id": "bass", "instrument": { "type": "sampler", "layers": [] } } })",
                    R"({ "type": "addPattern", "pattern": { "id": "p", "notes": [
                        { "channel": "bass", "start": 0, "length": 120, "key": 36 },
                        { "channel": "bass", "start": 970, "length": 100, "key": 48 } ] } })" });

    LiveEngine live (test::getEngineContext(), session, test::tempDir() / "line-recording.json");
    auto& bridge = live.getMachines();
    bridge.setFocus (model::Id ("p"));
    FakeMachine machine;
    bridge.attach ("bass", &machine);
    REQUIRE (machine.host != nullptr);
    auto& host = *machine.host;
    host.setStepTicks (240);
    bridge.updateNow();

    // Every note as it is, the one off the grid too.
    REQUIRE (machine.notes.size() == 2);
    CHECK (machine.notes[1].start == 970);
    CHECK (machine.notes[1].length == 100);

    // Steps 2 to 5 rewritten: a note slid into the next, which is tied over a step. The note
    // starting at 970 (step 4) goes with them; the first note stays.
    const std::vector<gkmachine::Note> line { { 38, 480, 540, 127 }, { 43, 960, 360, 100 } };
    host.setNotes (2, 4, line.data(), 2);
    auto notes = notesOf (session, "p", "bass");
    REQUIRE (notes.size() == 3);
    CHECK (notes[0] == model::Note { "bass", 0, 120, 36, 100 });
    CHECK (notes[1] == model::Note { "bass", 480, 540, 38, 127 });
    CHECK (notes[2] == model::Note { "bass", 960, 360, 43, 100 });
    REQUIRE (session.undo());
    CHECK (notesOf (session, "p", "bass").size() == 2);
    REQUIRE (session.redo());

    // A line loaded alone in its pattern sets the length; a pattern of four bars is filled.
    run (session, { R"({ "type": "updatePattern", "pattern": "p", "changes": { "length": 15360 } })" });
    host.loadNotes ("Test", line.data(), 2, 8);
    notes = notesOf (session, "p", "bass");
    CHECK (notes.size() == 2);
    CHECK (model::findPattern (session.getProject(), "p")->length == 1920);

    run (session, { R"({ "type": "addChannel", "channel": { "id": "drums", "instrument": { "type": "sampler", "layers": [] } } })",
                    R"({ "type": "updatePattern", "pattern": "p", "changes": { "length": 3840 } })",
                    R"({ "type": "addNotes", "pattern": "p", "notes": [ { "channel": "drums", "start": 0, "length": 240, "key": 36 } ] })" });
    host.loadNotes ("Test", line.data(), 2, 8);
    notes = notesOf (session, "p", "bass");
    REQUIRE (notes.size() == 4);
    CHECK (notes[2] == model::Note { "bass", 2400, 540, 38, 127 });
    CHECK (model::findPattern (session.getProject(), "p")->length == 3840);

    bridge.disconnect();
}

TEST_CASE ("Groove-8 inside the engine edits the song, its own sequencer quiet")
{
    static PluginCatalog catalog (test::getEngine(), test::tempDir() / "machine-plugins.xml");
    std::optional<juce::PluginDescription> groove8;

    for (auto& d : catalog.getPlugins())
        if (d.name == "Groove-8")
            groove8 = d;

    REQUIRE (groove8);

    auto dir = test::tempDir() / "machine-groove8";
    std::filesystem::remove_all (dir);
    auto session = Session::open (dir);

    // Its sequencer switched on, with its first factory pattern: outside GrooveKit it would play.
    Json channel { { "id", "g8" }, { "instrument", { { "type", "plugin" },
        { "plugin", { { "format", "VST3" }, { "uid", groove8->createIdentifierString().toStdString() }, { "name", "Groove-8" }, { "params", { { "Seq", 1 } } } } } } } };
    session->execute ({ command (Json { { "type", "addChannel" }, { "channel", channel } }.dump()),
                        command (R"({ "type": "addPattern", "pattern": { "id": "p", "length": 3840, "notes": [] } })"),
                        command (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "row": 0, "start": 0, "length": 15360, "pattern": "p" } })") });

    HostedAudio audio;
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "machine-recording.json");
    auto& bridge = live.getMachines();
    CHECK (bridge.getConnectedChannels() == std::vector<model::Id> { "g8" });

    // What its step keys send: a hit on the bass drum, accented, and taken off again, in the
    // pattern selected (a new song has Pattern 1 of its own too).
    bridge.setFocus (model::Id ("p"));
    auto* host = bridge.findHost ("g8");
    REQUIRE (host != nullptr);
    host->setStep (36, 4, 1, 1, 0, 0);
    CHECK (notesOf (*session, "p", "g8") == std::vector<model::Note> { { "g8", 960, 240, 36, 127 } });
    host->setStep (36, 4, 0, 0, 0, 0);
    CHECK (notesOf (*session, "p", "g8").empty());

    // A change that rebuilds the Edit makes the plugin again: it's connected again.
    session->execute ({ command (R"({ "type": "addChannel", "channel": { "id": "other", "instrument": { "type": "sampler", "layers": [] } } })") });
    live.updateNow();
    CHECK (bridge.getConnectedChannels() == std::vector<model::Id> { "g8" });
    CHECK (bridge.findHost ("g8") != nullptr);

    // Connected, it plays the song's notes only: none here. Let go, it plays its own pattern
    // again (the test isn't GrooveKit's engine, where it never would).
    live.play();
    CHECK (audio.peak (1.0) < 0.0001f);
    bridge.disconnect();
    CHECK (audio.peak (1.0) > 0.01f);
    live.stop();
}

TEST_CASE ("Groove-3 inside the engine sees the notes with their lengths, its own line quiet")
{
    static PluginCatalog catalog (test::getEngine(), test::tempDir() / "line-plugins.xml");
    std::optional<juce::PluginDescription> groove3;

    for (auto& d : catalog.getPlugins())
        if (d.name == "Groove-3")
            groove3 = d;

    REQUIRE (groove3);

    auto dir = test::tempDir() / "machine-groove3";
    std::filesystem::remove_all (dir);
    auto session = Session::open (dir);

    // Its line running, as it is out of the box: outside GrooveKit it would play.
    Json channel { { "id", "g3" }, { "instrument", { { "type", "plugin" },
        { "plugin", { { "format", "VST3" }, { "uid", groove3->createIdentifierString().toStdString() }, { "name", "Groove-3" }, { "params", { { "Run", 1 } } } } } } } };
    session->execute ({ command (Json { { "type", "addChannel" }, { "channel", channel } }.dump()),
                        command (R"({ "type": "addPattern", "pattern": { "id": "p", "length": 3840, "notes": [] } })"),
                        command (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "row": 0, "start": 0, "length": 15360, "pattern": "p" } })") });

    HostedAudio audio;
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "line-recording.json");
    auto& bridge = live.getMachines();
    CHECK (bridge.getConnectedChannels() == std::vector<model::Id> { "g3" });
    bridge.setFocus (model::Id ("p"));

    // What its step section sends: a note sliding into the next, which is tied over a step.
    auto* host = bridge.findHost ("g3");
    REQUIRE (host != nullptr);
    const std::vector<gkmachine::Note> line { { 36, 0, 300, 127 }, { 48, 240, 360, 100 } };
    host->setNotes (0, 2, line.data(), 2);
    CHECK (notesOf (*session, "p", "g3") == std::vector<model::Note> { { "g3", 0, 300, 36, 127 }, { "g3", 240, 360, 48, 100 } });
    live.updateNow();

    // Connected, it plays the song's notes: without any, nothing. Let go, its own line plays
    // (the test isn't GrooveKit's engine, where it never would).
    host->setNotes (0, 16, nullptr, 0);
    CHECK (notesOf (*session, "p", "g3").empty());
    live.updateNow();
    live.play();
    CHECK (audio.peak (2.5) < 0.0001f);
    bridge.disconnect();
    CHECK (audio.peak (2.5) > 0.01f);
    live.stop();
}
