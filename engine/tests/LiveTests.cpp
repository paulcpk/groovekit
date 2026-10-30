// Real-time playback and recording, driven block by block through Tracktion's hosted audio device.

#include "TestHelpers.h"

#include "analysis/Analysis.h"
#include "api/ApiHandler.h"
#include "live/LiveEngine.h"
#include "mixer/FaderPlugin.h"
#include "render/Render.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gk;
using model::Json;
using Catch::Approx;
namespace te = tracktion;

namespace
{
    constexpr double rate = 48000;
    constexpr int block = 512;

    /** Stands in for an audio interface: input goes in, output comes out, one block at a time. */
    struct HostedAudio
    {
        te::Engine& engine;
        te::HostedAudioDeviceInterface& io;

        explicit HostedAudio (int inputs)
            : engine (test::getEngine()), io (engine.getDeviceManager().getHostedAudioDeviceInterface())
        {
            te::HostedAudioDeviceInterface::Parameters p;
            p.sampleRate = rate;
            p.blockSize = block;
            p.inputChannels = inputs;
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

        analysis::Audio process (const juce::AudioBuffer<float>& input)
        {
            analysis::Audio out;
            out.sampleRate = rate;
            out.buffer.setSize (2, input.getNumSamples());
            out.buffer.clear();

            juce::MidiBuffer midi;

            for (int start = 0; start < input.getNumSamples(); start += block)
            {
                const auto n = std::min (block, input.getNumSamples() - start);
                juce::AudioBuffer<float> io_ (std::max (2, input.getNumChannels()), n);
                io_.clear();

                for (int ch = 0; ch < input.getNumChannels(); ++ch)
                    io_.copyFrom (ch, 0, input, ch, start, n);

                io.processBlock (io_, midi);

                for (int ch = 0; ch < 2; ++ch)
                    out.buffer.copyFrom (ch, start, io_, ch, 0, n);
            }

            return out;
        }

        analysis::Audio silence (double seconds)
        {
            juce::AudioBuffer<float> input (2, static_cast<int> (seconds * rate));
            input.clear();
            return process (input);
        }
    };

    std::unique_ptr<Session> beepSession (const std::string& name)
    {
        auto dir = test::tempDir() / name;
        std::filesystem::remove_all (dir);
        auto session = Session::open (dir);

        model::Reader r;
        std::vector<model::Command> commands;

        for (auto json : {
                 Json::parse (R"({ "type": "addChannel", "channel": { "id": "beep", "instrument": { "type": "sampler", "layers": [
                                   { "id": "l", "pitchTracking": false, "sample": ")" + test::fixture ("beep.wav").string() + R"(" } ] } } })"),
                 Json::parse (R"({ "type": "addPattern", "pattern": { "id": "p", "notes": [
                                   { "channel": "beep", "start": 0, "length": 240, "velocity": 127 },
                                   { "channel": "beep", "start": 960, "length": 240, "velocity": 127 } ] } })"),
                 Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "row": 0, "start": 0, "length": 15360, "pattern": "p" } })") })
        {
            readValue (r, json, commands.emplace_back());
        }

        session->execute (commands);
        return session;
    }
}

TEST_CASE ("Live playback follows the song")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-playback");
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");

    std::vector<LiveEngine::TransportState> notifications;
    live.onTransportChange = [&] (const LiveEngine::TransportState& s) { notifications.push_back (s); };

    live.play();
    REQUIRE (live.isPlaying());
    CHECK_FALSE (notifications.empty());

    auto out = audio.silence (1.0);
    auto onsets = analysis::detectOnsets (out);

    // Playback starts within a couple of blocks; after that the beats are a beat apart.
    REQUIRE (onsets.size() == 2);
    CHECK (onsets[0] < 0.05);
    CHECK (onsets[1] - onsets[0] == Approx (0.5).margin (0.001));
    CHECK (analysis::peakDb (out) == Approx (-6.02).margin (0.05));

    SECTION ("an edit while playing keeps the transport going")
    {
        const auto before = live.getPosition();
        model::Reader r;
        model::Command c;
        readValue (r, Json::parse (R"({ "type": "updateChannel", "channel": "beep", "changes": { "volumeDb": -6 } })"), c);
        session->execute ({ c });
        live.updateNow();

        CHECK (live.isPlaying());
        CHECK (live.getPosition() >= before);

        // The song carries on from about a second in; its next beeps are at two seconds.
        auto later = audio.silence (2.0);
        CHECK (analysis::peakDb (later) == Approx (-12.04).margin (0.1));
    }

    live.stop();
    CHECK_FALSE (live.isPlaying());
}

TEST_CASE ("The song stops a few bars after its end and goes back to the top")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-song-end");      // four bars long
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");
    constexpr model::Ticks bar = 3840;
    const auto stopAt = (4 + LiveEngine::barsAfterSongEnd) * bar;

    // The position moved by hand, as the timer would see it go by (the hosted test device
    // doesn't move it on its own).
    auto passBy = [&] (model::Ticks at)
    {
        live.setPosition (at);
        live.stopAfterSongEnd();
    };

    live.play();
    passBy (stopAt - bar / 4);              // the last beat before
    CHECK (live.isPlaying());

    passBy (stopAt + bar / 8);              // past it
    CHECK_FALSE (live.isPlaying());
    CHECK (live.getPosition() == 0);

    SECTION ("a loop plays on")
    {
        live.setLoop (std::pair<model::Ticks, model::Ticks> { 0, 4 * bar });
        live.play();
        passBy (stopAt - bar / 4);
        passBy (stopAt + bar / 8);
        CHECK (live.isPlaying());
        live.stop();
    }

    SECTION ("started past the end, it plays on")
    {
        live.play();
        passBy (stopAt + bar);
        passBy (stopAt + 2 * bar);
        CHECK (live.isPlaying());
        live.stop();
    }
}

TEST_CASE ("Recording an input adds the take to the project")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-recording");

    model::Reader r;
    model::Command c;
    readValue (r, Json::parse (R"({ "type": "updateRow", "row": 0, "changes": { "mute": true } })"), c);
    session->execute ({ c });

    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");
    auto inputs = live.getInputNames();
    REQUIRE_FALSE (inputs.empty());

    live.startRecording ({ { inputs[0], "master", 1 } });
    REQUIRE (live.isRecording());

    // Audio and messages run while recording, as in the app; Tracktion's own updates mustn't stop the take.
    audio.silence (0.05);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    // One second of a 220 Hz tone into the input.
    juce::AudioBuffer<float> tone (2, static_cast<int> (rate));

    for (int i = 0; i < tone.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            tone.setSample (ch, i, 0.25f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / rate)));

    audio.process (tone);
    auto ids = live.stopRecording();

    REQUIRE (ids.size() == 1);
    CHECK_FALSE (live.isRecording());

    auto& clips = session->getProject().playlist.clips;
    auto it = std::find_if (clips.begin(), clips.end(), [&] (auto& clip) { return model::getClipId (clip) == ids[0]; });
    REQUIRE (it != clips.end());

    const auto take = std::get<model::AudioClip> (*it); // a copy: undo below rebuilds the project
    const auto& channel = *model::findChannel (session->getProject(), take.channel);
    const auto sample = model::getClipLayer (channel)->sample;
    CHECK (take.row == 1);
    CHECK (channel.group == model::ChannelGroup::arrangement);
    INFO ("sample: " << sample);
    CHECK (sample.starts_with ("recordings/"));
    CHECK (std::filesystem::exists (*session->getProjectDir() / sample));
    CHECK (model::getClipLength (*it) > 1500); // about a second at 120 BPM is 1920 ticks

    // The take renders like any other clip.
    auto compiled = compileProject (test::getEngine(), session->getProject(), { *session->getProjectDir(), te::Edit::forRendering });
    auto file = test::tempDir() / "live-recording.wav";
    RenderOptions options;
    options.end = take.start + take.length;
    renderToWav (compiled, session->getProject(), file, options);

    auto rendered = analysis::loadAudio (file);
    CHECK (analysis::peakDb (rendered) == Approx (-12.04).margin (0.3));

    // And one undo removes it again, leaving the file on disk.
    session->undo();
    CHECK (session->getProject().playlist.clips.size() == 1);
    CHECK (model::findChannel (session->getProject(), take.channel) == nullptr);
    CHECK (std::filesystem::exists (*session->getProjectDir() / sample));
}

TEST_CASE ("Recording alignment is stored per input device")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-alignment");
    const auto file = test::tempDir() / "alignment.json";
    std::filesystem::remove (file);

    {
        LiveEngine live (test::getEngineContext(), *session, file);
        CHECK (live.getAlignmentMs() == 0);
        live.setAlignmentMs (37.5);
    }

    LiveEngine live (test::getEngineContext(), *session, file);
    CHECK (live.getAlignmentMs() == 37.5);
}

TEST_CASE ("Recording sends MIDI clock that starts once and ticks evenly")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-clock");
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");

    // The hosted device has no audio callback of its own, so each block is followed by the clock's,
    // with the host time a device would give it.
    std::vector<std::pair<juce::MidiMessage, double>> sent;
    live.getMidiClock().onSend = [&] (const juce::MidiMessage& m, juce::uint64 ns) { sent.push_back ({ m, static_cast<double> (ns) / 1.0e9 }); };
    double hostSeconds = 1000;

    auto runBlocks = [&] (int n)
    {
        for (int i = 0; i < n; ++i)
        {
            audio.silence (block / rate);
            auto ns = static_cast<juce::uint64> (std::llround (hostSeconds * 1.0e9));
            live.getMidiClock().audioDeviceIOCallbackWithContext (nullptr, 0, nullptr, 0, block, { &ns });
            hostSeconds += block / rate;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
    };

    live.startRecording ({ { live.getInputNames()[0], "master", 0 } }, { .addClips = false });
    runBlocks (200); // about two seconds
    live.stopRecording();

    REQUIRE (sent.size() > 10);

    std::vector<double> clocks;
    int starts = 0, others = 0;

    for (auto& [m, t] : sent)
    {
        if (m.isMidiClock())        clocks.push_back (t);
        else if (m.isMidiStart())   starts++;
        else if (! m.isSongPositionPointer() && ! m.isMidiStop()) others++;
    }

    CHECK (starts == 1);
    CHECK (others == 0);
    CHECK (sent.back().first.isMidiStop());
    REQUIRE (clocks.size() > 80);

    for (size_t i = 1; i < clocks.size(); ++i)
        CHECK (clocks[i] - clocks[i - 1] == Approx (0.5 / 24).margin (1.0e-6));
}

TEST_CASE ("Mixer moves apply to the playing Edit without rebuilding it")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-fast-path");
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");
    live.updateNow();
    auto edit = live.getEdit();

    model::Reader r;
    std::vector<model::Command> commands (2);
    readValue (r, Json::parse (R"({ "type": "updateInsert", "insert": "master", "changes": { "volumeDb": -6, "pan": 0.5 } })"), commands[0]);
    readValue (r, Json::parse (R"({ "type": "addEffect", "insert": "master", "effect": { "type": "builtin", "id": "lp", "kind": "lowpass" } })"), commands[1]);
    session->execute ({ commands[0] });
    live.updateNow();

    CHECK (live.getEdit() == edit);
    // The master fader is the last plugin on the master insert's track.
    bool found = false;
    for (auto t : te::getAudioTracks (*edit))
        for (auto p : t->pluginList)
            if (auto f = dynamic_cast<mixer::FaderPlugin*> (p); f != nullptr && f->volumeParam->getCurrentValue() == Approx (-6.0f))
                found = f->panParam->getCurrentValue() == Approx (0.5f);
    CHECK (found);

    // Adding an effect changes the graph, so that rebuilds.
    session->execute ({ commands[1] });
    live.updateNow();
    CHECK (live.getEdit() != edit);
    edit = live.getEdit();

    readValue (r, Json::parse (R"({ "type": "updateEffect", "insert": "master", "effect": "lp", "changes": { "params": { "frequency": 500 } } })"), commands[0]);
    session->execute ({ commands[0] });
    live.updateNow();
    CHECK (live.getEdit() == edit);
    CHECK (live.findPlugin ("effect:master/lp")->getAutomatableParameterByID ("frequency")->getCurrentValue() == Approx (500.0f).margin (1.0f));
}

TEST_CASE ("Muting or soloing a row switches its automation, not just its clips")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-automation-rows");
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");

    auto run = [&] (const char* json)
    {
        model::Reader r;
        model::Command c;
        readValue (r, Json::parse (json), c);
        session->execute ({ c });
        live.updateNow();
    };

    // The master's volume automated from row 1; the beeps on row 0.
    run (R"({ "type": "addClip", "clip": { "type": "automation", "id": "a", "row": 1, "start": 0, "length": 3840,
              "target": { "scope": "insert", "insert": "master", "param": "volume" }, "points": [ { "time": 0, "value": -20 } ] } })");
    auto edit = live.getEdit();

    // Muting the beeps' row switches their clip where it is.
    run (R"({ "type": "updateRow", "row": 0, "changes": { "mute": true } })");
    CHECK (live.getEdit() != edit);     // with automation in the song, a row's mute rebuilds it

    // Muted, the automation row no longer drives the volume: it's back to its own level.
    run (R"({ "type": "updateRow", "row": 0, "changes": { "mute": false } })");
    run (R"({ "type": "updateRow", "row": 1, "changes": { "mute": true } })");

    // Points on any fader's volume: the automation, where it drives one.
    auto masterVolume = [&]
    {
        int points = 0;

        for (auto t : te::getAudioTracks (*live.getEdit()))
            for (auto p : t->pluginList)
                if (auto f = dynamic_cast<mixer::FaderPlugin*> (p))
                    points += f->volumeParam->getCurve().getNumPoints();

        return points;
    };

    CHECK (masterVolume() == 0);
    run (R"({ "type": "updateRow", "row": 1, "changes": { "mute": false } })");
    CHECK (masterVolume() > 0);

    // A solo on the beeps' row leaves the automation out too.
    run (R"({ "type": "updateRow", "row": 0, "changes": { "solo": true } })");
    CHECK (masterVolume() == 0);
}

TEST_CASE ("Levels report what plays")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-levels");
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");

    float master = 0, channel = 0;
    live.onLevels = [&] (const LiveEngine::Levels& levels)
    {
        master = std::max (master, levels.inserts.at ("master").first);
        channel = std::max (channel, levels.channels.at ("beep"));
    };

    live.play();
    audio.silence (0.5);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    live.stop();

    CHECK (master > 0.1f);
    CHECK (channel > 0.1f);
}


TEST_CASE ("Pattern mode plays the pattern on a loop instead of the song")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-pattern-mode");
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");

    // The song stays silent for its first two bars.
    model::Reader r;
    model::Command c;
    readValue (r, Json::parse (R"({ "type": "updateClip", "clip": "c", "changes": { "start": 7680 } })"), c);
    session->execute ({ c });
    live.updateNow();

    live.setLoop (std::make_pair<model::Ticks, model::Ticks> (7680, 11520));
    live.setPlayPattern ("p");
    CHECK (live.getTransportState().pattern == "p");

    // The pattern is a bar long (2 seconds at 120 BPM) with beeps on beats 1 and 2.
    auto& tc = live.getEdit()->getTransport();
    CHECK (tc.looping.get());
    CHECK (tc.getLoopRange().getEnd().inSeconds() == Approx (2.0));

    live.play();
    // Beeps at 0 and 0.5 s, then again from the top at 2 and 2.5 s.
    auto onsets = analysis::detectOnsets (audio.silence (2.8));
    REQUIRE (onsets.size() == 4);
    CHECK (onsets[0] < 0.05);
    CHECK (onsets[1] - onsets[0] == Approx (0.5).margin (0.001));
    CHECK (onsets[2] - onsets[0] == Approx (2.0).margin (0.02));
    CHECK (live.getPosition() < 3840);

    // Back to the song with its loop, playing from the loop's start, where the song's clip begins.
    live.setPlayPattern ({});
    CHECK_FALSE (live.getTransportState().pattern);
    CHECK (live.getEdit()->getTransport().getLoopRange().getStart().inSeconds() == Approx (4.0));
    CHECK (live.getPosition() >= 7680);
    CHECK (live.getPosition() < 7680 + 960);
    CHECK (analysis::detectOnsets (audio.silence (0.4)).size() == 1);

    CHECK_THROWS (live.setPlayPattern ("nope"));
    live.stop();
}

TEST_CASE ("The metronome clicks while playing")
{
    HostedAudio audio (2);
    auto dir = test::tempDir() / "live-metronome";
    std::filesystem::remove_all (dir);
    auto session = Session::open (dir);
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "metronome.json");

    live.play();
    CHECK (analysis::peakDb (audio.silence (1.2)) < -90);
    live.stop();

    live.setMetronome (true);
    CHECK (live.getTransportState().metronome);
    live.setPosition (0);
    live.play();

    // Clicks on every beat: 0, 0.5 and 1.0 s at 120 BPM.
    auto onsets = analysis::detectOnsets (audio.silence (1.2));
    CHECK (onsets.size() == 3);
    live.stop();
    live.setMetronome (false);
}

TEST_CASE ("Notes from the app's keyboard play the live channel and record into a pattern")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-notes");
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "live-notes.json");

    live.setLiveChannel ("beep");

    // The keyboard device appears after a MIDI rescan.
    for (int i = 0; i < 100; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        live.updateNow();

        if (std::any_of (test::getEngine().getDeviceManager().getMidiInDevices().begin(),
                         test::getEngine().getDeviceManager().getMidiInDevices().end(),
                         [] (auto& d) { return d->getName() == "GrooveKit keyboard"; }))
            break;
    }

    // Let the routing catch up with the new device: on a slow machine it takes a while, so the
    // note is tried again until it's heard.
    auto peak = analysis::silenceDb;

    for (int i = 0; i < 25 && peak <= -12; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        audio.silence (0.1);
        live.noteOn (60, 127);
        peak = analysis::peakDb (audio.silence (0.2));
        live.noteOff (60);
    }

    CHECK (peak > -12);

    live.startNoteRecording ({ "p", "beep", 0 });
    CHECK (live.isRecordingNotes());
    audio.silence (0.2);
    live.noteOn (64, 90);
    audio.silence (0.1);
    live.noteOff (64);
    audio.silence (0.1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    live.stop();
    CHECK_FALSE (live.isRecordingNotes());

    auto& notes = session->getProject().patterns[0].notes;
    REQUIRE (notes.size() == 3);
    CHECK (notes[2].key == 64);
    CHECK (notes[2].velocity == 90);
    CHECK (notes[2].channel == "beep");
}

TEST_CASE ("The API reaches the live engine for transport, live and record methods")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-api");
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "live-api.json");
    ApiHandler handler (*session, test::getEngineContext(), nullptr, &live);

    ApiHandler::Client client;
    Json response;
    client.send = [&] (const std::string& m) { response = Json::parse (m); };
    auto call = [&] (const std::string& method, Json params = Json::object())
    {
        return response = test::callApi (handler, client, method, params);
    };

    CHECK (call ("live.setChannel", { { "channel", "beep" } })["result"]["channel"] == "beep");
    CHECK (call ("live.noteOn", { { "key", 64 } }).contains ("result"));
    CHECK (call ("live.noteOff", { { "key", 64 } }).contains ("result"));
    CHECK (call ("transport.setMetronome", { { "countInBars", 1 } })["result"]["countInBars"] == 1);
    CHECK (call ("record.notes", { { "pattern", "p" }, { "channel", "beep" } })["result"]["recording"] == true);
    CHECK (call ("record.stop").contains ("result"));
    CHECK (call ("transport.setMetronome", { { "countInBars", 0 } })["result"]["countInBars"] == 0);
}

TEST_CASE ("Solo and mute switch the playing Edit without rebuilding it")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-solo");
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");

    auto run = [&] (const char* json)
    {
        model::Reader r;
        model::Command c;
        readValue (r, Json::parse (json), c);
        session->execute ({ c });
        live.updateNow();
    };

    auto edit = live.getEdit();

    live.play();
    CHECK (analysis::peakDb (audio.silence (0.6)) > -12);

    // Soloing a track without clips silences the one with the beeps, in the same Edit.
    run (R"({ "type": "updateRow", "row": 1, "changes": { "solo": true } })");
    CHECK (live.getEdit() == edit);
    audio.silence (0.05);
    CHECK (analysis::peakDb (audio.silence (1.0)) < -90);

    run (R"({ "type": "updateRow", "row": 1, "changes": { "solo": false } })");
    CHECK (live.getEdit() == edit);
    audio.silence (0.05);
    CHECK (analysis::peakDb (audio.silence (1.0)) > -12);

    // Muting the channel, and the clip, likewise.
    run (R"({ "type": "updateChannel", "channel": "beep", "changes": { "mute": true } })");
    CHECK (live.getEdit() == edit);
    audio.silence (0.05);
    CHECK (analysis::peakDb (audio.silence (1.0)) < -90);
    run (R"({ "type": "updateChannel", "channel": "beep", "changes": { "mute": false } })");
    run (R"({ "type": "updateClip", "clip": "c", "changes": { "mute": true } })");
    CHECK (live.getEdit() == edit);
    audio.silence (0.05);
    CHECK (analysis::peakDb (audio.silence (1.0)) < -90);

    live.stop();
}

TEST_CASE ("Notes and pattern clips change the playing Edit without rebuilding it")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-notes-in-place");
    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");
    live.updateNow();
    auto edit = live.getEdit();

    const auto notesInEdit = [&]
    {
        int n = 0;

        for (auto t : te::getAudioTracks (*live.getEdit()))
            for (auto c : t->getClips())
                if (auto midi = dynamic_cast<te::MidiClip*> (c))
                    n += midi->getSequence().getNumNotes();

        return n;
    };

    // Four bars of a two-note pattern: eight notes.
    CHECK (notesInEdit() == 8);

    model::Reader r;
    model::Command command;
    readValue (r, Json::parse (R"({ "type": "addNotes", "pattern": "p", "notes": [ { "channel": "beep", "start": 480, "length": 120 } ] })"), command);
    session->execute ({ command });
    live.updateNow();
    CHECK (live.getEdit() == edit);
    CHECK (notesInEdit() == 12);

    // The clip's length changes how often the pattern repeats.
    readValue (r, Json::parse (R"({ "type": "updateClip", "clip": "c", "changes": { "length": 7680 } })"), command);
    session->execute ({ command });
    live.updateNow();
    CHECK (live.getEdit() == edit);
    CHECK (notesInEdit() == 6);

    // Another clip of the same pattern, then taken away again.
    readValue (r, Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c2", "row": 1, "start": 7680, "length": 3840, "pattern": "p" } })"), command);
    session->execute ({ command });
    live.updateNow();
    CHECK (live.getEdit() == edit);
    CHECK (notesInEdit() == 9);

    readValue (r, Json::parse (R"({ "type": "removeClip", "clip": "c2" })"), command);
    session->execute ({ command });
    live.updateNow();
    CHECK (live.getEdit() == edit);
    CHECK (notesInEdit() == 6);

    // Notes for a channel that played none before need its track: that rebuilds.
    readValue (r, Json::parse (R"({ "type": "addChannel", "channel": { "id": "hat", "instrument": { "type": "sampler", "layers": [
                                   { "id": "l", "pitchTracking": false, "sample": ")" + test::fixture ("beep.wav").string() + R"(" } ] } } })"), command);
    session->execute ({ command });
    live.updateNow();
    readValue (r, Json::parse (R"({ "type": "addNotes", "pattern": "p", "notes": [ { "channel": "hat", "start": 0, "length": 120 } ] })"), command);
    session->execute ({ command });
    live.updateNow();
    CHECK (live.getEdit() != edit);
    CHECK (notesInEdit() == 8);
}
