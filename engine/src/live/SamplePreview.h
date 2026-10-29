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

    /** Starts playing a file from the top (or from a time in it, seconds), replacing whatever was
        playing. With under, a second file plays in sync beneath it at underLevel (0 to 1, changed
        while playing with setUnderLevel): a sound alone over the rest of a song, the song faded in
        and out like a DJ mixer's cue mix. Throws if a file can't be read. */
    void play (const std::filesystem::path&, float gainDb = -6.0f, double fromSeconds = 0,
               const std::optional<std::filesystem::path>& under = {}, float underLevel = 1.0f);
    void setUnderLevel (float level);
    void stop();
    bool isPlaying() const;

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    struct Track;
    struct Voice;

    juce::AudioFormatManager formats;
    std::atomic<float> underLevel { 1.0f };
    juce::CriticalSection lock;
    std::unique_ptr<Voice> voice;
    double deviceRate = 48000;
    int blockSize = 512;
};

} // namespace gk
