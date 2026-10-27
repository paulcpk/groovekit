#include "SampleCache.h"
#include "Modulation.h"

namespace gk::sampler
{

SampleCache& SampleCache::getInstance()
{
    static SampleCache cache;
    return cache;
}

SampleCache::SampleCache()
{
    formats.registerBasicFormats();
}

std::shared_ptr<const Sample> SampleCache::load (const std::filesystem::path& path)
{
    std::error_code ec;
    auto modified = std::filesystem::last_write_time (path, ec);

    if (ec)
        throw std::runtime_error ("Sample not found: " + path.string());

    std::lock_guard lock (mutex);

    if (auto it = entries.find (path); it != entries.end() && it->second.modified == modified)
        return it->second.sample;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (path.string())));

    if (reader == nullptr)
        throw std::runtime_error ("Unsupported or unreadable audio file: " + path.string());

    if (reader->lengthInSamples > std::numeric_limits<int>::max())
        throw std::runtime_error ("Audio file too long: " + path.string());

    auto sample = std::make_shared<Sample>();
    sample->sampleRate = reader->sampleRate;
    sample->audio.setSize (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
    reader->read (&sample->audio, 0, static_cast<int> (reader->lengthInSamples), 0, true, true);

    entries[path] = { modified, sample };
    return sample;
}

std::shared_ptr<const Sample> SampleCache::shaped (const std::shared_ptr<const Sample>& sample, float punch, float tail)
{
    if (punch == 0.0f && tail == 0.0f)
        return sample;

    const auto key = std::make_tuple (sample.get(), punch, tail);

    {
        std::lock_guard lock (mutex);

        if (auto it = shapedEntries.find (key); it != shapedEntries.end() && it->second.source.lock() == sample)
            if (auto found = it->second.shaped.lock())
                return found;
    }

    auto made = std::make_shared<Sample>();
    made->sampleRate = sample->sampleRate;
    made->audio = shapeTransients (sample->audio, sample->sampleRate, punch, tail);

    std::lock_guard lock (mutex);
    std::erase_if (shapedEntries, [] (const auto& entry) { return entry.second.shaped.expired(); });
    shapedEntries[key] = { sample, made };
    return made;
}

void SampleCache::clear()
{
    std::lock_guard lock (mutex);
    entries.clear();
    shapedEntries.clear();
}

} // namespace gk::sampler
