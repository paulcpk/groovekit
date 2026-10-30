#include "SamplePreview.h"

namespace gk
{

/** One file, resampled to the device's rate: short ones decoded into memory when the preview
    starts, long ones read ahead on a background thread, so the device callback never touches the
    disk (a cold read from an external drive would stall every sound the engine plays). */
struct SamplePreview::Track
{
    static constexpr double decodeUpToSeconds = 30;

    std::unique_ptr<juce::PositionableAudioSource> source;   // in memory, or buffered from the file
    juce::ResamplingAudioSource resampler;
    int channels;

    Track (std::unique_ptr<juce::PositionableAudioSource> s, double ratio, int numChannels)
        : source (std::move (s)), resampler (source.get(), false, 2), channels (numChannels)
    {
        resampler.setResamplingRatio (ratio);
    }

    bool finished() const
    {
        return source->getNextReadPosition() >= source->getTotalLength();
    }

    /** Fills the first two channels of buffer (a mono file plays on both). */
    void read (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        juce::AudioSourceChannelInfo info (&buffer, 0, numSamples);
        resampler.getNextAudioBlock (info);

        if (channels == 1 && buffer.getNumChannels() == 2)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    }
};

struct SamplePreview::Voice
{
    std::unique_ptr<Track> main, under;
    float gain;
    juce::SmoothedValue<float> underGain;
    juce::AudioBuffer<float> scratch { 2, 4096 };

    bool finished() const
    {
        return main->finished() && (under == nullptr || under->finished());
    }
};

SamplePreview::SamplePreview()
{
    formats.registerBasicFormats();
    readAhead.startThread (juce::Thread::Priority::normal);
}

SamplePreview::~SamplePreview() = default;

void SamplePreview::play (const std::filesystem::path& file, float gainDb, double fromSeconds,
                          const std::optional<std::filesystem::path>& underFile, float level)
{
    auto open = [&] (const std::filesystem::path& f)
    {
        auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (juce::File (f.string())));

        if (reader == nullptr)
            throw std::runtime_error ("Can't read \"" + f.string() + "\"");

        const auto ratio = reader->sampleRate / deviceRate;
        const auto rate = reader->sampleRate;
        const auto channels = static_cast<int> (reader->numChannels);
        const auto length = reader->lengthInSamples;
        const auto from = std::min (length, static_cast<juce::int64> (std::max (0.0, fromSeconds) * rate));
        std::unique_ptr<juce::PositionableAudioSource> source;

        if (length <= static_cast<juce::int64> (Track::decodeUpToSeconds * rate))
        {
            auto audio = std::make_unique<juce::AudioBuffer<float>> (std::max (1, channels), static_cast<int> (std::max<juce::int64> (1, length)));
            reader->read (audio.get(), 0, static_cast<int> (length), 0, true, channels > 1);
            auto memory = std::make_unique<juce::MemoryAudioSource> (*audio, false, false);
            // The source keeps the buffer alive by reading from it; it goes with the track.
            struct Owning : juce::PositionableAudioSource
            {
                std::unique_ptr<juce::AudioBuffer<float>> buffer;
                std::unique_ptr<juce::MemoryAudioSource> inner;
                void prepareToPlay (int n, double sr) override                  { inner->prepareToPlay (n, sr); }
                void releaseResources() override                                 { inner->releaseResources(); }
                void getNextAudioBlock (const juce::AudioSourceChannelInfo& i) override { inner->getNextAudioBlock (i); }
                void setNextReadPosition (juce::int64 p) override                { inner->setNextReadPosition (p); }
                juce::int64 getNextReadPosition() const override                 { return inner->getNextReadPosition(); }
                juce::int64 getTotalLength() const override                      { return inner->getTotalLength(); }
                bool isLooping() const override                                  { return false; }
            };
            auto owning = std::make_unique<Owning>();
            owning->buffer = std::move (audio);
            owning->inner = std::move (memory);
            source = std::move (owning);
        }
        else
        {
            auto file = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
            // Four seconds ahead; the thread fills the first stretch before this returns.
            auto buffered = std::make_unique<juce::BufferingAudioSource> (file.release(), readAhead, true, static_cast<int> (rate * 4), channels);
            source = std::move (buffered);
        }

        source->setNextReadPosition (from);
        auto track = std::make_unique<Track> (std::move (source), ratio, channels);
        track->resampler.prepareToPlay (blockSize, deviceRate);
        return track;
    };

    auto next = std::make_unique<Voice>();
    next->main = open (file);

    if (underFile)
        next->under = open (*underFile);

    next->gain = juce::Decibels::decibelsToGain (gainDb);
    underLevel = std::clamp (level, 0.0f, 1.0f);
    next->underGain.reset (deviceRate, 0.05);
    next->underGain.setCurrentAndTargetValue (underLevel);

    const juce::ScopedLock sl (lock);
    std::swap (voice, next);
}

void SamplePreview::setUnderLevel (float level)
{
    underLevel = std::clamp (level, 0.0f, 1.0f);
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

    const auto channels = std::min (numOutputs, 2);
    juce::AudioBuffer<float> buffer (outputs, channels, numSamples);
    voice->main->read (buffer, numSamples);

    if (voice->under != nullptr)
    {
        voice->underGain.setTargetValue (underLevel);

        // In pieces the size of the scratch buffer, mixed in at the (smoothed) level.
        for (int done = 0; done < numSamples;)
        {
            const auto n = std::min (numSamples - done, voice->scratch.getNumSamples());
            juce::AudioBuffer<float> part (voice->scratch.getArrayOfWritePointers(), channels, n);
            voice->under->read (part, n);

            for (int i = 0; i < n; ++i)
            {
                const auto g = voice->underGain.getNextValue();

                for (int ch = 0; ch < channels; ++ch)
                    buffer.addSample (ch, done + i, part.getSample (ch, i) * g);
            }

            done += n;
        }
    }

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
