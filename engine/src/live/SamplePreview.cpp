#include "SamplePreview.h"

namespace gk
{

struct SamplePreview::Voice
{
    std::unique_ptr<juce::AudioFormatReaderSource> reader;
    juce::ResamplingAudioSource resampler;
    float gain;
    int channels;

    Voice (std::unique_ptr<juce::AudioFormatReaderSource> r, double ratio, float g, int numChannels)
        : reader (std::move (r)), resampler (reader.get(), false, 2), gain (g), channels (numChannels)
    {
        resampler.setResamplingRatio (ratio);
    }

    bool finished() const
    {
        return reader->getNextReadPosition() >= reader->getTotalLength();
    }
};

SamplePreview::SamplePreview()
{
    formats.registerBasicFormats();
}

SamplePreview::~SamplePreview() = default;

void SamplePreview::play (const std::filesystem::path& file, float gainDb, double fromSeconds)
{
    auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (juce::File (file.string())));

    if (reader == nullptr)
        throw std::runtime_error ("Can't read \"" + file.string() + "\"");

    const auto ratio = reader->sampleRate / deviceRate;
    const auto channels = static_cast<int> (reader->numChannels);
    const auto from = static_cast<juce::int64> (std::max (0.0, fromSeconds) * reader->sampleRate);
    auto source = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    source->setNextReadPosition (from);
    auto next = std::make_unique<Voice> (std::move (source), ratio, juce::Decibels::decibelsToGain (gainDb), channels);
    next->resampler.prepareToPlay (blockSize, deviceRate);

    const juce::ScopedLock sl (lock);
    std::swap (voice, next);
}

void SamplePreview::stop()
{
    std::unique_ptr<Voice> old;
    const juce::ScopedLock sl (lock);
    std::swap (voice, old);
}

bool SamplePreview::isPlaying() const
{
    const juce::ScopedLock sl (lock);
    return voice != nullptr && ! voice->finished();
}

void SamplePreview::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                      int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const juce::ScopedTryLock sl (lock);

    if (! sl.isLocked() || voice == nullptr || voice->finished() || numOutputs == 0)
        return;

    // A mono sample plays on both of the first two outputs.
    const auto channels = std::min (numOutputs, 2);
    juce::AudioBuffer<float> buffer (outputs, channels, numSamples);
    voice->resampler.getNextAudioBlock (juce::AudioSourceChannelInfo (buffer));

    if (voice->channels == 1 && channels == 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    buffer.applyGain (voice->gain);
}

void SamplePreview::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock sl (lock);
    deviceRate = device->getCurrentSampleRate();
    blockSize = device->getCurrentBufferSizeSamples();
    voice.reset();
}

void SamplePreview::audioDeviceStopped()
{
    const juce::ScopedLock sl (lock);
    voice.reset();
}

} // namespace gk
