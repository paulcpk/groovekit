#include "SoundIndex.h"

#include "EngineContext.h"
#include "analysis/Analysis.h"

#include <fstream>

namespace gk::listening
{

namespace
{
    constexpr char magic[4] = { 'G', 'K', 'S', 'I' };
    constexpr uint32_t version = 1;
}

Embedding embedAudioFile (Clap& clap, const std::filesystem::path& path)
{
    auto audio = analysis::loadAudio (path, Clap::windowSeconds);
    auto mono = resampleMono (audio.buffer, audio.sampleRate, Clap::sampleRate);

    if (mono.empty())
        throw std::runtime_error ("No audio in " + path.string());

    return clap.embedAudio (mono.data(), mono.size());
}

SoundIndex::SoundIndex (library::SampleLibrary& l, Clap& c, std::filesystem::path f)
    : library (l), clap (c), file (std::move (f))
{
    load();
}

SoundIndex::~SoundIndex()
{
    stop();
}

void SoundIndex::stop()
{
    cancel = true;
    wait();
}

std::filesystem::path SoundIndex::getDefaultFile()
{
    if (auto env = std::getenv ("GROOVEKIT_SOUND_INDEX"))
        return env;

    return EngineContext::getAppDataFolder() / "sounds.bin";
}

bool SoundIndex::start (ProgressCallback callback)
{
    std::lock_guard lock (mutex);

    if (progress.running)
        return false;

    if (thread.joinable())
        thread.join();

    progress = { true, 0, 0, 0 };
    cancel = false;
    thread = std::thread ([this, callback] { run (callback); });
    return true;
}

void SoundIndex::wait()
{
    if (thread.joinable())
        thread.join();
}

SoundIndex::Progress SoundIndex::getProgress() const
{
    std::lock_guard lock (mutex);
    return progress;
}

void SoundIndex::run (ProgressCallback callback)
{
    auto report = [&] (bool force)
    {
        Progress p;
        {
            std::lock_guard lock (mutex);
            p = progress;
        }

        if (callback && (force || (p.embedded + p.failed) % 25 == 0))
            callback (p);
    };

    const auto samples = library.getSamples();
    std::vector<const library::SampleEntry*> todo;

    {
        std::lock_guard lock (mutex);
        std::set<std::string> present;

        for (auto& s : samples)
        {
            present.insert (s.path);
            auto it = entries.find (s.path);

            if (it == entries.end() || it->second.size != s.size || it->second.modified != s.modified)
                todo.push_back (&s);
        }

        // Forget samples that left the library.
        for (auto it = entries.begin(); it != entries.end();)
            it = present.contains (it->first) ? std::next (it) : entries.erase (it);

        progress.total = static_cast<int> (todo.size());
    }

    report (true);

    // Two at a time, so reading one file overlaps hearing another; the model itself spreads
    // each one over several cores.
    std::atomic<size_t> next { 0 };
    int sinceSave = 0;

    auto work = [&]
    {
        for (size_t i; ! cancel && (i = next++) < todo.size();)
        {
            auto* s = todo[i];

            try
            {
                auto embedding = embedAudioFile (clap, s->path);
                std::lock_guard lock (mutex);
                entries[s->path] = { s->size, s->modified, embedding };
                ++progress.embedded;

                if (++sinceSave >= 500)
                {
                    sinceSave = 0;
                    save();
                }
            }
            catch (const std::exception&)
            {
                std::lock_guard lock (mutex);
                ++progress.failed;
            }

            report (false);
        }
    };

    std::thread second (work);
    work();
    second.join();

    {
        std::lock_guard lock (mutex);
        progress.running = false;
        save();
    }

    report (true);
}

std::optional<Embedding> SoundIndex::find (const std::string& path) const
{
    std::lock_guard lock (mutex);

    if (auto it = entries.find (path); it != entries.end())
        return it->second.embedding;

    return {};
}

size_t SoundIndex::size() const
{
    std::lock_guard lock (mutex);
    return entries.size();
}

Embedding SoundIndex::embedFile (const std::filesystem::path& path)
{
    if (auto entry = library.find (path))
        if (auto embedding = find (entry->path))
            return *embedding;

    return embedAudioFile (clap, path);
}

void SoundIndex::forEach (const std::function<void (const std::string&, const Embedding&)>& f) const
{
    std::lock_guard lock (mutex);

    for (auto& [path, e] : entries)
        f (path, e.embedding);
}

//==============================================================================
void SoundIndex::load()
{
    std::ifstream in (file, std::ios::binary);
    char header[4];
    uint32_t fileVersion = 0, count = 0;

    if (! in.read (header, 4) || std::memcmp (header, magic, 4) != 0
        || ! in.read (reinterpret_cast<char*> (&fileVersion), 4) || fileVersion != version
        || ! in.read (reinterpret_cast<char*> (&count), 4))
        return; // missing or from another version: indexed again

    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t length = 0;
        Entry e;

        if (! in.read (reinterpret_cast<char*> (&length), 4) || length > 4096)
            break;

        std::string path (length, '\0');

        if (! in.read (path.data(), length)
            || ! in.read (reinterpret_cast<char*> (&e.size), 8)
            || ! in.read (reinterpret_cast<char*> (&e.modified), 8)
            || ! in.read (reinterpret_cast<char*> (e.embedding.data()), sizeof (float) * e.embedding.size()))
            break;

        entries.emplace (std::move (path), e);
    }
}

void SoundIndex::save() const
{
    std::filesystem::create_directories (file.parent_path());
    auto temp = file;
    temp += ".tmp";

    {
        std::ofstream out (temp, std::ios::binary | std::ios::trunc);
        const auto count = static_cast<uint32_t> (entries.size());
        out.write (magic, 4);
        out.write (reinterpret_cast<const char*> (&version), 4);
        out.write (reinterpret_cast<const char*> (&count), 4);

        for (auto& [path, e] : entries)
        {
            const auto length = static_cast<uint32_t> (path.size());
            out.write (reinterpret_cast<const char*> (&length), 4);
            out.write (path.data(), length);
            out.write (reinterpret_cast<const char*> (&e.size), 8);
            out.write (reinterpret_cast<const char*> (&e.modified), 8);
            out.write (reinterpret_cast<const char*> (e.embedding.data()), sizeof (float) * e.embedding.size());
        }
    }

    std::filesystem::rename (temp, file);
}

} // namespace gk::listening
