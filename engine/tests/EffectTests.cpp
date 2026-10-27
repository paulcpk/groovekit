#include "TestHelpers.h"

#include "effects/BuiltinEffects.h"
#include "effects/EffectPlugin.h"
#include "effects/Saturation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gk;
using Catch::Approx;
using model::BuiltinEffectKind;

namespace
{
    constexpr double rate = 48000;

    /** One of our effects outside a song, fed buffers directly. */
    struct Harness
    {
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (test::getEngine());
        tracktion::Plugin::Ptr plugin;

        Harness (BuiltinEffectKind kind, std::map<std::string, double> params)
        {
            model::BuiltinEffect effect;
            effect.id = "fx";
            effect.kind = kind;
            effect.params = std::move (params);
            plugin = effects::createEffect (*edit, effect);
            plugin->baseClassInitialise ({ tracktion::TimePosition(), rate, 512 });
        }

        ~Harness()
        {
            plugin->baseClassDeinitialise();
        }

        void process (juce::AudioBuffer<float>& buffer)
        {
            for (int start = 0; start < buffer.getNumSamples(); start += 512)
            {
                tracktion::PluginRenderContext context (&buffer, juce::AudioChannelSet::stereo(), start,
                                                        std::min (512, buffer.getNumSamples() - start), nullptr, 0.0,
                                                        {}, true, false, true, false);
                plugin->applyToBuffer (context);
            }
        }

        model::Json meter (const model::Json& params = model::Json::object())
        {
            return dynamic_cast<effects::EffectPlugin&> (*plugin).readMeter (params);
        }
    };

    juce::AudioBuffer<float> sine (double hz, float amplitude, int samples, float rightAmplitude = -1)
    {
        juce::AudioBuffer<float> b (2, samples);

        for (int i = 0; i < samples; ++i)
        {
            const auto s = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));
            b.setSample (0, i, amplitude * s);
            b.setSample (1, i, (rightAmplitude < 0 ? amplitude : rightAmplitude) * s);
        }

        return b;
    }

    float peakDb (const juce::AudioBuffer<float>& b, int from, int ch = 0)
    {
        return juce::Decibels::gainToDecibels (b.getMagnitude (ch, from, b.getNumSamples() - from), -200.0f);
    }

    float rmsDb (const juce::AudioBuffer<float>& b, int from, int ch = 0)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (ch, from, b.getNumSamples() - from), -200.0f);
    }

    /** The level in dB of an impulse response at a frequency. */
    double responseDb (const juce::AudioBuffer<float>& impulse, double hz)
    {
        std::complex<double> sum;

        for (int i = 0; i < impulse.getNumSamples(); ++i)
            sum += static_cast<double> (impulse.getSample (0, i)) * std::polar (1.0, -juce::MathConstants<double>::twoPi * hz * i / rate);

        return 20 * std::log10 (std::abs (sum));
    }
}

TEST_CASE ("The compressor follows its curve, and measures what it does")
{
    // A -6 dB sine, threshold -20, ratio 4: 14 dB over comes out as 3.5.
    Harness h (BuiltinEffectKind::compressor, { { "thresholdDb", -20 }, { "ratio", 4 }, { "attackMs", 1 }, { "releaseMs", 200 } });
    auto b = sine (1000, 0.5f, 48000);
    h.process (b);
    CHECK (peakDb (b, 24000) == Approx (-16.5).margin (0.5));

    auto meter = h.meter();
    REQUIRE (meter["frames"].size() > 100);
    auto last = meter["frames"].back();
    CHECK (last[0].get<double>() == Approx (-6).margin (0.2));
    CHECK (last[1].get<double>() == Approx (-16.5).margin (0.6));
    CHECK (last[2].get<double>() == Approx (-10.5).margin (0.6));

    // Asking from a position returns only what came after it.
    const auto position = meter["position"].get<int64_t>();
    auto more = sine (1000, 0.5f, 4800);
    h.process (more);
    CHECK (h.meter ({ { "since", position } })["frames"].size() == 20);
}

TEST_CASE ("The compressor at 1:1 leaves the sound alone")
{
    Harness h (BuiltinEffectKind::compressor, { { "thresholdDb", -40 }, { "ratio", 1 } });
    auto b = sine (440, 0.8f, 4800);
    const auto original = b;
    h.process (b);

    for (int i = 0; i < b.getNumSamples(); ++i)
        REQUIRE (b.getSample (0, i) == Approx (original.getSample (0, i)).margin (1.0e-5));
}

TEST_CASE ("The multiband's bands sum back flat")
{
    for (auto bands : { 0.0, 1.0 })
    {
        INFO ((bands == 0 ? "three" : "four") << " bands");
        Harness h (BuiltinEffectKind::multiband, { { "bands", bands }, { "band1Ratio", 1 }, { "band2Ratio", 1 }, { "band3Ratio", 1 }, { "band4Ratio", 1 } });
        juce::AudioBuffer<float> impulse (2, 16384);
        impulse.clear();
        impulse.setSample (0, 0, 1);
        impulse.setSample (1, 0, 1);
        h.process (impulse);

        for (auto hz : { 30.0, 100.0, 150.0, 400.0, 1000.0, 3000.0, 5000.0, 9000.0, 16000.0 })
        {
            INFO (hz << " Hz");
            CHECK (responseDb (impulse, hz) == Approx (0).margin (0.1));
        }
    }
}

TEST_CASE ("The multiband compresses each band on its own")
{
    // A loud low sine and a quiet high one: only the low band is pulled down.
    Harness h (BuiltinEffectKind::multiband, { { "band1ThresholdDb", -20 }, { "band1Ratio", 4 }, { "band1AttackMs", 1 }, { "kneeDb", 0 },
                                               { "band3ThresholdDb", -20 }, { "band3Ratio", 4 } });
    auto b = sine (50, 0.5f, 48000);
    auto high = sine (8000, 0.02f, 48000);

    for (int ch = 0; ch < 2; ++ch)
        b.addFrom (ch, 0, high, ch, 0, b.getNumSamples());

    h.process (b);
    auto last = h.meter()["frames"].back();
    CHECK (last[2 + 2].get<double>() == Approx (-10.5).margin (1.0));   // low band's gain
    CHECK (last[2 + 3 * 2 + 2].get<double>() == Approx (0).margin (0.1)); // high band's
    CHECK (last[2 + 3 * 3].get<double>() == -100);                       // no fourth band
}

TEST_CASE ("The multiband lifts quiet parts with an upward ratio")
{
    // A -50 dB sine in the middle band, lifted below -30 at 2:1: 20 dB under comes up 10.
    Harness h (BuiltinEffectKind::multiband, { { "band2Ratio", 1 }, { "band2BelowDb", -30 }, { "band2BelowRatio", 2 }, { "detect", 1 } });
    auto b = sine (1000, std::pow (10.0f, -50.0f / 20.0f), 48000);
    h.process (b);
    CHECK (peakDb (b, 24000) == Approx (-40).margin (1.0));
}

TEST_CASE ("The multiband's solo and mute pick bands")
{
    auto run = [] (std::map<std::string, double> params, double hz)
    {
        Harness h (BuiltinEffectKind::multiband, params);
        auto b = sine (hz, 0.5f, 24000);
        h.process (b);
        return rmsDb (b, 12000);
    };

    CHECK (run ({ { "band1Mode", 2 } }, 8000) < -40);                    // low band soloed: the highs go
    CHECK (run ({ { "band1Mode", 2 }, { "band1Ratio", 1 } }, 40) > -10); // the lows stay
    CHECK (run ({ { "band1Mode", 3 } }, 40) < -30);                      // low band muted
    CHECK (run ({ { "band3Mode", 1 }, { "band3ThresholdDb", -40 } }, 12000) > -10); // bypassed: not compressed
}

TEST_CASE ("The bitcrusher quantises and holds")
{
    // 3 bits: steps of a quarter. 4.8 kHz at 48 kHz: a new value every ten samples.
    Harness h (BuiltinEffectKind::bitcrusher, { { "bits", 3 }, { "rateHz", 4800 } });
    auto b = sine (100, 0.9f, 4800);
    h.process (b);
    int changes = 0;

    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const auto v = b.getSample (0, i);
        REQUIRE (v * 4 == Approx (std::round (v * 4)).margin (1.0e-5));

        if (i > 0 && v != b.getSample (0, i - 1))
            ++changes;
    }

    CHECK (changes <= b.getNumSamples() / 10);
    CHECK (changes > 10);
}

TEST_CASE ("The stereo field swaps, narrows, delays and keeps the bass mono")
{
    SECTION ("swap")
    {
        Harness h (BuiltinEffectKind::stereo, { { "leftToLeft", 0 }, { "rightToLeft", 100 }, { "leftToRight", 100 }, { "rightToRight", 0 } });
        auto b = sine (440, 0.5f, 9600, 0.1f);
        h.process (b);
        CHECK (peakDb (b, 4800, 0) == Approx (-20).margin (0.1));
        CHECK (peakDb (b, 4800, 1) == Approx (-6.02).margin (0.1));
    }

    SECTION ("width 0 is mono")
    {
        Harness h (BuiltinEffectKind::stereo, { { "width", 0 } });
        auto b = sine (440, 0.5f, 9600, 0.1f);
        h.process (b);

        for (int i = 4800; i < b.getNumSamples(); ++i)
            REQUIRE (b.getSample (0, i) == Approx (b.getSample (1, i)).margin (1.0e-5));
    }

    SECTION ("delay")
    {
        Harness h (BuiltinEffectKind::stereo, { { "delayMs", 1 } });
        juce::AudioBuffer<float> b (2, 4096);
        b.clear();
        b.setSample (0, 2000, 1);
        b.setSample (1, 2000, 1);
        h.process (b);
        CHECK (b.getSample (0, 2000) == Approx (1));
        CHECK (b.getSample (1, 2048) == Approx (1).margin (1.0e-4));
    }

    SECTION ("mono bass")
    {
        // A low sine on the left only comes out the same on both sides; a high one stays left.
        Harness h (BuiltinEffectKind::stereo, { { "monoBelow", 200 } });
        auto low = sine (40, 0.5f, 48000, 0);
        h.process (low);
        CHECK (rmsDb (low, 24000, 1) == Approx (rmsDb (low, 24000, 0)).margin (0.3));

        auto high = sine (4000, 0.5f, 48000, 0);
        h.process (high);
        CHECK (rmsDb (high, 24000, 1) < -40);

        // And it keeps the latest output for the scope.
        auto meter = h.meter ({ { "samples", 256 } });
        CHECK (meter["left"].size() == 256);
        CHECK (meter["right"].size() == 256);
    }
}

TEST_CASE ("The exciter warms without getting louder")
{
    for (int model = 0; model < static_cast<int> (effects::ExciterPlugin::modelNames().size()); ++model)
    {
        INFO (effects::ExciterPlugin::modelNames()[static_cast<size_t> (model)]);
        Harness h (BuiltinEffectKind::exciter, { { "model", model }, { "driveDb", 12 }, { "harmonics", 0 } });
        auto b = sine (1000, 0.3f, 48000);
        const auto before = rmsDb (b, 24000);
        h.process (b);
        CHECK (rmsDb (b, 24000) == Approx (before).margin (3.0));
        CHECK (h.meter()["frames"].size() > 100);
    }

    // At no drive, level-matched, the stage is close to straight.
    for (int model = 0; model < static_cast<int> (effects::ExciterPlugin::modelNames().size()); ++model)
        CHECK (effects::ExciterPlugin::stage (model, 0.1f, 1.0f, 0.0f) / effects::ExciterPlugin::stageGain (model, 1.0f, 0.0f) == Approx (0.1f).epsilon (0.05));
}

TEST_CASE ("The exciter's harmonics brighten")
{
    auto highs = [] (double harmonics)
    {
        Harness h (BuiltinEffectKind::exciter, { { "driveDb", 0 }, { "harmonics", harmonics }, { "harmonicsHz", 3000 } });
        auto b = sine (2500, 0.3f, 48000);
        h.process (b);

        // How much above 6 kHz: the 2500 Hz sine's harmonics.
        juce::AudioBuffer<float> tail (1, 16384);
        tail.copyFrom (0, 0, b, 0, 24000, 16384);
        return responseDb (tail, 5000) + responseDb (tail, 7500);
    };

    CHECK (highs (100) > highs (0) + 6);
}
