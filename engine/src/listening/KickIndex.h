#pragma once

#include "KickSound.h"
#include "library/SampleLibrary.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace gk::listening
{

/** The library's kicks measured like the references' (KickSound): kick one-shots and loops, and
    drum loops with a kick in them, so a search can find the ones that sound like a reference's.
    Kept in a file and caught up in the background after scans; needs no models. Thread-safe. */
class KickIndex
{
public:
    KickIndex (library::SampleLibrary&, std::filesystem::path file);
    ~KickIndex();

    /** <app data>/kick-sounds.json */
    static std::filesystem::path getDefaultFile();

    struct Progress
    {
        bool running = false;
        int total = 0, measured = 0, failed = 0;
    };

    /** Measures the samples not measured yet (or changed), once any library scan is done. */
    void start (std::function<void (const Progress&)> = {});
    void wait();
    void stop();
    Progress getProgress() const;

    std::optional<KickSound> find (const std::string& path) const;
    size_t size() const;
    void forEach (const std::function<void (const std::string& path, const KickSound&)>&) const;

    /** Whether a sample is one to measure: kicks, and loops with a kick in them. */
    static bool isKickSample (const library::SampleEntry&);

    /** A sample's kick: a one-shot as one hit, a loop on its tempo's grid (it starts on the one). */
    static std::optional<KickSound> measureSample (const library::SampleEntry&);

private:
    library::SampleLibrary& library;
    const std::filesystem::path file;

    struct Entry
    {
        int64_t size = 0, modified = 0;
        std::optional<KickSound> sound;     // none: measured, no kick in it
    };

    mutable std::mutex mutex;
    std::map<std::string, Entry> entries;
    Progress progress;
    std::thread thread;
    std::atomic<bool> cancel { false };

    void load();

    /** Writes the file: takes the mutex only to snapshot the entries, so it mustn't be held
        (only the measuring thread saves, so two saves never overlap). */
    void save() const;
};

} // namespace gk::listening
