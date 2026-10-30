#include "LimiterPlugin.h"

#include "analysis/TruePeak.h"

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

    inputGainParam = addParam ("inputGain", "Input gain", { -36.0f, 36.0f },
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
    delayL.assign (static_cast<size_t> (lookahead + detectorDelay + 1), 0.0f);
    delayR.assign (static_cast<size_t> (lookahead + detectorDelay + 1), 0.0f);
    delayGain.assign (static_cast<size_t> (lookahead + 1), 1.0f);
    // The window never holds more entries than the gain window spans, plus the one being added.
    window.assign (static_cast<size_t> (lookahead + 3), {});
    lastReleaseMs = -1;
    reset();
}

void LimiterPlugin::reset()
{
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    std::fill (delayGain.begin(), delayGain.end(), 1.0f);
    windowHead = 0;
    windowCount = 0;
    writeIndex = 0;
    gainIndex = 0;
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
    const auto audioSize = static_cast<int> (delayL.size());   // lookahead + detector delay + 1
    const auto gainSize = lookahead + 1;
    constexpr auto taps = analysis::TruePeakInterpolator::taps;
    std::array<float, 2 * taps> windowL, windowR;

    // The moving average of the windowed minimum, over the last lookahead + 1 detections.
    double averageSum = 0;

    for (auto g : delayGain)
        averageSum += g;

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        delayL[static_cast<size_t> (writeIndex)] = left[i] * inputGain;
        delayR[static_cast<size_t> (writeIndex)] = right[i] * inputGain;

        // The detector looks at the sample taps ago, so the interpolator can see 12 samples each side
        // of it and catch peaks that fall between samples.
        for (int t = 0; t < 2 * taps; ++t)
        {
            const auto index = static_cast<size_t> ((writeIndex - (2 * taps - 1) + t + audioSize) % audioSize);
            windowL[static_cast<size_t> (t)] = delayL[index];
            windowR[static_cast<size_t> (t)] = delayR[index];
        }

        const auto level = std::max (analysis::TruePeakInterpolator::peakAround (windowL.data()),
                                     analysis::TruePeakInterpolator::peakAround (windowR.data()));
        const auto required = level > ceiling ? ceiling / level : 1.0f;

        // Sliding minimum of the required gain over the lookahead window.
        const auto ring = static_cast<int> (window.size());
        const auto slot = [&] (int i) -> std::pair<int64_t, float>& { return window[static_cast<size_t> ((windowHead + i) % ring)]; };

        while (windowCount > 0 && slot (windowCount - 1).second >= required)
            --windowCount;

        slot (windowCount) = { sampleCount, required };
        ++windowCount;

        while (slot (0).first <= sampleCount - gainSize)
        {
            windowHead = (windowHead + 1) % ring;
            --windowCount;
        }

        const auto windowMin = slot (0).second;

        // Averaging the windowed minimum ramps the gain down ahead of each peak and never above what it needs.
        averageSum += windowMin - delayGain[static_cast<size_t> (gainIndex)];
        delayGain[static_cast<size_t> (gainIndex)] = windowMin;
        gainIndex = (gainIndex + 1) % gainSize;
        const auto smoothed = static_cast<float> (averageSum / gainSize);

        envelope = smoothed < envelope ? smoothed
                                       : envelope + static_cast<float> ((smoothed - envelope) * releaseCoefficient);

        // Output the sample from lookahead + detector delay ago (the oldest in the line).
        const auto readIndex = (writeIndex + 1) % audioSize;
        left[i] = std::clamp (delayL[static_cast<size_t> (readIndex)] * envelope, -ceiling, ceiling);
        right[i] = std::clamp (delayR[static_cast<size_t> (readIndex)] * envelope, -ceiling, ceiling);

        writeIndex = readIndex;
        ++sampleCount;
    }
}

} // namespace gk::mixer
