#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <filesystem>

namespace gk
{

/** Plays a sample file straight to the audio device, next to (and independent of) the song,
    for auditioning in the browser. Register it with the audio device manager. */
class SamplePreview : public juce::AudioIODeviceCallback
{
public:
    SamplePreview();
    ~SamplePreview() override;

    /** Starts playing a file from the top, replacing whatever was playing. Throws if it can't be read. */
    void play (const std::filesystem::path&, float gainDb = -6.0f);
    void stop();
    bool isPlaying() const;

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    struct Voice;

    juce::AudioFormatManager formats;
    juce::CriticalSection lock;
    std::unique_ptr<Voice> voice;
    double deviceRate = 48000;
    int blockSize = 512;
};

} // namespace gk
