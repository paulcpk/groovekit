#include "LimiterPlugin.h"

namespace gk::mixer
{

const char* LimiterPlugin::xmlTypeName = "groovekitLimiter";

LimiterPlugin::LimiterPlugin (tracktion::PluginCreationInfo info)
    : Plugin (info)
{
    auto um = getUndoManager();
    inputGainValue.referTo (state, "inputGain", um, 0.0f);
    ceilingValue.referTo (state, "ceiling", um, -1.0f);
    releaseValue.referTo (state, "release", um, 100.0f);

    inputGainParam = addParam ("inputGain", "Input gain", { -24.0f, 24.0f },
                               [] (float v) { return juce::Decibels::toString (v, 1); },
                               [] (const juce::String& s) { return s.getFloatValue(); });
    ceilingParam = addParam ("ceiling", "Ceiling", { -12.0f, 0.0f },
                             [] (float v) { return juce::Decibels::toString (v, 1); },
                             [] (const juce::String& s) { return s.getFloatValue(); });
    releaseParam = addParam ("release", "Release", { 1.0f, 1000.0f },
                             [] (float v) { return juce::String (v, 0) + " ms"; },
                             [] (const juce::String& s) { return s.getFloatValue(); });

    inputGainParam->attachToCurrentValue (inputGainValue);
    ceilingParam->attachToCurrentValue (ceilingValue);
    releaseParam->attachToCurrentValue (releaseValue);
}

LimiterPlugin::~LimiterPlugin()
{
    notifyListenersOfDeletion();
    inputGainParam->detachFromCurrentValue();
    ceilingParam->detachFromCurrentValue();
    releaseParam->detachFromCurrentValue();
}

void LimiterPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    lookahead = std::max (1, static_cast<int> (std::lround (lookaheadSeconds * info.sampleRate)));
    delayL.assign (static_cast<size_t> (lookahead + 1), 0.0f);
    delayR.assign (static_cast<size_t> (lookahead + 1), 0.0f);
    delayGain.assign (static_cast<size_t> (lookahead + 1), 1.0f);
    lastReleaseMs = -1;
    reset();
}

void LimiterPlugin::reset()
{
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    std::fill (delayGain.begin(), delayGain.end(), 1.0f);
    window.clear();
    writeIndex = 0;
    sampleCount = 0;
    envelope = 1.0f;
}

void LimiterPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.destBuffer->getNumChannels() < 2 || delayL.empty())
        return;

    const auto inputGain = juce::Decibels::decibelsToGain (inputGainParam->getCurrentValue());
    const auto ceiling = juce::Decibels::decibelsToGain (ceilingParam->getCurrentValue());

    if (const auto releaseMs = releaseParam->getCurrentValue(); releaseMs != lastReleaseMs)
    {
        releaseCoefficient = 1.0 - std::exp (-1.0 / (releaseMs * 0.001 * sampleRate));
        lastReleaseMs = releaseMs;
    }

    auto* left = fc.destBuffer->getWritePointer (0, fc.bufferStartSample);
    auto* right = fc.destBuffer->getWritePointer (1, fc.bufferStartSample);
    const auto size = lookahead + 1;

    // The moving average of the windowed minimum, over the last lookahead + 1 samples.
    double averageSum = 0;

    for (auto g : delayGain)
        averageSum += g;

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const auto l = left[i] * inputGain;
        const auto r = right[i] * inputGain;
        const auto level = std::max (std::abs (l), std::abs (r));
        const auto required = level > ceiling ? ceiling / level : 1.0f;

        // Sliding minimum of the required gain over the lookahead window.
        while (! window.empty() && window.back().second >= required)
            window.pop_back();

        window.emplace_back (sampleCount, required);

        while (window.front().first <= sampleCount - size)
            window.pop_front();

        const auto windowMin = window.front().second;

        // Averaging the windowed minimum ramps the gain down ahead of each peak and never above what it needs.
        averageSum += windowMin - delayGain[static_cast<size_t> (writeIndex)];
        delayGain[static_cast<size_t> (writeIndex)] = windowMin;
        const auto smoothed = static_cast<float> (averageSum / size);

        envelope = smoothed < envelope ? smoothed
                                       : envelope + static_cast<float> ((smoothed - envelope) * releaseCoefficient);

        // Output the sample from one lookahead ago.
        const auto readIndex = (writeIndex + 1) % size;
        const auto outL = delayL[static_cast<size_t> (readIndex)] * envelope;
        const auto outR = delayR[static_cast<size_t> (readIndex)] * envelope;

        delayL[static_cast<size_t> (writeIndex)] = l;
        delayR[static_cast<size_t> (writeIndex)] = r;
        writeIndex = readIndex;
        ++sampleCount;

        left[i] = std::clamp (outL, -ceiling, ceiling);
        right[i] = std::clamp (outR, -ceiling, ceiling);
    }
}

} // namespace gk::mixer
