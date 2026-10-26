#include "analysis/Analysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gk::analysis;
using Catch::Approx;

namespace
{
    Audio sine (double freq, double seconds, float amplitudeLeft, float amplitudeRight, double rate = 48000)
    {
        Audio a;
        a.sampleRate = rate;
        a.buffer.setSize (2, static_cast<int> (seconds * rate));

        for (int i = 0; i < a.buffer.getNumSamples(); ++i)
        {
            const auto s = static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / rate));
            a.buffer.setSample (0, i, amplitudeLeft * s);
            a.buffer.setSample (1, i, amplitudeRight * s);
        }

        return a;
    }
}

TEST_CASE ("A -23 dBFS 1 kHz stereo sine measures -23 LUFS (EBU Tech 3341)")
{
    const auto amp = juce::Decibels::decibelsToGain (-23.0f);

    CHECK (integratedLufs (sine (1000, 20, amp, amp)) == Approx (-23.0).margin (0.1));
    CHECK (integratedLufs (sine (1000, 20, amp, amp, 44100)) == Approx (-23.0).margin (0.1));
}

TEST_CASE ("Integrated loudness gates silence")
{
    const auto amp = juce::Decibels::decibelsToGain (-23.0f);
    auto a = sine (1000, 20, amp, amp);

    // Silence the second half: the relative gate ignores it, so the result barely moves.
    for (int ch = 0; ch < 2; ++ch)
        a.buffer.clear (ch, a.buffer.getNumSamples() / 2, a.buffer.getNumSamples() / 2);

    CHECK (integratedLufs (a) == Approx (-23.0).margin (0.1));

    a.buffer.clear();
    CHECK (integratedLufs (a) == silenceDb);
}

TEST_CASE ("Peak and RMS work per channel and per range")
{
    auto a = sine (1000, 1, 0.5f, 0.25f);

    CHECK (peakDb (a) == Approx (-6.02).margin (0.01));
    CHECK (peakDb (a, { 0, {}, 1 }) == Approx (-12.04).margin (0.01));
    CHECK (rmsDb (a, { 0, {}, 0 }) == Approx (-9.03).margin (0.01));
    CHECK (peakDb (a, { 0.5, 0.5, {} }) == silenceDb);
}

TEST_CASE ("Onsets are found after silence")
{
    Audio a;
    a.sampleRate = 48000;
    a.buffer.setSize (2, 48000);
    a.buffer.clear();

    for (auto t : { 0.1, 0.4, 0.42, 0.8 })
        a.buffer.setSample (0, static_cast<int> (t * 48000), 0.5f);

    // 0.42 is within the 50 ms gap after 0.4, so it isn't a new onset.
    auto onsets = detectOnsets (a);
    REQUIRE (onsets.size() == 3);
    CHECK (onsets[0] == Approx (0.1));
    CHECK (onsets[1] == Approx (0.4));
    CHECK (onsets[2] == Approx (0.8));
}

TEST_CASE ("True peak finds peaks between samples")
{
    // 12 kHz at 48 kHz with a 45 degree phase: every sample lands at 0.707 of the real peak.
    Audio a;
    a.sampleRate = 48000;
    a.buffer.setSize (2, 48000);

    for (int i = 0; i < a.buffer.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            a.buffer.setSample (ch, i, static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 12000.0 * i / 48000.0
                                                                     + juce::MathConstants<double>::pi / 4)));

    CHECK (peakDb (a) == Approx (-3.01).margin (0.05));
    CHECK (truePeakDb (a) == Approx (0.0).margin (0.3));
}

TEST_CASE ("Loudness range reflects quiet and loud passages")
{
    auto loud = juce::Decibels::decibelsToGain (-20.0f);
    auto quiet = juce::Decibels::decibelsToGain (-30.0f);
    auto a = sine (1000, 40, loud, loud);

    // Alternate 10 s loud, 10 s quiet.
    for (int i = 0; i < a.buffer.getNumSamples(); ++i)
        if ((i / 480000) % 2 == 1)
            for (int ch = 0; ch < 2; ++ch)
                a.buffer.setSample (ch, i, a.buffer.getSample (ch, i) * quiet / loud);

    auto stats = measureLoudness (a);
    CHECK (stats.range == Approx (10.0).margin (1.0));
    CHECK (stats.shortTermMax == Approx (-20.0).margin (0.3));
    CHECK (stats.momentaryMax == Approx (-20.0).margin (0.3));
}

TEST_CASE ("Spectral balance puts a tone in its band")
{
    auto bands = spectralBalance (sine (100, 2, 0.5f, 0.5f));
    REQUIRE (bands.size() == 10);
    CHECK (bands[1].fromHz == 60);
    CHECK (bands[1].db > -0.5f);

    for (size_t b = 3; b < bands.size(); ++b)
        CHECK (bands[b].db < -40.0f);
}

TEST_CASE ("Stereo correlation and width")
{
    auto mono = measureStereo (sine (200, 1, 0.5f, 0.5f));
    CHECK (mono.correlation == Approx (1.0f));
    CHECK (mono.sideToMidDb == silenceDb);

    auto inverted = measureStereo (sine (200, 1, 0.5f, -0.5f));
    CHECK (inverted.correlation == Approx (-1.0f));
    CHECK (inverted.lowCorrelation < -0.9f);

    Audio noise;
    noise.sampleRate = 48000;
    noise.buffer.setSize (2, 48000);
    juce::Random rng (42);

    for (int i = 0; i < noise.buffer.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            noise.buffer.setSample (ch, i, rng.nextFloat() - 0.5f);

    auto wide = measureStereo (noise);
    CHECK (std::abs (wide.correlation) < 0.05f);
    CHECK (wide.sideToMidDb == Approx (0.0f).margin (0.5f));
}
