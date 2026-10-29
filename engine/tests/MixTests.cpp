#include "TestHelpers.h"

#include "api/ApiHandler.h"
#include "effects/EqPlugin.h"
#include "live/SamplePreview.h"
#include "render/Encode.h"
#include "mixer/Slots.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gk;
using model::Json;

namespace
{
    struct Harness
    {
        Session session { Session::createEmptyProject() };
        ApiHandler handler { session, test::getEngineContext() };
        Json lastResponse;
        ApiHandler::Client client;
        int nextId = 1;

        Harness()
        {
            client.send = [this] (const std::string& m) { lastResponse = Json::parse (m); };
        }

        Json call (const std::string& method, Json params = Json::object())
        {
            handler.handleMessage (Json { { "jsonrpc", "2.0" }, { "id", nextId++ }, { "method", method }, { "params", params } }.dump(), client);
            return lastResponse;
        }

        const model::Channel& channel (const std::string& id)  { return *model::findChannel (session.getProject(), id); }
    };

    Json addChannel (const std::string& id, const std::string& name, const char* sample, Json extra = Json::object())
    {
        Json channel { { "id", id }, { "name", name }, { "instrument", { { "type", "sampler" }, { "layers", { {
            { "id", "l" }, { "sample", test::fixture (sample).string() }, { "rootKey", 45 } } } } } } };
        channel.update (extra);
        return { { "type", "addChannel" }, { "channel", channel } };
    }

    /** One note per channel on every beat for bars, in a pattern clip. */
    std::vector<Json> playEveryBeat (const std::vector<std::string>& channels, int bars, int key = 45)
    {
        auto notes = Json::array();

        for (int beat = 0; beat < 4; ++beat)
            for (auto& c : channels)
                notes.push_back ({ { "channel", c }, { "start", beat * 960 }, { "length", 480 }, { "key", key } });

        return {
            Json { { "type", "addPattern" }, { "pattern", { { "id", "p" }, { "notes", notes } } } },
            Json { { "type", "addClip" }, { "clip", { { "type", "pattern" }, { "id", "c" }, { "row", 0 }, { "start", 0 }, { "length", bars * 3840 }, { "pattern", "p" } } } },
        };
    }

    double param (const model::Project& p, const std::string& insert, const std::string& id)
    {
        for (auto& e : model::findInsert (p, insert)->effects)
            if (auto* b = std::get_if<model::BuiltinEffect> (&e); b != nullptr && b->id == "slot")
                return b->params.at (id);

        FAIL ("no slot on " << insert);
        return 0;
    }

    bool hasProblem (const Json& result, const std::string& kind, const std::string& channel)
    {
        for (auto& p : result["problems"])
            if (p["kind"] == kind && std::find (p["channels"].begin(), p["channels"].end(), Json (channel)) != p["channels"].end())
                return true;

        return false;
    }
}

TEST_CASE ("Parts are guessed from names and samples")
{
    using model::ChannelPart;

    CHECK (mixer::guessPart ("Rumble 140", "kick", true, 0.8f) == ChannelPart::rumble);
    CHECK (mixer::guessPart ("Kick", "", false, 0) == ChannelPart::kick);
    CHECK (mixer::guessPart ("Open hat", "", false, 0) == ChannelPart::hats);
    CHECK (mixer::guessPart ("Dusty chord stab", "", false, 0) == ChannelPart::synth);
    CHECK (mixer::guessPart ("Sample 3", "clap", false, 0) == ChannelPart::percussion);
    CHECK (mixer::guessPart ("Sample 4", "drums", true, 0.6f) == ChannelPart::drums);
    CHECK (mixer::guessPart ("Sample 5", "drums", true, 0.1f) == ChannelPart::percussion);
    CHECK (mixer::guessPart ("Sample 6", "", false, 0) == ChannelPart::synth);
}

TEST_CASE ("Slots fitted to a style's stems stay within the part's range")
{
    using model::ChannelPart;

    // A music stem strongest from 250 Hz to 4 kHz, quiet outside.
    const std::vector<float> stem { -40, -35, -24, -8, -6, -7, -12, -25, -30, -40 };
    auto slot = mixer::fitToReferences (mixer::defaultSlot (ChannelPart::synth), ChannelPart::synth, stem);
    CHECK (slot.from == "references");
    CHECK (slot.lowCutHz == 250);
    CHECK (slot.highCutHz == 5000);         // 4 kHz, raised to the synth's least

    // Kicks keep their own slot.
    CHECK (mixer::fitToReferences (mixer::defaultSlot (ChannelPart::kick), ChannelPart::kick, stem) == mixer::defaultSlot (ChannelPart::kick));
}

TEST_CASE ("Each channel gets its own insert with a slot EQ for its part, in one undo step")
{
    using Type = effects::EqPlugin::BandType;
    Harness h;

    h.call ("execute", { { "commands", {
        Json { { "type", "addInsert" }, { "insert", { { "id", "drums" }, { "name", "Drums" } } } },
        addChannel ("kick", "Kick", "kick.wav", { { "insert", "drums" } }),
        addChannel ("hat", "Hat", "hat.wav", { { "insert", "drums" } }),
        addChannel ("chords", "Chords", "beep.wav"),
    } } });

    auto r = h.call ("mix.slots", { { "apply", true } });
    REQUIRE (r.contains ("result"));
    CHECK (r["result"]["applied"].size() == 3);

    const auto& p = h.session.getProject();

    // Its own insert, feeding where it played.
    CHECK (h.channel ("kick").insert == "kick");
    CHECK (model::findInsert (p, "kick")->output == "drums");
    CHECK (model::findInsert (p, "kick")->name == "Kick");
    CHECK (model::findInsert (p, "chords")->output == "master");
    CHECK (h.channel ("kick").part == model::ChannelPart::kick);
    CHECK (h.channel ("hat").part == model::ChannelPart::hats);
    CHECK (h.channel ("chords").part == model::ChannelPart::synth);

    // Kick: low-passed steeply. Hats: high-passed, the top tamed. Synths: band-passed.
    CHECK (param (p, "kick", "band7Type") == static_cast<double> (Type::highCut));
    CHECK (param (p, "kick", "band6Type") == static_cast<double> (Type::highCut));
    CHECK (param (p, "kick", "band7Freq") == 5000);
    CHECK (param (p, "hat", "band1Type") == static_cast<double> (Type::lowCut));
    CHECK (param (p, "hat", "band6Type") == static_cast<double> (Type::highShelf));
    CHECK (param (p, "hat", "band6GainDb") < 0);
    CHECK (param (p, "chords", "band1Freq") == 180);
    CHECK (param (p, "chords", "band7Freq") == 9000);
    CHECK (model::getEffectId (model::findInsert (p, "chords")->effects.front()) == "slot");

    auto slot = mixer::readSlot (p, h.channel ("hat"));
    REQUIRE (slot);
    CHECK (slot->lowCutHz == 450);
    CHECK (slot->topShelfDb == -3);

    // Applied again to channels that have one, nothing changes.
    r = h.call ("mix.slots", { { "apply", true }, { "onlyNew", true } });
    CHECK (r["result"]["applied"].empty());

    // One undo takes them all out.
    h.call ("undo");
    CHECK (h.session.getProject().mixer.inserts.size() == 2);
    CHECK (h.channel ("kick").insert == "drums");
    CHECK_FALSE (h.channel ("kick").part);
}

TEST_CASE ("A slot can be set by hand")
{
    Harness h;
    h.call ("execute", { { "commands", { addChannel ("bass", "Bass", "bass_a2.wav") } } });

    auto r = h.call ("mix.setSlot", Json::parse (R"({ "channel": "bass", "lowCutHz": 50, "highCutHz": null })"));
    REQUIRE (r.contains ("result"));
    CHECK (r["result"]["part"] == "bass");

    auto slot = mixer::readSlot (h.session.getProject(), h.channel ("bass"));
    REQUIRE (slot);
    CHECK (slot->lowCutHz == 50);
    CHECK (slot->highCutHz == 0);

    // Its insert is reused, not another made.
    const auto inserts = h.session.getProject().mixer.inserts.size();
    h.call ("mix.setSlot", { { "channel", "bass" }, { "part", "synth" } });
    CHECK (h.session.getProject().mixer.inserts.size() == inserts);
    CHECK (h.channel ("bass").part == model::ChannelPart::synth);
    CHECK (mixer::readSlot (h.session.getProject(), h.channel ("bass"))->lowCutHz == 180);

    CHECK (h.call ("mix.setSlot", { { "channel", "bass" }, { "lowCutHz", 5 } }).contains ("error"));
    CHECK (h.call ("mix.setSlot", { { "channel", "bass" }, { "part", "guitar" } }).contains ("error"));
    CHECK (h.call ("mix.setSlot", { { "channel", "bass" }, { "lowCutHz", 3000 }, { "highCutHz", 2000 } }).contains ("error"));
}

TEST_CASE ("The mix check finds parts in each other's way, and slots clear them")
{
    Harness h;
    auto commands = Json::array ({
        addChannel ("bassA", "Bass A", "bass_a2.wav"),
        addChannel ("bassB", "Bass B", "bass_a2.wav"),
        addChannel ("pad", "Pad", "bass_a2.wav"),
    });

    for (auto& c : playEveryBeat ({ "bassA", "bassB", "pad" }, 2))
        commands.push_back (c);

    h.call ("execute", { { "commands", commands } });

    auto r = h.call ("mix.check");
    REQUIRE (r.contains ("result"));
    auto result = r["result"];

    CHECK (hasProblem (result, "noSlot", "pad"));
    CHECK (hasProblem (result, "lowEnd", "pad"));           // a synth playing bass notes
    CHECK (hasProblem (result, "lowShared", "bassA"));      // two basses on the same notes
    CHECK (result["channels"].size() == 3);
    CHECK (result["problems"][0]["severity"] >= result["problems"].back()["severity"]);

    h.call ("mix.slots", { { "apply", true } });
    result = h.call ("mix.check")["result"];
    CHECK_FALSE (hasProblem (result, "noSlot", "pad"));
    CHECK_FALSE (hasProblem (result, "lowEnd", "pad"));
}

TEST_CASE ("The mix check hears highs that never move")
{
    Harness h;
    auto commands = Json::array ({ addChannel ("hat", "Hat", "hat.wav") });

    for (auto& c : playEveryBeat ({ "hat" }, 8, 60))
        commands.push_back (c);

    h.call ("execute", { { "commands", commands } });
    h.call ("mix.slots", { { "apply", true } });

    auto result = h.call ("mix.check")["result"];
    CHECK (hasProblem (result, "staticTop", "hat"));
}

TEST_CASE ("Candidates are heard in the song, in the channel's place")
{
    Harness h;
    // Auditions go to the cache folder: a test one, not the user's.
    const auto* before = std::getenv ("GROOVEKIT_CACHE_DIR");
    const std::optional<std::string> saved = before != nullptr ? std::optional<std::string> (before) : std::nullopt;
    setenv ("GROOVEKIT_CACHE_DIR", (test::tempDir() / "mix-cache").c_str(), 1);
    struct Restore { std::optional<std::string> value; ~Restore() { if (value) setenv ("GROOVEKIT_CACHE_DIR", value->c_str(), 1); else unsetenv ("GROOVEKIT_CACHE_DIR"); } } restore { saved };

    auto commands = Json::array ({
        Json { { "type", "setKey" }, { "key", "A minor" } },
        addChannel ("bass", "Bass", "bass_a2.wav"),
        addChannel ("lead", "Lead", "beep.wav"),
    });

    for (auto& c : playEveryBeat ({ "bass", "lead" }, 2))
        commands.push_back (c);

    h.call ("execute", { { "commands", commands } });
    h.call ("mix.slots", { { "apply", true } });

    auto r = h.call ("mix.audition", { { "channel", "lead" }, { "end", 2 * 3840 },
                                       { "paths", { test::fixture ("bass_a2.wav").string(), test::fixture ("hat.wav").string() } } });
    REQUIRE (r.contains ("result"));
    auto& result = r["result"];

    CHECK (result["part"] == "synth");
    CHECK (std::filesystem::exists (result["current"]["file"].get<std::string>()));
    REQUIRE (result["candidates"].size() == 2);

    for (auto& c : result["candidates"])
    {
        CHECK (std::filesystem::exists (c["file"].get<std::string>()));
        CHECK (std::filesystem::exists (c["alone"].get<std::string>()));
        CHECK (std::filesystem::exists (c["rest"].get<std::string>()));
        CHECK (c["fit"].get<double>() >= 0);
        CHECK (c["fit"].get<double>() <= 100);
    }

    // A pitched part hears whether a candidate goes with the song's key: an A goes with A minor.
    CHECK (result["candidates"][0]["key"]["fits"] == true);

    // The same bass as the bass channel's is covered by it; a hat isn't.
    CHECK (result["candidates"][1]["fit"].get<double>() > result["candidates"][0]["fit"].get<double>());

    // Offered to the user, a candidate brings its two layers, so the window can fade the song under it.
    auto offered = h.call ("picks.offer", { { "title", "Which lead?" }, { "candidates", {
        { { "path", result["candidates"][0]["path"] }, { "audition", result["candidates"][0]["file"] } },
        { { "path", result["candidates"][1]["path"] } } } } });
    auto offer = h.call ("picks.get", { { "id", offered["result"]["id"] } })["result"]["offer"];
    CHECK (offer["candidates"][0]["alone"] == result["candidates"][0]["alone"]);
    CHECK (offer["candidates"][0]["rest"] == result["candidates"][0]["rest"]);
    CHECK_FALSE (offer["candidates"][1].contains ("alone"));

    // The song itself is unchanged.
    CHECK (std::get<model::SamplerInstrument> (h.channel ("lead").instrument).layers.front().sample == test::fixture ("beep.wav").string());
    CHECK (h.call ("mix.audition", { { "channel", "lead" }, { "paths", Json::array() } }).contains ("error"));
}

TEST_CASE ("The preview fades a second file in sync beneath the first")
{
    // Two constant files: the sound at 0.25, the song at 0.5.
    auto write = [] (const char* name, float value)
    {
        juce::AudioBuffer<float> b (2, 48000);

        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (b.getWritePointer (ch), value, b.getNumSamples());

        const auto file = test::tempDir() / name;
        writeAudio (b, 48000, file, {});
        return file;
    };

    const auto sound = write ("preview-sound.wav", 0.25f), song = write ("preview-song.wav", 0.5f);

    SamplePreview preview;
    juce::AudioBuffer<float> out (2, 512);

    auto block = [&]
    {
        preview.audioDeviceIOCallbackWithContext (nullptr, 0, out.getArrayOfWritePointers(), 2, 512, {});
        return out.getSample (0, 511);
    };

    // The song off: the sound alone (gain 0 dB).
    preview.play (sound, 0.0f, 0, song, 0.0f);
    block();
    CHECK (block() == Catch::Approx (0.25f).margin (0.01));

    // Faded in while playing, smoothly: all the way after 50 ms.
    preview.setUnderLevel (1.0f);
    for (int i = 0; i < 8; ++i)
        block();
    CHECK (block() == Catch::Approx (0.75f).margin (0.01));

    // Without a second file, just the sound.
    preview.play (sound, 0.0f);
    block();
    CHECK (block() == Catch::Approx (0.25f).margin (0.01));
}
