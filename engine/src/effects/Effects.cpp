#include "Effects.h"

namespace gk::effects
{

//==============================================================================
// Delay: sync, timeMs, feedback, pingPong, lowCut, highCut, width
const char* DelayPlugin::xmlTypeName = "groovekitDelay";

const std::vector<const char*>& DelayPlugin::syncNames()
{
    static const std::vector<const char*> names { "off", "1/32", "1/16", "1/16 dotted", "1/8 triplet", "1/8", "1/8 dotted",
                                                  "1/4 triplet", "1/4", "1/4 dotted", "1/2", "1 bar" };
    return names;
}

double DelayPlugin::syncQuarters (int index)
{
    static const double quarters[] { 0, 0.125, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1, 1.5, 2, 4 };
    return quarters[juce::jlimit (0, 11, index)];
}

DelayPlugin::DelayPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::delay)
{
}

void DelayPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    bpm = edit.tempoSequence.getBpmAt ({});

    for (auto& line : lines)
        line.prepare (static_cast<int> (info.sampleRate * 4.1));

    for (auto& f : lowCuts)  f.state = 0;
    for (auto& f : highCuts) f.state = 0;

    delaySamples.reset (info.sampleRate, 0.08);
    delaySamples.setCurrentAndTargetValue (static_cast<float> (value (1) / 1000.0 * info.sampleRate));
}

void DelayPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.destBuffer->getNumChannels() < 2)
        return;

    // Synced to the tempo where the block starts.
    const auto quarters = syncQuarters (choice (0));
    const auto seconds = quarters > 0 ? quarters * 60.0 / bpm : value (1) / 1000.0;
    delaySamples.setTargetValue (static_cast<float> (std::min (4.0, seconds) * sampleRate));

    const auto feedback = value (2) / 100.0f;
    const bool pingPong = choice (3) == 1;
    const auto width = value (6) / 100.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        lowCuts[ch].setCutoff (value (4), sampleRate);
        highCuts[ch].setCutoff (value (5), sampleRate);
    }

    auto* left = fc.destBuffer->getWritePointer (0, fc.bufferStartSample);
    auto* right = fc.destBuffer->getWritePointer (1, fc.bufferStartSample);

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const auto d = delaySamples.getNextValue();
        auto outL = lines[0].read (d), outR = lines[1].read (d);

        // The repeats are filtered on their way round.
        const auto fbL = highCuts[0].lowpass (lowCuts[0].highpass (outL)) * feedback;
        const auto fbR = highCuts[1].lowpass (lowCuts[1].highpass (outR)) * feedback;

        if (pingPong)
        {
            // The input goes into the left line; each line feeds the other.
            lines[0].push (0.5f * (left[i] + right[i]) + fbR);
            lines[1].push (fbL);
        }
        else
        {
            lines[0].push (left[i] + fbL);
            lines[1].push (right[i] + fbR);
        }

        const auto mid = 0.5f * (outL + outR), side = 0.5f * (outL - outR) * width;
        left[i] = mid + side;
        right[i] = mid - side;
    }
}

//==============================================================================
// Reverb: preDelayMs, size, decay, damping, lowCut, width, diffusion
const char* ReverbPlugin::xmlTypeName = "groovekitReverb";

namespace
{
    // Mutually prime-ish lengths in ms at size 1, for an even, uncoloured tail.
    constexpr float baseLengthsMs[] { 29.7f, 37.1f, 41.1f, 43.7f, 47.9f, 53.3f, 59.9f, 67.1f };
    constexpr float diffuserMs[] { 4.7f, 3.6f, 12.7f, 9.3f };
}

ReverbPlugin::ReverbPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::reverb)
{
}

void ReverbPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    preDelay.prepare (static_cast<int> (info.sampleRate * 0.26));

    for (auto& d : diffusers)
        d.prepare (static_cast<int> (info.sampleRate * 0.03));

    for (auto& line : lines)
        line.prepare (static_cast<int> (info.sampleRate * 0.21));

    for (auto& d : damping)
        d.state = 0;

    lowCut.state = 0;
    lastSize = -1;
}

void ReverbPlugin::updateLines (float size, float decay, float dampingHz, float lowCutHz)
{
    if (size == lastSize && decay == lastDecay && dampingHz == lastDamping && lowCutHz == lastLowCut)
        return;

    lastSize = size;
    lastDecay = decay;
    lastDamping = dampingHz;
    lastLowCut = lowCutHz;

    const auto scale = 0.4f + 2.6f * size;   // 40% to 300% of the base lengths

    for (int l = 0; l < numLines; ++l)
    {
        lineLengths[l] = baseLengthsMs[l] * scale * 0.001f * static_cast<float> (sampleRate);
        // Each pass round a line loses its share of 60 dB over the decay time.
        gains[l] = std::pow (10.0f, -3.0f * lineLengths[l] / (decay * static_cast<float> (sampleRate)));
        damping[l].setCutoff (dampingHz, sampleRate);
    }

    lowCut.setCutoff (lowCutHz, sampleRate);
}

void ReverbPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.destBuffer->getNumChannels() < 2)
        return;

    updateLines (value (1) / 100.0f, value (2), value (3), value (4));
    const auto preDelaySamples = std::max (1.0f, value (0) * 0.001f * static_cast<float> (sampleRate));
    const auto width = value (5) / 100.0f;
    const auto diffusion = 0.75f * value (6) / 100.0f;
    float diffuserSamples[4];

    for (int d = 0; d < 4; ++d)
        diffuserSamples[d] = diffuserMs[d] * 0.001f * static_cast<float> (sampleRate);

    auto* left = fc.destBuffer->getWritePointer (0, fc.bufferStartSample);
    auto* right = fc.destBuffer->getWritePointer (1, fc.bufferStartSample);

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        preDelay.push (lowCut.highpass (0.5f * (left[i] + right[i])));
        auto x = preDelay.read (preDelaySamples);

        // Series allpasses smear the input into a dense cloud before the network.
        for (int d = 0; d < 4; ++d)
        {
            const auto delayed = diffusers[d].read (diffuserSamples[d]);
            const auto v = x + diffusion * delayed;
            diffusers[d].push (v);
            x = delayed - diffusion * v;
        }

        float out[numLines];

        for (int l = 0; l < numLines; ++l)
            out[l] = damping[l].lowpass (lines[l].read (lineLengths[l])) * gains[l];

        // An 8x8 Hadamard mix (fast Walsh-Hadamard transform), scaled to stay lossless.
        float h[numLines];
        std::copy (out, out + numLines, h);

        for (int len = 1; len < numLines; len *= 2)
            for (int s = 0; s < numLines; s += 2 * len)
                for (int k = s; k < s + len; ++k)
                {
                    const auto a = h[k], b = h[k + len];
                    h[k] = a + b;
                    h[k + len] = a - b;
                }

        constexpr float norm = 0.35355339f;   // 1 / sqrt(8)

        for (int l = 0; l < numLines; ++l)
            lines[l].push (h[l] * norm + (l % 2 == 0 ? x : -x) * 0.5f);

        const auto l = out[0] + out[2] + out[4] + out[6];
        const auto r = out[1] + out[3] + out[5] + out[7];
        const auto mid = 0.25f * (l + r), side = 0.25f * (l - r) * width;
        left[i] = mid + side;
        right[i] = mid - side;
    }
}

//==============================================================================
// Chorus: rate, depthMs, delayMs, feedback, width
const char* ChorusPlugin::xmlTypeName = "groovekitChorus";

ChorusPlugin::ChorusPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::chorus)
{
}

void ChorusPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    for (auto& line : lines)
        line.prepare (static_cast<int> (info.sampleRate * 0.05));

    phase = 0;
    feedbackState[0] = feedbackState[1] = 0;
}

void ChorusPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.destBuffer->getNumChannels() < 2)
        return;

    const auto rate = value (0);
    const auto depth = value (1) * 0.001f * static_cast<float> (sampleRate);
    const auto base = value (2) * 0.001f * static_cast<float> (sampleRate);
    const auto feedback = value (3) / 100.0f;
    const auto width = value (4) / 100.0f;
    const auto step = rate / sampleRate;
    constexpr int voices = 3;

    auto* left = fc.destBuffer->getWritePointer (0, fc.bufferStartSample);
    auto* right = fc.destBuffer->getWritePointer (1, fc.bufferStartSample);

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        float out[2] {};

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int v = 0; v < voices; ++v)
            {
                // Voices a third of a cycle apart; the right side a quarter cycle on.
                const auto p = phase + v / static_cast<double> (voices) + (ch == 1 ? 0.25 * width : 0.0);
                const auto lfo = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * p));
                out[ch] += lines[ch].read (base + depth * 0.5f * (1.0f + lfo));
            }

            out[ch] /= static_cast<float> (voices);
        }

        lines[0].push (left[i] + feedbackState[0] * feedback);
        lines[1].push (right[i] + feedbackState[1] * feedback);
        feedbackState[0] = out[0];
        feedbackState[1] = out[1];

        left[i] = out[0];
        right[i] = out[1];

        phase += step;

        if (phase >= 1.0)
            phase -= 1.0;
    }
}

//==============================================================================
// Distortion: type, driveDb, tone, outputDb
const char* DistortionPlugin::xmlTypeName = "groovekitDistortion";

const std::vector<const char*>& DistortionPlugin::typeNames()
{
    static const std::vector<const char*> names { "soft", "hard", "tube", "fold", "crush" };
    return names;
}

float DistortionPlugin::shape (int type, float x, float driveDb)
{
    return shape (type, x, juce::Decibels::decibelsToGain (driveDb), crushLevels (driveDb));
}

float DistortionPlugin::crushLevels (float driveDb)
{
    // Fewer steps the harder it's driven: 2^12 down to 2^2.
    return std::exp2 (juce::jmap (driveDb, 0.0f, 48.0f, 12.0f, 2.0f));
}

float DistortionPlugin::shape (int type, float x, float drive, float levels)
{
    const auto in = x * drive;

    switch (type)
    {
        case 1:     return juce::jlimit (-1.0f, 1.0f, in);
        case 2:     return std::tanh (in + 0.25f) - std::tanh (0.25f);   // asymmetric, for even harmonics
        case 3:     return std::sin (juce::MathConstants<float>::halfPi * in);                                   // folds back past full scale
        case 4:     return std::round (juce::jlimit (-1.0f, 1.0f, x) * levels) / levels;
        default:    return std::tanh (in);
    }
}

DistortionPlugin::DistortionPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::distortion)
{
}

void DistortionPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    maxBlock = info.blockSizeSamples;
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing (static_cast<size_t> (std::max (1, maxBlock)));

    for (auto& t : tone)
        t.state = 0;
}

double DistortionPlugin::getLatencySeconds()
{
    return oversampling != nullptr && sampleRate > 0 ? oversampling->getLatencyInSamples() / sampleRate : 0.0;
}

void DistortionPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || oversampling == nullptr || fc.destBuffer->getNumChannels() < 2)
        return;

    const auto type = choice (0);
    const auto driveDb = value (1);
    const auto drive = juce::Decibels::decibelsToGain (driveDb);
    const auto levels = crushLevels (driveDb);
    const auto output = juce::Decibels::decibelsToGain (value (3));

    for (auto& t : tone)
        t.setCutoff (value (2), sampleRate);

    // Blocks longer than prepared for go through in parts.
    for (int start = 0; start < fc.bufferNumSamples; start += maxBlock)
    {
        const auto n = std::min (maxBlock, fc.bufferNumSamples - start);
        float* channels[] { fc.destBuffer->getWritePointer (0, fc.bufferStartSample + start),
                            fc.destBuffer->getWritePointer (1, fc.bufferStartSample + start) };
        juce::dsp::AudioBlock<float> block (channels, 2, static_cast<size_t> (n));
        auto up = oversampling->processSamplesUp (block);

        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            auto* d = up.getChannelPointer (ch);

            for (size_t i = 0; i < up.getNumSamples(); ++i)
                d[i] = shape (type, d[i], drive, levels);
        }

        oversampling->processSamplesDown (block);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                channels[ch][i] = tone[ch].lowpass (channels[ch][i]) * output;
    }
}

} // namespace gk::effects
