#include "SampleCache.h"

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

void SampleCache::clear()
{
    std::lock_guard lock (mutex);
    entries.clear();
}

} // namespace gk::sampler
