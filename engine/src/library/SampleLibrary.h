#pragma once

#include "Classifier.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace gk::library
{

struct SampleEntry
{
    std::string path;
    int64_t size = 0;
    int64_t modified = 0;   // file time, as a count since the filesystem clock's epoch
    SampleFeatures features;
    Classification classification;
};

struct SampleQuery
{
    std::string text;                       // every word must match the name, folders or category
    std::optional<std::string> category;
    std::optional<bool> loop;
    std::optional<double> tempo;            // also matches half and double time
    double tempoTolerance = 2;
    std::optional<std::string> key;         // "Am", "A minor", ...
    std::optional<double> minDuration, maxDuration;
    int limit = 20;
};

/** An index of the user's sample folders with sound features, kept in a JSON file.

    Scans run on a background thread and only analyse new or changed files.
    All public methods are thread-safe.
*/
class SampleLibrary
{
public:
    explicit SampleLibrary (std::filesystem::path indexFile);
    ~SampleLibrary();

    static std::filesystem::path getDefaultIndexFile();

    std::vector<std::filesystem::path> getFolders() const;
    void addFolder (const std::filesystem::path&);
    void removeFolder (const std::filesystem::path&);

    struct Progress
    {
        bool running = false;
        int filesFound = 0, analysed = 0, unchanged = 0, failed = 0;
    };

    using ProgressCallback = std::function<void (const Progress&)>;

    /** Rescans every folder. Returns false if a scan is already running. The callback runs on the scan thread. */
    bool startScan (ProgressCallback = {});
    void waitForScan();
    Progress getProgress() const;

    size_t size() const;
    std::vector<SampleEntry> search (const SampleQuery&) const;
    std::optional<SampleEntry> find (const std::filesystem::path&) const;

    /** Analyses one file now, whether or not it's in a library folder. */
    static SampleEntry analyseFile (const std::filesystem::path&);

    static model::Json toJson (const SampleEntry&, bool includeFeatures);

private:
    const std::filesystem::path indexFile;

    mutable std::mutex mutex;
    std::vector<std::filesystem::path> folders;
    std::map<std::string, SampleEntry> entries;
    Progress progress;
    std::thread scanThread;
    std::atomic<bool> cancelScan { false };

    void scan (ProgressCallback);
    void load();
    void save() const; // with the mutex held
};

/** MIDI note names: 33 -> "A1". */
std::string noteName (int key);

} // namespace gk::library
