#include "FaderPlugin.h"

namespace gk::mixer
{

const char* FaderPlugin::xmlTypeName = "groovekitFader";

FaderPlugin::FaderPlugin (tracktion::PluginCreationInfo info)
    : Plugin (info)
{
    auto um = getUndoManager();
    volumeValue.referTo (state, "volumeDb", um, 0.0f);
    panValue.referTo (state, "pan", um, 0.0f);

    volumeParam = addParam ("volume", "Volume", { minDb, maxDb },
                            [] (float v) { return juce::Decibels::toString (v, 1, minDb); },
                            [] (const juce::String& s) { return s.getFloatValue(); });
    panParam = addParam ("pan", "Pan", { -1.0f, 1.0f },
                         [] (float v) { return juce::String (v, 2); },
                         [] (const juce::String& s) { return s.getFloatValue(); });

    volumeParam->attachToCurrentValue (volumeValue);
    panParam->attachToCurrentValue (panValue);
}

FaderPlugin::~FaderPlugin()
{
    notifyListenersOfDeletion();
    volumeParam->detachFromCurrentValue();
    panParam->detachFromCurrentValue();
}

void FaderPlugin::setVolumeDb (float db)
{
    volumeParam->setParameter (juce::jlimit (minDb, maxDb, db), juce::sendNotification);
}

void FaderPlugin::setPan (float pan)
{
    panParam->setParameter (juce::jlimit (-1.0f, 1.0f, pan), juce::sendNotification);
}

std::pair<float, float> FaderPlugin::getTargetGains() const
{
    const auto db = volumeParam->getCurrentValue();
    const auto gain = db <= minDb ? 0.0f : juce::Decibels::decibelsToGain (db, minDb);
    const auto pan = panParam->getCurrentValue();

    return { gain * std::min (1.0f, 1.0f - pan), gain * std::min (1.0f, 1.0f + pan) };
}

void FaderPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    auto [l, r] = getTargetGains();
    leftGain.reset (info.sampleRate, 0.02);
    rightGain.reset (info.sampleRate, 0.02);
    leftGain.setCurrentAndTargetValue (l);
    rightGain.setCurrentAndTargetValue (r);
}

void FaderPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto [l, r] = getTargetGains();
    leftGain.setTargetValue (l);
    rightGain.setTargetValue (r);

    auto& buffer = *fc.destBuffer;

    if (buffer.getNumChannels() < 2)
        return;

    auto* left = buffer.getWritePointer (0, fc.bufferStartSample);
    auto* right = buffer.getWritePointer (1, fc.bufferStartSample);

    if (! leftGain.isSmoothing() && ! rightGain.isSmoothing())
    {
        juce::FloatVectorOperations::multiply (left, leftGain.getCurrentValue(), fc.bufferNumSamples);
        juce::FloatVectorOperations::multiply (right, rightGain.getCurrentValue(), fc.bufferNumSamples);
        return;
    }

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        left[i] *= leftGain.getNextValue();
        right[i] *= rightGain.getNextValue();
    }
}

} // namespace gk::mixer
