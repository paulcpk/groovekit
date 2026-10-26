#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>

namespace gk::sampler
{

/** A decoded audio file, kept in memory. */
struct Sample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0;
};

/** Decodes audio files once and shares them between every sampler that uses them.
    Entries are reloaded when the file's modification time changes.
*/
class SampleCache
{
public:
    static SampleCache& getInstance();

    /** Throws std::runtime_error if the file can't be read. */
    std::shared_ptr<const Sample> load (const std::filesystem::path&);

    void clear();

private:
    struct Entry
    {
        std::filesystem::file_time_type modified;
        std::shared_ptr<const Sample> sample;
    };

    std::mutex mutex;
    std::map<std::filesystem::path, Entry> entries;
    juce::AudioFormatManager formats;

    SampleCache();
};

} // namespace gk::sampler
