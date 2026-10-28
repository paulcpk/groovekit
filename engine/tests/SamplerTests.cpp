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

    std::shared_ptr<const Sample> toneSample (int length, float hz)
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = rate;
        s->audio.setSize (1, length);

        for (int i = 0; i < length; ++i)
            s->audio.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * hz * static_cast<float> (i) / static_cast<float> (rate)));

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
        l.envelope = { .sustain = 1.0f, .release = 0.0f };
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
    l.envelope = { .attack = 0.01f, .sustain = 1.0f, .release = 0.01f }; // 480 samples each
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

TEST_CASE ("Start, end and reverse choose the part that plays")
{
    SECTION ("forward, with 1 ms fades only where the part cuts into the file")
    {
        Config config;
        auto l = layer (rampSample (1000));
        l.start = 100;
        l.end = 400;
        config.layers.push_back (l);
        Harness h (config);

        h.run (512, { Harness::on (0, 60) });
        CHECK (h.out.getSample (0, 0) == 0.0f);
        CHECK (h.out.getSample (0, 200) == Approx (0.301f));
        CHECK (h.out.getSample (0, 300) == 0.0f);
    }

    SECTION ("reversed")
    {
        Config config;
        auto l = layer (rampSample (1000));
        l.reverse = true;
        config.layers.push_back (l);
        Harness h (config);

        h.run (1024, { Harness::on (0, 60) });
        CHECK (h.out.getSample (0, 0) == Approx (1.0f));
        CHECK (h.out.getSample (0, 500) == Approx (0.5f));
        CHECK (h.out.getSample (0, 999) == Approx (0.001f));
    }
}

TEST_CASE ("The filter shapes every voice")
{
    // A 6 kHz sine.
    auto tone = std::make_shared<Sample>();
    tone->sampleRate = rate;
    tone->audio.setSize (1, 4800);

    for (int i = 0; i < 4800; ++i)
        tone->audio.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 6000.0f * static_cast<float> (i) / static_cast<float> (rate)));

    auto rms = [] (Harness& h) { return h.out.getRMSLevel (0, 2400, 2400); };

    Config config;
    config.layers.push_back (layer (tone));

    Harness open (config);
    open.run (4800, { Harness::on (0, 60) });
    CHECK (rms (open) == Approx (0.3536f).margin (0.01f));

    // 12 dB per octave: about 31 dB down, 2.6 octaves above the cutoff.
    config.filter.type = Filter::Type::lowpass;
    config.filter.cutoff = 1000;
    Harness lowpass (config);
    lowpass.run (4800, { Harness::on (0, 60) });
    CHECK (rms (lowpass) < 0.015f);

    // The envelope opens it up: 5 octaves up from 600 Hz is past the top of the range.
    config.filter.cutoff = 600;
    config.filter.envelopeAmount = 10;
    Harness opened (config);
    opened.run (4800, { Harness::on (0, 60) });
    CHECK (rms (opened) > 0.2f);
}

TEST_CASE ("New settings for the same samples keep notes playing")
{
    Config config;
    auto l = layer (constantSample (48000, 0.5f));
    l.oneShot = false;
    config.layers.push_back (l);
    Harness h (config);

    h.run (64, { Harness::on (0, 60) });
    CHECK (h.engine.getNumActiveVoices() == 1);

    config.layers[0].gain = 0.5f;
    config.filter.type = Filter::Type::lowpass;
    h.engine.setConfig (std::make_shared<Config> (config));
    CHECK (h.engine.getNumActiveVoices() == 1);
    h.run (256); // the filter settles

    // Another sample stops them, fading out over a few milliseconds rather than with a click.
    config.layers[0].sample = constantSample (100, 0.5f);
    h.engine.setConfig (std::make_shared<Config> (config));
    h.run (256);
    CHECK (h.out.getSample (0, 0) > 0.1f);
    CHECK (std::abs (h.out.getSample (0, 0) - h.out.getSample (0, 1)) < 0.01f);
    CHECK (h.out.getSample (0, 200) == 0.0f);
    CHECK (h.engine.getNumActiveVoices() == 0);
}

TEST_CASE ("Envelopes have a delay, a hold and curves")
{
    EnvelopeShape shape { .delay = 0.01f, .attack = 0.01f, .hold = 0.01f, .decay = 0.01f, .sustain = 0.5f, .release = 0.01f };

    CHECK (shape.levelBeforeRelease (0.005) == 0.0f);
    CHECK (shape.levelBeforeRelease (0.015) == Approx (0.5f));
    CHECK (shape.levelBeforeRelease (0.025) == 1.0f);
    CHECK (shape.levelBeforeRelease (0.035) == Approx (0.75f));
    CHECK (shape.levelBeforeRelease (1.0) == 0.5f);

    // Over an audio clip, the release ends where the clip does.
    CHECK (shape.levelAt (1.0, 0.005) == Approx (0.25f));

    SECTION ("the generator follows the same shape, sample by sample")
    {
        EnvelopeGenerator g;
        g.setSampleRate (rate);
        g.setShape (shape);
        g.noteOn();

        for (int i = 0; i < 720; ++i)
            g.next();

        CHECK (g.next() == Approx (0.5f).margin (0.01f));

        for (int i = 0; i < 2000; ++i)
            g.next();

        CHECK (g.getLevel() == 0.5f);
        g.noteOff();

        for (int i = 0; i < 240; ++i)
            g.next();

        CHECK (g.next() == Approx (0.25f).margin (0.01f));

        for (int i = 0; i < 300; ++i)
            g.next();

        CHECK (! g.isActive());
    }

    SECTION ("a positive curve rises fast and then eases, a negative one starts slowly")
    {
        shape.attackCurve = 0.5f;
        CHECK (shape.levelBeforeRelease (0.015) > 0.7f);
        shape.attackCurve = -0.5f;
        CHECK (shape.levelBeforeRelease (0.015) < 0.3f);
    }

    SECTION ("without an attack, the first sample is at full level")
    {
        EnvelopeGenerator g;
        g.setSampleRate (rate);
        g.setShape ({ .sustain = 1.0f });
        g.noteOn();
        CHECK (g.next() == 1.0f);
    }
}

TEST_CASE ("The steeper filters cut more, and the notch takes out its frequency")
{
    auto rms = [] (Harness& h) { return h.out.getRMSLevel (0, 2400, 2400); };
    auto play = [&] (Filter::Type type, float cutoff)
    {
        Config config;
        config.layers.push_back (layer (toneSample (4800, 6000)));
        config.filter.type = type;
        config.filter.cutoff = cutoff;
        Harness h (config);
        h.run (4800, { Harness::on (0, 60) });
        return rms (h);
    };

    const auto gentle = play (Filter::Type::lowpass, 1000), steep = play (Filter::Type::lowpass24, 1000);
    CHECK (steep < gentle / 10);
    CHECK (play (Filter::Type::highpass24, 1000) == Approx (0.3536f).margin (0.01f));
    CHECK (play (Filter::Type::highpass24, 20000) < 0.01f);
    CHECK (play (Filter::Type::notch, 6000) < 0.02f);
}

TEST_CASE ("Modulation moves the pitch, pan and volume of each note")
{
    SECTION ("a pitch envelope at half its amount plays an octave up")
    {
        Config config;
        config.layers.push_back (layer (rampSample (1000)));
        config.modulation.pitch.amount = 0.5f;
        config.modulation.pitch.envelope = { .sustain = 1.0f };
        Harness h (config);

        h.run (512, { Harness::on (0, 60) });
        CHECK (h.out.getSample (0, 1) == Approx (0.003f));
        CHECK (h.out.getSample (0, 100) == Approx (0.201f));
    }

    SECTION ("a square LFO on the pan swings between the sides")
    {
        Config config;
        config.layers.push_back (layer (constantSample (4800, 0.5f)));
        config.modulation.pan.lfo = { .shape = LfoSettings::Shape::square, .amount = 1.0f, .speed = 100.0f };
        Harness h (config);

        h.run (480, { Harness::on (0, 60) });
        CHECK (h.out.getSample (0, 100) == Approx (0.0f).margin (0.001f));
        CHECK (h.out.getSample (1, 100) == Approx (0.5f));
        CHECK (h.out.getSample (0, 300) == Approx (0.5f));
        CHECK (h.out.getSample (1, 300) == Approx (0.0f).margin (0.001f));
    }

    SECTION ("tremolo dips the volume by its amount")
    {
        Config config;
        config.layers.push_back (layer (constantSample (4800, 0.5f)));
        config.modulation.volume = { .shape = LfoSettings::Shape::square, .amount = 0.5f, .speed = 100.0f };
        Harness h (config);

        h.run (480, { Harness::on (0, 60) });
        CHECK (h.out.getSample (0, 100) == Approx (0.5f));
        CHECK (h.out.getSample (0, 300) == Approx (0.25f));
    }

    SECTION ("an LFO waits for its delay")
    {
        Config config;
        config.layers.push_back (layer (constantSample (4800, 0.5f)));
        config.modulation.volume = { .shape = LfoSettings::Shape::square, .amount = 1.0f, .speed = 100.0f, .delay = 0.01f };
        Harness h (config);

        h.run (480, { Harness::on (0, 60) });
        CHECK (h.out.getSample (0, 300) == Approx (0.5f));
    }
}

TEST_CASE ("Layers loop, fade and swap their sides")
{
    SECTION ("a forward loop goes round until the note ends")
    {
        Config config;
        auto l = layer (rampSample (100));
        l.loop = Layer::Loop::forward;
        config.layers.push_back (l);
        Harness h (config);

        h.run (256, { Harness::on (0, 60), Harness::off (200, 60) });
        CHECK (h.out.getSample (0, 150) == Approx (0.051f));
        CHECK (h.out.getSample (0, 210) == 0.0f);
    }

    SECTION ("a ping-pong loop turns at the end")
    {
        Config config;
        auto l = layer (rampSample (100));
        l.loop = Layer::Loop::pingPong;
        config.layers.push_back (l);
        Harness h (config);

        h.run (128, { Harness::on (0, 60) });
        CHECK (h.out.getSample (0, 99) == Approx (0.100f));
        CHECK (h.out.getSample (0, 105) == Approx (0.094f));
    }

    SECTION ("fades in and out over the part that plays")
    {
        Config config;
        auto l = layer (constantSample (1000, 1.0f));
        l.fadeIn = 100;
        l.fadeOut = 100;
        config.layers.push_back (l);
        Harness h (config);

        h.run (1024, { Harness::on (0, 60) });
        CHECK (h.out.getSample (0, 50) == Approx (0.5f));
        CHECK (h.out.getSample (0, 500) == 1.0f);
        CHECK (h.out.getSample (0, 950) == Approx (0.5f));
    }

    SECTION ("left and right swap")
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = rate;
        s->audio.setSize (2, 100);
        s->audio.clear();

        for (int i = 0; i < 100; ++i)
            s->audio.setSample (1, i, 0.5f);

        Config config;
        auto l = layer (s);
        l.swapStereo = true;
        config.layers.push_back (l);
        Harness h (config);

        h.run (16, { Harness::on (0, 60) });
        CHECK (h.out.getSample (0, 8) == 0.5f);
        CHECK (h.out.getSample (1, 8) == 0.0f);
    }
}

TEST_CASE ("Transient shaping sharpens hits or brings up their tails")
{
    // A 200 Hz hit that dies away.
    juce::AudioBuffer<float> hit (1, 24000);

    for (int i = 0; i < hit.getNumSamples(); ++i)
    {
        const auto t = static_cast<float> (i) / static_cast<float> (rate);
        hit.setSample (0, i, 0.8f * std::exp (-t / 0.08f) * std::sin (juce::MathConstants<float>::twoPi * 200.0f * t));
    }

    const auto attackToTail = [] (const juce::AudioBuffer<float>& b) { return b.getMagnitude (0, 0, 240) / b.getRMSLevel (0, 4800, 4800); };
    const auto original = attackToTail (hit);

    CHECK (attackToTail (shapeTransients (hit, rate, 1.0f, 0.0f)) > original * 1.5f);
    CHECK (attackToTail (shapeTransients (hit, rate, -1.0f, 0.0f)) < original / 1.5f);
    CHECK (attackToTail (shapeTransients (hit, rate, 0.0f, 1.0f)) < original / 1.5f);
}

TEST_CASE ("Grains move the pitch apart from the speed")
{
    // A second of 220 Hz.
    const auto tone = toneSample (static_cast<int> (rate), 220);

    // Where the output stops sounding, in seconds, and its frequency (from zero crossings) in its
    // first half.
    auto measure = [] (const juce::AudioBuffer<float>& b)
    {
        int last = 0;

        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (b.getSample (0, i)) > 0.01f)
                last = i;

        int crossings = 0;
        const auto from = static_cast<int> (0.1 * rate), to = std::max (from + 1, last / 2);

        for (int i = from + 1; i < to; ++i)
            if ((b.getSample (0, i - 1) < 0) != (b.getSample (0, i) < 0))
                ++crossings;

        return std::pair { last / rate, crossings / 2.0 / ((to - from) / rate) };
    };

    auto play = [&] (Grains grains, int note, double seconds = 2.5)
    {
        auto l = layer (tone);
        l.grains = grains;
        Config config;
        config.layers = { l };
        Harness h (config);
        h.run (static_cast<int> (seconds * rate), { Harness::on (0, note) });
        return measure (h.out);
    };

    SECTION ("straight, an octave up is twice as fast")
    {
        const auto [length, hz] = play ({}, 72);
        CHECK (length == Approx (0.5).margin (0.01));
        CHECK (hz == Approx (440).margin (5));
    }

    SECTION ("in grains, an octave up keeps the timing")
    {
        const auto [length, hz] = play ({ .on = true, .spray = 0 }, 72);
        CHECK (length == Approx (1.0).margin (0.1));
        CHECK (hz == Approx (440).margin (8));
    }

    SECTION ("half speed is twice as long, at the same pitch")
    {
        const auto [length, hz] = play ({ .on = true, .speed = 0.5f, .spray = 0 }, 60);
        CHECK (length == Approx (2.0).margin (0.12));
        CHECK (hz == Approx (220).margin (5));
    }

    SECTION ("at its own pitch and pace, grains sound like the sample")
    {
        auto l = layer (tone);
        l.grains = { .on = true, .spray = 0 };
        Config config;
        config.layers = { l };
        Harness h (config);
        h.run (static_cast<int> (0.9 * rate), { Harness::on (0, 60) });
        const auto level = h.out.getRMSLevel (0, static_cast<int> (0.2 * rate), static_cast<int> (0.6 * rate));
        CHECK (juce::Decibels::gainToDecibels (level) == Approx (juce::Decibels::gainToDecibels (0.5f / std::sqrt (2.0f))).margin (1.0));
    }

    SECTION ("few short grains leave space between them")
    {
        auto l = layer (tone);
        l.grains = { .on = true, .size = 0.02f, .density = 8, .spray = 0, .shape = Grains::Shape::pluck };
        Config config;
        config.layers = { l };
        Harness h (config);
        h.run (static_cast<int> (rate), { Harness::on (0, 60) });

        int silent = 0, windows = 0;

        for (int start = 0; start + 480 <= h.out.getNumSamples(); start += 480, ++windows)
            if (h.out.getMagnitude (0, start, 480) < 0.001f)
                ++silent;

        CHECK (silent > windows / 2);
    }

    SECTION ("held still, grains play on until the note ends")
    {
        auto l = layer (tone);
        l.grains = { .on = true, .speed = 0 };
        Config config;
        config.layers = { l };
        Harness h (config);
        h.run (static_cast<int> (3 * rate), { Harness::on (0, 60), Harness::off (static_cast<int> (2.5 * rate), 60) });
        CHECK (h.out.getRMSLevel (0, static_cast<int> (2.0 * rate), static_cast<int> (0.4 * rate)) > 0.1f);
        CHECK (h.out.getMagnitude (0, static_cast<int> (2.6 * rate), static_cast<int> (0.4 * rate)) == 0.0f);
    }
}
