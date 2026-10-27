#include "StereoPlugin.h"

namespace gk::effects
{

// Stereo: leftToLeft, rightToLeft, leftToRight, rightToRight, width, pan, delayMs, monoBelow, outputDb
const char* StereoPlugin::xmlTypeName = "groovekitStereo";

StereoPlugin::StereoPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::stereo)
{
}

void StereoPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    for (int i = 0; i < numSmoothed; ++i)
    {
        smoothed[i].reset (info.sampleRate, i == 6 ? 0.05 : 0.02);
        smoothed[i].setCurrentAndTargetValue (i == 6 ? value (6) * 0.001f * static_cast<float> (info.sampleRate) : value (static_cast<size_t> (i)) / 100.0f);
    }

    for (auto& line : lines)
        line.prepare (static_cast<int> (info.sampleRate * 0.025) + 4);

    bassSplit.prepare ({ info.sampleRate, static_cast<juce::uint32> (std::max (1, info.blockSizeSamples)), 2 });
    lastMonoHz = -1;
}

void StereoPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.destBuffer->getNumChannels() < 2)
        return;

    for (int i = 0; i < 6; ++i)
        smoothed[i].setTargetValue (value (static_cast<size_t> (i)) / 100.0f);

    smoothed[6].setTargetValue (value (6) * 0.001f * static_cast<float> (sampleRate));

    const auto monoHz = value (7);
    const bool monoBass = monoHz >= 20;

    if (monoBass && monoHz != lastMonoHz)
    {
        bassSplit.setCutoffFrequency (std::min (monoHz, static_cast<float> (sampleRate * 0.45)));
        lastMonoHz = monoHz;
    }

    const auto output = juce::Decibels::decibelsToGain (value (8));
    auto* left = fc.destBuffer->getWritePointer (0, fc.bufferStartSample);
    auto* right = fc.destBuffer->getWritePointer (1, fc.bufferStartSample);

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const auto ll = smoothed[0].getNextValue(), rl = smoothed[1].getNextValue();
        const auto lr = smoothed[2].getNextValue(), rr = smoothed[3].getNextValue();
        const auto width = smoothed[4].getNextValue(), pan = smoothed[5].getNextValue();
        const auto delay = smoothed[6].getNextValue();

        auto l = ll * left[i] + rl * right[i];
        auto r = lr * left[i] + rr * right[i];

        // One side later. Read 1 is the sample just pushed, so a delay of d reads d + 1.
        lines[0].push (l);
        lines[1].push (r);

        if (delay > 0)
            r = lines[1].read (delay + 1);
        else if (delay < 0)
            l = lines[0].read (1 - delay);

        const auto mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
        l = mid + side;
        r = mid - side;

        if (monoBass)
        {
            float lowL, highL, lowR, highR;
            bassSplit.processSample (0, l, lowL, highL);
            bassSplit.processSample (1, r, lowR, highR);
            const auto low = 0.5f * (lowL + lowR);
            l = highL + low;
            r = highR + low;
        }

        left[i] = l * (pan > 0 ? 1 - pan : 1) * output;
        right[i] = r * (pan < 0 ? 1 + pan : 1) * output;
        scope.push (left[i], right[i]);
    }
}

model::Json StereoPlugin::readMeter (const model::Json& params) const
{
    auto n = 512;

    if (auto it = params.find ("samples"); it != params.end() && it->is_number())
        n = it->get<int>();

    return scope.read (n);
}

} // namespace gk::effects
