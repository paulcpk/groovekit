#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>

namespace gk::sampler
{

/** A decoded audio file, kept in memory. */
struct Sample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0;
    /** Seconds here per second of the file it came from (processing can stretch it). */
    double timeScale = 1;
};

struct Processing;

/** Decodes audio files once and shares them between every sampler that uses them.
    Entries are reloaded when the file's modification time changes.
*/
class SampleCache
{
public:
    static SampleCache& getInstance();

    /** Throws std::runtime_error if the file can't be read. */
    std::shared_ptr<const Sample> load (const std::filesystem::path&);

    /** The sample with processing worked in (see Processing), made once and shared while it's
        in use. */
    std::shared_ptr<const Sample> processed (const std::shared_ptr<const Sample>&, const Processing&);

    void clear();

private:
    struct Entry
    {
        std::filesystem::file_time_type modified;
        std::shared_ptr<const Sample> sample;
    };

    std::mutex mutex;
    std::map<std::filesystem::path, Entry> entries;
    struct Processed { std::weak_ptr<const Sample> source, processed; };
    std::map<std::pair<const Sample*, std::string>, Processed> processedEntries;
    juce::AudioFormatManager formats;

    SampleCache();
};

} // namespace gk::sampler
