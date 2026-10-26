// Real-time playback and recording, driven block by block through Tracktion's hosted audio device.

#include "TestHelpers.h"

#include "analysis/Analysis.h"
#include "live/LiveEngine.h"
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
                 Json::parse (R"({ "type": "addTrack", "track": { "id": "t" } })"),
                 Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "track": "t", "start": 0, "length": 15360, "pattern": "p" } })") })
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

        auto later = audio.silence (1.0);
        CHECK (analysis::peakDb (later) == Approx (-12.04).margin (0.1));
    }

    live.stop();
    CHECK_FALSE (live.isPlaying());
}

TEST_CASE ("Recording an input adds the take to the project")
{
    HostedAudio audio (2);
    auto session = beepSession ("live-recording");

    model::Reader r;
    model::Command c;
    readValue (r, Json::parse (R"({ "type": "addTrack", "track": { "id": "takes" } })"), c);
    session->execute ({ c });
    readValue (r, Json::parse (R"({ "type": "updateTrack", "track": "t", "changes": { "mute": true } })"), c);
    session->execute ({ c });

    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");
    auto inputs = live.getInputNames();
    REQUIRE_FALSE (inputs.empty());

    live.startRecording ({ { inputs[0], "master", "takes" } });
    REQUIRE (live.isRecording());

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
    CHECK (take.track == "takes");
    INFO ("sample: " << take.sample);
    CHECK (take.sample.starts_with ("recordings/"));
    CHECK (std::filesystem::exists (*session->getProjectDir() / take.sample));
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
    CHECK (std::filesystem::exists (*session->getProjectDir() / take.sample));
}
