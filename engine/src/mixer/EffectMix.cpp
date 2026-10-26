#include "EffectMix.h"

namespace gk::mixer
{

const char* EffectMixTap::xmlTypeName = "groovekitEffectMixTap";

EffectMixTap::EffectMixTap (tracktion::PluginCreationInfo info)
    : Plugin (info)
{
}

EffectMixTap::~EffectMixTap()
{
    notifyListenersOfDeletion();
}

void EffectMixTap::initialise (const tracktion::PluginInitialisationInfo&)
{
    ring.clear();
    writePosition = 0;
    blockStart = 0;
}

void EffectMixTap::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& buffer = *fc.destBuffer;
    blockStart = writePosition;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* in = buffer.getReadPointer (std::min (ch, buffer.getNumChannels() - 1), fc.bufferStartSample);
        auto* out = ring.getWritePointer (ch);

        for (int i = 0, w = writePosition; i < fc.bufferNumSamples; ++i, w = (w + 1) & (ringSize - 1))
            out[w] = in[i];
    }

    writePosition = (writePosition + fc.bufferNumSamples) & (ringSize - 1);
}

//==============================================================================
const char* EffectMixBlend::xmlTypeName = "groovekitEffectMix";

EffectMixBlend::EffectMixBlend (tracktion::PluginCreationInfo info)
    : Plugin (info)
{
    mixValue.referTo (state, "mix", getUndoManager(), 1.0f);
    mixParam = addParam ("mix", "Mix", { 0.0f, 1.0f },
                         [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; },
                         [] (const juce::String& s) { return s.getFloatValue() / 100.0f; });
    mixParam->attachToCurrentValue (mixValue);
}

EffectMixBlend::~EffectMixBlend()
{
    notifyListenersOfDeletion();
    mixParam->detachFromCurrentValue();
}

void EffectMixBlend::setup (EffectMixTap& t, tracktion::Plugin& e)
{
    tap = &t;
    effect = &e;
}

void EffectMixBlend::setMix (float v)
{
    mixParam->setParameter (juce::jlimit (0.0f, 1.0f, v), juce::sendNotification);
}

void EffectMixBlend::initialise (const tracktion::PluginInitialisationInfo& info)
{
    mix.reset (info.sampleRate, 0.02);
    mix.setCurrentAndTargetValue (mixParam->getCurrentValue());
}

std::vector<float> EffectMixBlend::readScope (int numSamples) const
{
    numSamples = std::clamp (numSamples, 0, scopeSize);
    std::vector<float> out (static_cast<size_t> (numSamples));
    const auto end = scopePosition.load();

    for (int i = 0; i < numSamples; ++i)
        out[static_cast<size_t> (i)] = scope[static_cast<size_t> ((end - numSamples + i + scopeSize) % scopeSize)];

    return out;
}

void EffectMixBlend::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& buffer = *fc.destBuffer;
    const auto n = fc.bufferNumSamples;
    mix.setTargetValue (mixParam->getCurrentValue());

    // A bypassed effect passes its input through as it is: nothing to mix.
    const bool mixing = tap != nullptr && effect != nullptr && effect->isEnabled() && buffer.getNumChannels() >= 2
                        && (mix.isSmoothing() || mix.getTargetValue() < 0.9999f);

    if (mixing)
    {
        const auto latency = juce::jlimit (0, EffectMixTap::ringSize - n, juce::roundToInt (effect->getLatencySeconds() * sampleRate));
        const auto start = (tap->blockStart - latency) & (EffectMixTap::ringSize - 1);
        auto* left = buffer.getWritePointer (0, fc.bufferStartSample);
        auto* right = buffer.getWritePointer (1, fc.bufferStartSample);
        const auto* dryLeft = tap->ring.getReadPointer (0);
        const auto* dryRight = tap->ring.getReadPointer (1);

        for (int i = 0, r = start; i < n; ++i, r = (r + 1) & (EffectMixTap::ringSize - 1))
        {
            const auto wet = mix.getNextValue();
            left[i] = left[i] * wet + dryLeft[r] * (1.0f - wet);
            right[i] = right[i] * wet + dryRight[r] * (1.0f - wet);
        }
    }
    else
    {
        mix.skip (n);
    }

    // The scope: a mono sum of the output.
    const auto* l = buffer.getReadPointer (0, fc.bufferStartSample);
    const auto* r = buffer.getReadPointer (std::min (1, buffer.getNumChannels() - 1), fc.bufferStartSample);
    auto position = scopePosition.load();

    for (int i = 0; i < n; ++i, position = (position + 1) % scopeSize)
        scope[static_cast<size_t> (position)] = 0.5f * (l[i] + r[i]);

    scopePosition = position;
}

} // namespace gk::mixer
