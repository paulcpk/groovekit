#include "TestHelpers.h"

#include "analysis/Analysis.h"
#include "mixer/LimiterPlugin.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gk;
using Catch::Approx;

namespace
{
    struct Harness
    {
        std::unique_ptr<tracktion::Edit> edit = tracktion::Edit::createSingleTrackEdit (test::getEngine());
        tracktion::Plugin::Ptr plugin = edit->getPluginCache().createNewPlugin (mixer::LimiterPlugin::xmlTypeName, {});
        mixer::LimiterPlugin& limiter = *dynamic_cast<mixer::LimiterPlugin*> (plugin.get());

        Harness (float inputGainDb, float ceilingDb)
        {
            limiter.inputGainParam->setParameter (inputGainDb, juce::sendNotification);
            limiter.ceilingParam->setParameter (ceilingDb, juce::sendNotification);
            limiter.baseClassInitialise ({ tracktion::TimePosition(), 48000.0, 512 });
        }

        ~Harness()
        {
            limiter.baseClassDeinitialise();
        }

        void process (juce::AudioBuffer<float>& buffer)
        {
            for (int start = 0; start < buffer.getNumSamples(); start += 512)
            {
                tracktion::PluginRenderContext context (&buffer, juce::AudioChannelSet::stereo(), start,
                                                        std::min (512, buffer.getNumSamples() - start), nullptr, 0.0,
                                                        {}, true, false, true, false);
                limiter.applyToBuffer (context);
            }
        }
    };

    constexpr int lookahead = 240 + 12; // 5 ms at 48 kHz plus the true-peak detector
}

TEST_CASE ("The limiter delays by its lookahead and loses nothing")
{
    Harness h (0, 0);
    juce::AudioBuffer<float> buffer (2, 2048);
    buffer.clear();
    buffer.setSample (0, 0, 0.5f);
    buffer.setSample (1, 0, -0.5f);
    buffer.setSample (0, 1000, 0.25f);

    h.process (buffer);

    CHECK (buffer.getSample (0, lookahead) == 0.5f);
    CHECK (buffer.getSample (1, lookahead) == -0.5f);
    CHECK (buffer.getSample (0, 1000 + lookahead) == 0.25f);
    CHECK (buffer.getMagnitude (0, 0, lookahead) == 0.0f);
}

TEST_CASE ("The limiter never exceeds its ceiling")
{
    Harness h (12, -1);
    h.limiter.releaseParam->setParameter (10.0f, juce::sendNotification);
    const auto ceiling = juce::Decibels::decibelsToGain (-1.0f);

    juce::AudioBuffer<float> buffer (2, 48000);
    juce::Random rng (7);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // Bursts of loud noise over a quiet sine.
        const auto burst = (i / 4800) % 2 == 0 ? (rng.nextFloat() * 2 - 1) : 0.0f;
        const auto s = 0.05f * std::sin (i * 0.05f) + burst;
        buffer.setSample (0, i, s);
        buffer.setSample (1, i, -s * 0.8f);
    }

    h.process (buffer);

    CHECK (buffer.getMagnitude (0, 0, buffer.getNumSamples()) <= ceiling + 1.0e-6f);
    CHECK (buffer.getMagnitude (1, 0, buffer.getNumSamples()) <= ceiling + 1.0e-6f);

    // Noise peaks between samples, so holding the true peak at the ceiling leaves sample peaks a bit lower.
    analysis::Audio out { buffer, 48000.0 };
    CHECK (analysis::truePeakDb (out) <= -1.0f + 0.1f);
    CHECK (analysis::truePeakDb (out) > -1.5f);

    // Between bursts the quiet sine comes back up (12 dB louder than it went in).
    CHECK (buffer.getMagnitude (0, 4800 * 2 - 600, 400) == Approx (0.05f * 3.98f).margin (0.02f));
}
