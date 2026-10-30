#include "TestHelpers.h"

#include "api/ApiHandler.h"
#include "effects/EqPlugin.h"
#include "analysis/Analysis.h"
#include "library/SampleLibrary.h"
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
            return lastResponse = test::callApi (handler, client, method, std::move (params));
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
    CHECK (mixer::guessPart ("Sample 3", "clap", false, 0) == ChannelPart::clap);
    CHECK (mixer::guessPart ("Metal loop", "", true, 0) == ChannelPart::percussion);
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
    CHECK (slot.highCutHz == 4000);

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

    // Kick: lows, low-passed with its top eased off. Hats: high-passed, the top tamed. Synths: band-passed to the mids.
    CHECK (param (p, "kick", "band7Type") == static_cast<double> (Type::highCut));
    CHECK (param (p, "kick", "band7Freq") == 4000);
    CHECK (param (p, "kick", "band6Type") == static_cast<double> (Type::highShelf));
    CHECK (param (p, "kick", "band6GainDb") == -4);
    CHECK (param (p, "hat", "band1Type") == static_cast<double> (Type::lowCut));
    CHECK (param (p, "hat", "band6Type") == static_cast<double> (Type::highShelf));
    CHECK (param (p, "hat", "band6GainDb") < 0);
    CHECK (param (p, "chords", "band1Freq") == 280);
    CHECK (param (p, "chords", "band7Freq") == 6000);
    CHECK (model::getEffectId (model::findInsert (p, "chords")->effects.front()) == "slot");

    auto slot = mixer::readSlot (p, h.channel ("hat"));
    REQUIRE (slot);
    CHECK (slot->lowCutHz == 1800);
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
    CHECK (mixer::readSlot (h.session.getProject(), h.channel ("bass"))->lowCutHz == 280);

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
    CHECK (offer["candidates"][1].contains ("alone"));             // a one-shot: on its part's figure, in a phantom slot

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

TEST_CASE ("A loop's tempo is made exact by its length")
{
    library::SampleEntry e;
    e.classification.loop = true;

    // Heard as 127.6, but exactly two bars at 128.
    e.features.estimatedTempo = 127.6;
    e.features.durationSeconds = 8 * 60.0 / 128;
    CHECK (*library::SampleLibrary::loopTempo (e) == Catch::Approx (128).margin (1e-9));

    // A named tempo wins over the heard one, and is made exact the same way.
    e.classification.tempo = 130;
    e.features.durationSeconds = 4 * 60.0 / 129.9;
    CHECK (*library::SampleLibrary::loopTempo (e) == Catch::Approx (129.9).margin (1e-9));

    // With a tail it isn't whole bars: the tempo stays as named.
    e.features.durationSeconds = 4 * 60.0 / 130 + 0.4;
    CHECK (*library::SampleLibrary::loopTempo (e) == 130);

    e.classification.tempo.reset();
    e.features.estimatedTempo.reset();
    CHECK_FALSE (library::SampleLibrary::loopTempo (e));
}

TEST_CASE ("A loop offered on its own is tried in the song, on its grid from the same bar")
{
    Harness h;
    const auto folder = test::tempDir() / "phantom";
    std::filesystem::create_directories (folder);

    // A one-bar loop at 128 BPM with a click on every 8th, a little long at the end (as files are).
    const auto loopFile = folder / "Click Top Loop 128bpm.wav";
    {
        constexpr double rate = 44100;
        const auto length = static_cast<int> (4 * 60.0 / 128 * rate) + 3;
        juce::AudioBuffer<float> b (2, length);
        b.clear();

        for (int eighth = 0; eighth < 8; ++eighth)
        {
            const auto at = static_cast<int> (eighth * 30.0 / 128 * rate);

            for (int i = 0; i < 200 && at + i < length; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    b.setSample (ch, at + i, 0.8f * std::sin (i * 0.9f) * (1 - i / 200.0f));
        }

        writeAudio (b, rate, loopFile, {});
    }

    // The song: a bass at 120 BPM.
    auto commands = Json::array ({ addChannel ("bass", "Bass", "bass_a2.wav") });

    for (auto& c : playEveryBeat ({ "bass" }, 8))
        commands.push_back (c);

    h.call ("execute", { { "commands", commands } });

    // Offered with no audition: the engine tries it in the song, in a phantom slot of its own.
    auto offered = h.call ("picks.offer", { { "title", "Which top?" }, { "end", 8 * 3840 }, { "candidates", {
        { { "path", loopFile.string() } }, { { "path", test::fixture ("hat.wav").string() } } } } });
    REQUIRE (offered.contains ("result"));
    auto offer = h.call ("picks.get", { { "id", offered["result"]["id"] } })["result"]["offer"];

    REQUIRE (offer["candidates"][0].contains ("alone"));
    CHECK (offer["candidates"][0].contains ("audition"));
    CHECK (offer["candidates"][1].contains ("alone"));            // a one-shot plays its part's figure (hats: off-beats)

    // Stretched to 120 BPM, its clicks fall on the song's 8ths for all eight bars: no drift.
    const auto alone = analysis::loadAudio (offer["candidates"][0]["alone"].get<std::string>());
    const auto onsets = analysis::detectOnsets (alone, -30.0f, 0.1);
    REQUIRE (onsets.size() >= 60);

    for (auto t : onsets)
    {
        const auto eighths = t / 0.25;
        CHECK (std::abs (eighths - std::round (eighths)) * 0.25 < 0.012);
    }

    // The song stays as it was.
    CHECK (h.session.getProject().channels.size() == 1);
}

TEST_CASE ("One-shots and instruments are offered playing notes in the song")
{
    Harness h;
    auto commands = Json::array ({ addChannel ("bass", "Bass", "bass_a2.wav") });

    for (auto& c : playEveryBeat ({ "bass" }, 2))
        commands.push_back (c);

    h.call ("execute", { { "commands", commands } });

    // A kick one-shot on its part's figure (every beat), and an instrument with a line of its own.
    const Json instrument { { "type", "sampler" }, { "layers", { { { "id", "l" }, { "sample", test::fixture ("beep.wav").string() }, { "rootKey", 69 } } } } };
    auto offered = h.call ("picks.offer", { { "title", "Which sound?" }, { "end", 2 * 3840 }, { "candidates", {
        { { "path", test::fixture ("kick.wav").string() }, { "part", "kick" } },
        { { "instrument", instrument }, { "label", "Beep line" }, { "notes", { { { "start", 0 }, { "length", 480 }, { "key", 69 } },
                                                                              { { "start", 1920 }, { "length", 480 }, { "key", 72 } } } } },
    } } });
    REQUIRE (offered.contains ("result"));
    auto offer = h.call ("picks.get", { { "id", offered["result"]["id"] } })["result"]["offer"];

    auto onsetsOf = [&] (int n)
    {
        REQUIRE (offer["candidates"][n].contains ("alone"));
        return analysis::detectOnsets (analysis::loadAudio (offer["candidates"][n]["alone"].get<std::string>()), -30.0f, 0.2);
    };

    // At 120 BPM: the kick on every beat (0.5 s), the line on beats 1 and 3 (1 s), for two bars, from the same bar.
    const auto kick = onsetsOf (0);
    REQUIRE (kick.size() == 8);
    CHECK (kick.front() < 0.012);
    CHECK (std::abs (kick[1] - 0.5) < 0.012);

    const auto line = onsetsOf (1);
    REQUIRE (line.size() == 4);
    CHECK (std::abs (line[1] - 1.0) < 0.012);

    // Picking the instrument: no sample to rate, and the song is as it was.
    CHECK (h.call ("picks.choose", { { "id", offered["result"]["id"] }, { "choice", 1 } }).contains ("result"));
    CHECK (h.session.getProject().channels.size() == 1);
    CHECK (h.call ("picks.offer", { { "title", "?" }, { "candidates", { { { "label", "nothing" } } } } }).contains ("error"));
}

TEST_CASE ("The arrangement check counts parts per bar and new parts coming in together")
{
    Harness h;
    auto commands = Json::array();

    for (auto id : { "kick", "perc", "hats", "metal", "sub", "stab", "clap", "atmo" })
        commands.push_back (addChannel (id, id, "beep.wav"));

    // One pattern per part, each a note a beat; clips (start bar, bars).
    const std::vector<std::tuple<std::string, int, int>> clips {
        { "kick", 0, 16 }, { "perc", 0, 16 }, { "hats", 4, 12 }, { "metal", 4, 12 },       // two new at bar 5
        { "sub", 8, 8 }, { "stab", 8, 8 }, { "clap", 12, 4 }, { "atmo", 0, 16 },            // seven at once from bar 13
    };

    for (auto& [id, from, bars] : clips)
    {
        commands.push_back ({ { "type", "addPattern" }, { "pattern", { { "id", "p-" + id }, { "notes", { { { "channel", id }, { "start", 0 }, { "length", 240 } } } } } } });
        commands.push_back ({ { "type", "addClip" }, { "clip", { { "type", "pattern" }, { "id", "c-" + id }, { "row", 0 }, { "start", from * 3840 },
                                                                 { "length", bars * 3840 }, { "pattern", "p-" + id } } } });
    }

    commands.push_back ({ { "type", "updateChannel" }, { "channel", "atmo" }, { "changes", { { "part", "fx" } } } });
    h.call ("execute", { { "commands", commands } });

    auto result = h.call ("mix.check", { { "listen", false } })["result"];
    INFO (result.dump (1));

    // The atmo bed under the whole song doesn't count.
    CHECK (result["partsPerBar"][0] == 2);
    CHECK (result["partsPerBar"][12] == 7);
    CHECK (hasProblem (result, "entersTogether", "hats"));
    CHECK (hasProblem (result, "entersTogether", "sub"));
    CHECK (hasProblem (result, "tooManyParts", "clap"));
    CHECK_FALSE (hasProblem (result, "entersTogether", "kick"));    // the song's first bar
    CHECK_FALSE (hasProblem (result, "tooManyParts", "atmo"));

    // With room for seven, bars 13-16 are fine.
    result = h.call ("mix.check", { { "listen", false }, { "maxParts", 7 } })["result"];
    CHECK_FALSE (hasProblem (result, "tooManyParts", "clap"));
}
