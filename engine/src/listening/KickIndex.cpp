#include "KickIndex.h"

#include "EngineContext.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <fstream>

namespace gk::listening
{

using model::Json;

namespace
{
    constexpr int fileVersion = 2;      // 2: kick hits found on a loop's grid
}

KickIndex::KickIndex (library::SampleLibrary& l, std::filesystem::path f) : library (l), file (std::move (f))
{
    load();
}

KickIndex::~KickIndex()
{
    stop();
}

std::filesystem::path KickIndex::getDefaultFile()
{
    return EngineContext::getAppDataFolder() / "kick-sounds.json";
}

void KickIndex::load()
{
    try
    {
        std::ifstream in (file);

        if (! in)
            return;

        const auto j = Json::parse (in);

        if (j.value ("version", 0) != fileVersion)
            return;

        const auto samples = j.value ("samples", Json::object());

        for (auto& [path, e] : samples.items())
        {
            Entry entry { e.value ("size", int64_t { 0 }), e.value ("modified", int64_t { 0 }), {} };

            if (e.contains ("sound"))
                entry.sound = KickSound::fromJson (e["sound"]);

            entries[path] = std::move (entry);
        }
    }
    catch (const std::exception&)
    {
        // Measured again.
    }
}

void KickIndex::save() const
{
    Json samples = Json::object();

    for (auto& [path, e] : entries)
    {
        Json j { { "size", e.size }, { "modified", e.modified } };

        if (e.sound)
            j["sound"] = e.sound->toJson();

        samples[path] = std::move (j);
    }

    try
    {
        std::filesystem::create_directories (file.parent_path());
        const auto temp = file.string() + ".tmp";
        {
            std::ofstream out (temp, std::ios::trunc);
            out << Json { { "version", fileVersion }, { "samples", samples } }.dump();
        }
        std::filesystem::rename (temp, file);
    }
    catch (const std::exception&)
    {
        // Kept here; written with the next batch.
    }
}

bool KickIndex::isKickSample (const library::SampleEntry& e)
{
    const auto& c = e.classification;
    const auto& bands = e.features.bands;

    if (c.category == "kick")
        return true;

    // Drum loops whose low end carries a good part of them.
    return c.loop && c.category == "drums" && bands.sub + bands.low > 0.25f;
}

std::optional<KickSound> KickIndex::measureSample (const library::SampleEntry& e)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (e.path)));

    if (reader == nullptr)
        return {};

    const auto loop = e.classification.loop;
    const auto count = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, static_cast<juce::int64> ((loop ? 30 : 4) * reader->sampleRate)));
    juce::AudioBuffer<float> audio (2, count);
    reader->read (&audio, 0, count, 0, true, true);

    KickTiming timing;

    if (loop)
    {
        const auto tempo = e.classification.tempo ? e.classification.tempo : e.features.estimatedTempo;

        if (tempo && *tempo > 0)
        {
            timing.beat = 60 / *tempo;
            timing.downbeat = 0.0;
        }
    }

    return measureKick (audio, reader->sampleRate, timing);
}

void KickIndex::start (std::function<void (const Progress&)> onProgress)
{
    {
        std::lock_guard lock (mutex);

        if (progress.running)
            return;

        progress = { true, 0, 0, 0 };
    }

    if (thread.joinable())
        thread.join();

    cancel = false;
    thread = std::thread ([this, onProgress]
    {
        while (library.getProgress().running && ! cancel)
            std::this_thread::sleep_for (std::chrono::milliseconds (200));

        std::vector<library::SampleEntry> todo;

        for (auto& sample : library.getSamples())
        {
            if (! isKickSample (sample))
                continue;

            std::lock_guard lock (mutex);
            auto it = entries.find (sample.path);

            if (it == entries.end() || it->second.size != sample.size || it->second.modified != sample.modified)
                todo.push_back (sample);
        }

        {
            std::lock_guard lock (mutex);
            progress.total = static_cast<int> (todo.size());
        }

        for (size_t i = 0; i < todo.size() && ! cancel; ++i)
        {
            std::optional<KickSound> sound;
            bool failed = false;

            try
            {
                sound = measureSample (todo[i]);
            }
            catch (const std::exception&)
            {
                failed = true;
            }

            Progress now;
            {
                std::lock_guard lock (mutex);
                entries[todo[i].path] = { todo[i].size, todo[i].modified, sound };
                ++progress.measured;
                progress.failed += failed ? 1 : 0;

                if (progress.measured % 200 == 0)
                    save();

                now = progress;
            }

            if (onProgress && (now.measured % 25 == 0 || now.measured == now.total))
                onProgress (now);
        }

        Progress now;
        {
            std::lock_guard lock (mutex);

            if (progress.measured > 0)
                save();

            progress.running = false;
            now = progress;
        }

        if (onProgress)
            onProgress (now);
    });
}

void KickIndex::wait()
{
    if (thread.joinable())
        thread.join();
}

void KickIndex::stop()
{
    cancel = true;
    wait();
}

KickIndex::Progress KickIndex::getProgress() const
{
    std::lock_guard lock (mutex);
    return progress;
}

std::optional<KickSound> KickIndex::find (const std::string& path) const
{
    std::lock_guard lock (mutex);
    auto it = entries.find (path);
    return it != entries.end() ? it->second.sound : std::nullopt;
}

size_t KickIndex::size() const
{
    std::lock_guard lock (mutex);
    return static_cast<size_t> (std::count_if (entries.begin(), entries.end(), [] (auto& e) { return e.second.sound.has_value(); }));
}

void KickIndex::forEach (const std::function<void (const std::string&, const KickSound&)>& f) const
{
    std::lock_guard lock (mutex);

    for (auto& [path, e] : entries)
        if (e.sound)
            f (path, *e.sound);
}

} // namespace gk::listening
