#include "Processing.h"

#include "Modulation.h"
#include "render/Stretch.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace gk::sampler
{

namespace
{
    /** 4-point Hermite interpolation of one channel, silent outside it. */
    float interpolate (const float* d, int n, double pos)
    {
        const auto i = static_cast<int> (std::floor (pos));
        const auto f = static_cast<float> (pos - i);
        auto at = [&] (int k) { return k >= 0 && k < n ? d[k] : 0.0f; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    void makeStereo (juce::AudioBuffer<float>& audio)
    {
        if (audio.getNumChannels() >= 2)
            return;

        juce::AudioBuffer<float> stereo (2, audio.getNumSamples());
        stereo.copyFrom (0, 0, audio, 0, 0, audio.getNumSamples());
        stereo.copyFrom (1, 0, audio, 0, 0, audio.getNumSamples());
        audio = std::move (stereo);
    }

    /** The last frame louder than gain, or -1. */
    int lastAbove (const juce::AudioBuffer<float>& audio, float gain)
    {
        for (int i = audio.getNumSamples() - 1; i >= 0; --i)
            for (int c = 0; c < audio.getNumChannels(); ++c)
                if (std::abs (audio.getSample (c, i)) > gain)
                    return i;

        return -1;
    }

    void filter (juce::AudioBuffer<float>& audio, const juce::IIRCoefficients& coefficients)
    {
        for (int c = 0; c < audio.getNumChannels(); ++c)
        {
            juce::IIRFilter f;
            f.setCoefficients (coefficients);
            f.processSamples (audio.getWritePointer (c), audio.getNumSamples());
        }
    }
}

bool Processing::isNeutral() const
{
    return ! trimDb && ! removeDc && stretchSemitones == 0 && stretchLength == 1 && pitchBend == 0 && ringMix == 0 && eq == 0
        && cutoff >= 20000 && resonance == 0 && boostDb == 0 && ! clip && punch == 0 && tail == 0 && reverb == 0
        && stereoDelayMs == 0 && ! fadeStereo;
}

std::string Processing::key() const
{
    std::string k;

    for (auto v : { trimDb.value_or (1.0f), removeDc ? 1.0f : 0.0f, stretchSemitones, stretchLength, pitchBend, ringMix, ringHz, eq, cutoff,
                    resonance, boostDb, clip ? 1.0f : 0.0f, punch, tail, reverb, largeReverb ? 1.0f : 0.0f, stereoDelayMs, fadeStereo ? 1.0f : 0.0f })
        k += std::to_string (v) + "|";

    return k;
}

std::shared_ptr<Sample> process (const Sample& source, const Processing& p)
{
    auto out = std::make_shared<Sample>();
    out->sampleRate = source.sampleRate;
    out->timeScale = source.timeScale;
    auto audio = source.audio;
    const auto sr = source.sampleRate;

    // The silent end, trimmed (with 10 ms to let the last sound go).
    if (p.trimDb)
    {
        const auto last = lastAbove (audio, juce::Decibels::decibelsToGain (*p.trimDb));

        if (last >= 0)
            audio.setSize (audio.getNumChannels(), std::min (audio.getNumSamples(), last + 1 + static_cast<int> (0.01 * sr)), true, false, true);
    }

    if (p.removeDc)
    {
        for (int c = 0; c < audio.getNumChannels(); ++c)
        {
            double sum = 0;
            auto* d = audio.getWritePointer (c);

            for (int i = 0; i < audio.getNumSamples(); ++i)
                sum += d[i];

            const auto mean = static_cast<float> (sum / std::max (1, audio.getNumSamples()));

            for (int i = 0; i < audio.getNumSamples(); ++i)
                d[i] -= mean;
        }
    }

    // Pitch and length apart, at full quality.
    if ((p.stretchSemitones != 0 || p.stretchLength != 1) && audio.getNumSamples() > 0)
    {
        audio = stretchAudio (audio, sr, 1.0 / p.stretchLength, p.stretchSemitones);
        out->timeScale *= p.stretchLength;
    }

    // The pitch bent down over the sample, played slower where it's lower.
    if (p.pitchBend != 0 && audio.getNumSamples() > 1)
    {
        const auto n = audio.getNumSamples();
        std::vector<std::vector<float>> bent (static_cast<size_t> (audio.getNumChannels()));
        double pos = 0;

        while (pos < n)
        {
            for (int c = 0; c < audio.getNumChannels(); ++c)
                bent[static_cast<size_t> (c)].push_back (interpolate (audio.getReadPointer (c), n, pos));

            const auto x = pos / n;
            const auto semitones = -12.0 * std::abs (p.pitchBend) * (p.pitchBend > 0 ? x : 1 - x);
            pos += std::exp2 (semitones / 12.0);
        }

        const auto length = static_cast<int> (bent[0].size());
        juce::AudioBuffer<float> result (audio.getNumChannels(), length);

        for (int c = 0; c < audio.getNumChannels(); ++c)
            result.copyFrom (c, 0, bent[static_cast<size_t> (c)].data(), length);

        out->timeScale *= static_cast<double> (length) / n;
        audio = std::move (result);
    }

    if (p.ringMix > 0)
    {
        const auto step = juce::MathConstants<double>::twoPi * p.ringHz / sr;

        for (int c = 0; c < audio.getNumChannels(); ++c)
        {
            auto* d = audio.getWritePointer (c);

            for (int i = 0; i < audio.getNumSamples(); ++i)
                d[i] *= 1.0f - p.ringMix + p.ringMix * static_cast<float> (std::sin (step * i));
        }
    }

    if (p.eq != 0)
        filter (audio, juce::IIRCoefficients::makeHighShelf (sr, 2500.0, 0.7, juce::Decibels::decibelsToGain (12.0 * p.eq)));

    if (p.cutoff < 20000 || p.resonance > 0)
        filter (audio, juce::IIRCoefficients::makeLowPass (sr, std::min (static_cast<double> (p.cutoff), sr * 0.45), 0.7071 * std::pow (14.0, p.resonance)));

    if (p.boostDb != 0 || p.clip)
    {
        audio.applyGain (juce::Decibels::decibelsToGain (p.boostDb));

        if (p.clip)
            for (int c = 0; c < audio.getNumChannels(); ++c)
                juce::FloatVectorOperations::clip (audio.getWritePointer (c), audio.getReadPointer (c), -1.0f, 1.0f, audio.getNumSamples());
    }

    if (p.punch != 0 || p.tail != 0)
        audio = shapeTransients (audio, sr, p.punch, p.tail);

    // Reverb, with room for its tail, which then ends where it fades out.
    if (p.reverb > 0)
    {
        makeStereo (audio);
        const auto frames = audio.getNumSamples();
        const auto tail = static_cast<int> ((p.largeReverb ? 3.5 : 1.2) * sr);
        audio.setSize (2, frames + tail, true, true, true);

        juce::Reverb reverb;
        juce::Reverb::Parameters params;
        params.roomSize = p.largeReverb ? 0.88f : 0.4f;
        params.damping = p.largeReverb ? 0.35f : 0.55f;
        params.wetLevel = 0.6f * p.reverb;
        params.dryLevel = 1.0f - 0.35f * p.reverb;
        params.width = 1.0f;
        reverb.setParameters (params);
        reverb.setSampleRate (sr);
        reverb.processStereo (audio.getWritePointer (0), audio.getWritePointer (1), audio.getNumSamples());

        const auto last = lastAbove (audio, juce::Decibels::decibelsToGain (-80.0f));
        audio.setSize (2, std::max (frames, last + 1), true, false, true);
    }

    if (p.stereoDelayMs != 0)
    {
        makeStereo (audio);
        const auto delay = static_cast<int> (std::abs (p.stereoDelayMs) / 1000.0 * sr);
        const auto channel = p.stereoDelayMs > 0 ? 0 : 1;
        auto* d = audio.getWritePointer (channel);

        for (int i = audio.getNumSamples() - 1; i >= 0; --i)
            d[i] = i >= delay ? d[i - delay] : 0.0f;
    }

    if (p.fadeStereo)
    {
        makeStereo (audio);
        const auto n = std::max (1, audio.getNumSamples() - 1);

        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            const auto angle = juce::MathConstants<float>::halfPi * static_cast<float> (i) / static_cast<float> (n);
            audio.setSample (0, i, audio.getSample (0, i) * std::cos (angle));
            audio.setSample (1, i, audio.getSample (1, i) * std::sin (angle));
        }
    }

    out->audio = std::move (audio);
    return out;
}

} // namespace gk::sampler
