#include "sampler/SamplerEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gk::sampler;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000;

    /** A mono sample whose values are 0.001, 0.002, ... so playback position is easy to read back. */
    std::shared_ptr<const Sample> rampSample (int length)
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = rate;
        s->audio.setSize (1, length);

        for (int i = 0; i < length; ++i)
            s->audio.setSample (0, i, static_cast<float> (i + 1) * 0.001f);

        return s;
    }

    std::shared_ptr<const Sample> constantSample (int length, float value)
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = rate;
        s->audio.setSize (1, length);

        for (int i = 0; i < length; ++i)
            s->audio.setSample (0, i, value);

        return s;
    }

    Layer layer (std::shared_ptr<const Sample> sample)
    {
        Layer l;
        l.sample = std::move (sample);
        l.envelope = { 0.0f, 0.0f, 1.0f, 0.0f };
        return l;
    }

    struct Harness
    {
        SamplerEngine engine;
        juce::AudioBuffer<float> out { 2, 4096 };

        explicit Harness (Config config)
        {
            engine.prepare (rate, 4096);
            engine.setConfig (std::make_shared<Config> (std::move (config)));
        }

        void run (int numSamples, std::vector<SamplerEngine::MidiEvent> events = {})
        {
            out.setSize (2, numSamples, false, false, true);
            out.clear();
            engine.process (out, 0, numSamples, events);
        }

        static SamplerEngine::MidiEvent on (int offset, int note, int velocity = 127)
        {
            return { offset, juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (velocity)) };
        }

        static SamplerEngine::MidiEvent off (int offset, int note)
        {
            return { offset, juce::MidiMessage::noteOff (1, note) };
        }
    };
}

TEST_CASE ("At root pitch the sampler plays the sample data exactly, sample-accurately")
{
    Config config;
    config.layers.push_back (layer (rampSample (100)));
    Harness h (config);

    h.run (256, { Harness::on (10, 60) });

    for (int i = 0; i < 10; ++i)
        CHECK (h.out.getSample (0, i) == 0.0f);

    for (int i = 0; i < 100; ++i)
    {
        CHECK (h.out.getSample (0, 10 + i) == static_cast<float> (i + 1) * 0.001f);
        CHECK (h.out.getSample (1, 10 + i) == static_cast<float> (i + 1) * 0.001f);
    }

    CHECK (h.out.getSample (0, 110) == 0.0f);
    CHECK (h.engine.getNumActiveVoices() == 0);
}

TEST_CASE ("Velocity scales the output linearly")
{
    Config config;
    config.layers.push_back (layer (constantSample (100, 0.5f)));
    Harness h (config);

    h.run (64, { Harness::on (0, 60, 64) });
    CHECK (h.out.getSample (0, 0) == Approx (0.5f * 64.0f / 127.0f));
}

TEST_CASE ("Pitch tracking resamples relative to the root key")
{
    Config config;
    config.layers.push_back (layer (rampSample (1000)));
    Harness h (config);

    // An octave up plays twice as fast: every other sample, finishing in half the time.
    h.run (1024, { Harness::on (0, 72) });
    CHECK (h.out.getSample (0, 0) == Approx (0.001f));
    CHECK (h.out.getSample (0, 1) == Approx (0.003f));
    CHECK (h.out.getSample (0, 100) == Approx (0.201f));
    CHECK (h.out.getSample (0, 501) == 0.0f);

    SECTION ("unless it is disabled")
    {
        config.layers[0].pitchTracking = false;
        Harness fixed (config);
        fixed.run (16, { Harness::on (0, 72) });
        CHECK (fixed.out.getSample (0, 1) == 0.002f);
    }
}

TEST_CASE ("Notes are polyphonic and every matching layer plays")
{
    Config config;
    config.layers.push_back (layer (constantSample (1000, 0.25f)));
    config.layers.push_back (layer (constantSample (1000, 0.125f)));
    Harness h (config);

    h.run (16, { Harness::on (0, 60), Harness::on (0, 60), Harness::on (4, 64) });

    CHECK (h.engine.getNumActiveVoices() == 6);
    CHECK (h.out.getSample (0, 0) == Approx (2 * (0.25f + 0.125f)));
    CHECK (h.out.getSample (0, 8) == Approx (3 * (0.25f + 0.125f)));
}

TEST_CASE ("Key and velocity ranges select layers")
{
    Config config;
    auto soft = layer (constantSample (100, 0.1f));
    soft.velocityHigh = 64;
    auto hard = layer (constantSample (100, 0.4f));
    hard.velocityLow = 65;
    auto high = layer (constantSample (100, 0.8f));
    high.keyLow = 72;
    config.layers = { soft, hard, high };

    Harness h (config);

    h.run (4, { Harness::on (0, 60, 127) });
    CHECK (h.out.getSample (0, 0) == Approx (0.4f));

    h.engine.allNotesOff();
    h.run (4, { Harness::on (0, 60, 64) });
    CHECK (h.out.getSample (0, 0) == Approx (0.1f * 64 / 127));

    h.engine.allNotesOff();
    h.run (4, { Harness::on (0, 72, 127) });
    CHECK (h.out.getSample (0, 0) == Approx (0.4f + 0.8f));
}

TEST_CASE ("Exceeding the polyphony fades out the oldest voice")
{
    Config config;
    config.polyphony = 2;
    config.layers.push_back (layer (constantSample (48000, 0.25f)));
    Harness h (config);

    h.run (16, { Harness::on (0, 60), Harness::on (1, 62), Harness::on (2, 64) });
    CHECK (h.engine.getNumActiveVoices() == 3); // the stolen voice is still fading

    const auto fade = static_cast<int> (std::round (SamplerEngine::stealFadeSeconds * rate));
    h.run (fade + 16);
    CHECK (h.engine.getNumActiveVoices() == 2);
    CHECK (h.out.getSample (0, fade + 8) == Approx (0.5f));
}

TEST_CASE ("Gate mode releases on note-off, one-shot ignores it")
{
    Config config;
    config.layers.push_back (layer (constantSample (1000, 0.5f)));
    config.layers[0].oneShot = false;
    Harness gate (config);

    gate.run (64, { Harness::on (0, 60), Harness::off (32, 60) });
    CHECK (gate.out.getSample (0, 31) == 0.5f);
    CHECK (gate.out.getSample (0, 40) == 0.0f);
    CHECK (gate.engine.getNumActiveVoices() == 0);

    config.layers[0].oneShot = true;
    Harness oneShot (config);
    oneShot.run (64, { Harness::on (0, 60), Harness::off (32, 60) });
    CHECK (oneShot.out.getSample (0, 40) == 0.5f);
    CHECK (oneShot.engine.getNumActiveVoices() == 1);
}

TEST_CASE ("The envelope shapes the attack and release")
{
    Config config;
    auto l = layer (constantSample (48000, 1.0f));
    l.oneShot = false;
    l.envelope = { 0.01f, 0.0f, 1.0f, 0.01f }; // 480 samples each
    config.layers.push_back (l);
    Harness h (config);

    h.run (2048, { Harness::on (0, 60), Harness::off (1000, 60) });

    CHECK (h.out.getSample (0, 0) < 0.01f);
    CHECK (h.out.getSample (0, 240) == Approx (0.5f).margin (0.01f));
    CHECK (h.out.getSample (0, 600) == 1.0f);
    CHECK (h.out.getSample (0, 1240) == Approx (0.5f).margin (0.01f));
    CHECK (h.out.getSample (0, 1600) == 0.0f);
}

TEST_CASE ("Layer pan uses a balance law")
{
    Config config;
    auto l = layer (constantSample (100, 0.5f));
    l.pan = -1.0f;
    config.layers.push_back (l);
    Harness h (config);

    h.run (4, { Harness::on (0, 60) });
    CHECK (h.out.getSample (0, 0) == 0.5f);
    CHECK (h.out.getSample (1, 0) == 0.0f);
}
