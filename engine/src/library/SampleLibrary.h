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

    /** Worked out from the above once, by the library, so a search doesn't redo it for every
        entry (not in the index file): the file name's words, and the key the name gives. */
    std::vector<std::string> nameWords;
    std::optional<music::Key> namedKey;
};

struct SampleQuery
{
    std::string text;                       // every word must match the name, folders or category
    std::optional<std::string> category;
    std::optional<bool> loop;
    std::optional<double> tempo;            // also matches half and double time
    double tempoTolerance = 2;
    std::optional<std::string> key;         // "Am", "A minor", ...: in that key (or its relative)
    std::optional<music::Key> fitsKey;      // goes with that key: see SampleLibrary::fitTo
    std::optional<double> minDuration, maxDuration;
    int limit = 20;
};

/** An index of the user's sample folders with sound features, kept in a JSON file.

    Scans run on background threads and only analyse new or changed files.
    All public methods are thread-safe.
*/
class SampleLibrary
{
public:
    explicit SampleLibrary (std::filesystem::path indexFile);
    ~SampleLibrary();

    static std::filesystem::path getDefaultIndexFile();

    /** ~/Music/Samples, which GrooveKit uses until the user picks folders. */
    static std::filesystem::path getDefaultFolder();

    /** Adds (and creates) the default folder when no folders are set. Returns true if it did. */
    bool addDefaultFolderIfNone();

    std::vector<std::filesystem::path> getFolders() const;

    /** Whether the engine looks for new and changed files when it starts (on by default). A
        scan of a big library takes a while and memory; with this off, scans are started by hand. */
    bool getAutoScan() const;
    void setAutoScan (bool);
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

    /** Every indexed sample, sorted by path. */
    std::vector<SampleEntry> getSamples() const;

    /** Calls f for every indexed sample, sorted by path, with the lock held (so f must be quick
        and not call the library): a walk without copying every entry. */
    void forEach (const std::function<void (const SampleEntry&)>& f) const;

    struct Listing
    {
        struct Folder
        {
            std::filesystem::path path;
            size_t samples = 0;     // anywhere below it
        };

        std::vector<Folder> folders;
        std::vector<SampleEntry> samples;
    };

    /** The indexed subfolders and samples directly in a folder, in the order the Finder shows
        them. With an empty path, the library's folders. */
    Listing list (const std::filesystem::path& folder) const;

    /** The entry of a file, given any spelling of its path (made canonical, which takes a few
        system calls). */
    std::optional<SampleEntry> find (const std::filesystem::path&) const;

    /** The entry whose index key this is (the path as an entry or a search result gives it):
        a map lookup, for callers that already hold library keys. */
    std::optional<SampleEntry> findByKey (const std::string& key) const;

    /** Analyses one file now, whether or not it's in a library folder. */
    static SampleEntry analyseFile (const std::filesystem::path&);

    /** A result for clients; with a song key, how the sample goes with it ("keyFit"). */
    static model::Json toJson (const SampleEntry&, bool includeFeatures, std::optional<music::Key> songKey = {});

    /** A sample's key: what its name says, else what it sounds like (names are usually right;
        hearing a loop's key goes wrong more often, between a key and its fifth or major and
        minor). One-shots with a clear pitch have a root note too. */
    struct SampleKey
    {
        std::optional<music::Key> key;
        std::string source;                 // "name", "sound", or "name and sound"
        std::optional<music::Key> heard;    // what it sounds like, when that's not the name's key
        std::optional<int> root;            // MIDI note
    };

    static SampleKey keyOf (const SampleEntry&);

    /** How a sample goes with a song's key, if it has a key or a root note to go by: whether it
        fits as it is (the same notes, a fifth away, or a root in the scale), and the semitones
        that put it exactly in the key (by its relative major for keys, to the tonic for roots). */
    struct KeyFit
    {
        bool fits = false;
        int transpose = 0;
    };

    static std::optional<KeyFit> fitTo (const SampleEntry&, music::Key);

    /** A loop's tempo made exact by its length: the whole bars of 4/4 (or beats, for loops under a
        bar) it lasts at its named or heard tempo, when that's within 3 % of it; otherwise the
        tempo as it is (a loop with a tail). Used as a layer's sourceTempo, it fits the loop to the
        song without drifting. Empty for a sample with no tempo. */
    static std::optional<double> loopTempo (const SampleEntry&);

private:
    const std::filesystem::path indexFile;

    /** An entry with its line of the index file, serialised when it was analysed (or on the
        first save after loading), so a save copies strings under the lock instead of building
        JSON for every entry there. */
    struct Stored
    {
        SampleEntry entry;
        mutable std::string json;   // a cache: save() fills it for loaded entries
    };

    mutable std::mutex mutex;
    std::vector<std::filesystem::path> folders;
    std::map<std::string, Stored> entries;
    bool autoScan = true;
    Progress progress;
    std::thread scanThread;
    std::atomic<bool> cancelScan { false };
    mutable std::mutex saveMutex;   // one writer of the index file at a time

    void scan (ProgressCallback);
    void load();

    /** Writes the index: takes the mutex only to snapshot it, so it mustn't be held. */
    void save() const;

    /** analyseFile, for a file whose index key is known. */
    static SampleEntry analyseEntry (const std::filesystem::path&, std::string key);
};

/** MIDI note names: 33 -> "A1". */
std::string noteName (int key);

} // namespace gk::library
