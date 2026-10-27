#pragma once

#include "Clap.h"

#include "library/SampleLibrary.h"

namespace gk::listening
{

/** What each library sample sounds like, as a CLAP embedding of its first 10 seconds, kept in
    a binary file next to the library index. Indexing runs in the background and only embeds
    new or changed files. Thread-safe. */
class SoundIndex
{
public:
    SoundIndex (library::SampleLibrary&, Clap&, std::filesystem::path file);
    ~SoundIndex();

    static std::filesystem::path getDefaultFile();

    struct Progress
    {
        bool running = false;
        int total = 0, embedded = 0, failed = 0;
    };

    using ProgressCallback = std::function<void (const Progress&)>;

    /** Embeds every library sample that isn't yet. Returns false if indexing is running. */
    bool start (ProgressCallback = {});
    void wait();

    /** Stops indexing (what's embedded so far is kept) and waits for it. */
    void stop();

    Progress getProgress() const;

    /** The embedding of a library sample (by its index key). */
    std::optional<Embedding> find (const std::string& path) const;
    size_t size() const;

    /** The embedding of any audio file: from the index when it's there, else computed now. */
    Embedding embedFile (const std::filesystem::path&);

    /** Calls f for every embedded sample, with the lock held. */
    void forEach (const std::function<void (const std::string& path, const Embedding&)>&) const;

private:
    struct Entry
    {
        int64_t size = 0, modified = 0;
        Embedding embedding;
    };

    library::SampleLibrary& library;
    Clap& clap;
    const std::filesystem::path file;

    mutable std::mutex mutex;
    std::map<std::string, Entry> entries;
    Progress progress;
    std::thread thread;
    std::atomic<bool> cancel { false };

    void run (ProgressCallback);
    void load();
    void save() const; // with the mutex held
};

/** The first 10 seconds of a file as CLAP hears them. Throws if it can't be read. */
Embedding embedAudioFile (Clap&, const std::filesystem::path&);

} // namespace gk::listening
