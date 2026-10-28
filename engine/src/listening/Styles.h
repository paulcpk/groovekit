#pragma once

#include "Arrangement.h"
#include "Clap.h"
#include "Groove.h"
#include "KickSound.h"
#include "Separator.h"

#include "model/JsonIO.h"

namespace gk::listening
{

/** What a sound does in a track, so it's compared with the same part of the references. */
enum class Role
{
    mix,        // everything
    drums,      // the drums stem: drum loops, tops, percussion
    kick,       // the drums below 200 Hz: kick loops and rumbles
    hats,       // the drums above 5 kHz: hat and cymbal loops
    bass,
    music,      // everything else: synths, pads, stabs, vocals, effects
    kickHit,    // single kicks cut from the drums: kick one-shots
    hatHit,     // single hats and cymbals
    percHit,    // single claps, snares and percussion
};

constexpr Role allRoles[] = { Role::mix, Role::drums, Role::kick, Role::hats, Role::bass, Role::music, Role::kickHit, Role::hatHit, Role::percHit };

std::string roleName (Role);
std::optional<Role> roleFromName (const std::string&);

/** The role of a library sample from its category and whether it loops. */
Role roleOf (const std::string& category, bool loop);

/** A style: what a folder of reference tracks sounds like, part by part, and how it moves. */
struct StyleProfile
{
    std::string name;
    std::filesystem::path folder;

    struct Track
    {
        std::string path;
        int64_t size = 0, modified = 0;
        int analysis = 0;                           // the version of the analysis that heard it
        double tempo = 0;
        std::optional<double> swing;
        double density = 0;
        float lufs = 0;
        std::vector<float> balance;                 // the mix's spectral balance (dB per band)
        std::map<Role, float> levels;               // each stem's level against the mix, dB
        std::map<Role, std::vector<Embedding>> sounds;
        std::optional<Arrangement> arrangement;     // heard from the whole track (analysis 6 on)

        /** The kick and rumble, measured where the kick plays with least around it (bars from 0,
            to excluded; clean when nothing but the low end plays there). */
        struct Kick
        {
            KickSound sound;
            int from = 0, to = 0;
            bool clean = false;
        };

        std::optional<Kick> kick;

        model::Json toJson() const;
        static Track fromJson (const model::Json&);
    };

    std::vector<Track> tracks;

    /** How close a reference track's sounds are to the other references': the scale of fit
        (computed when the profile is built). */
    std::map<Role, float> typicalFit;

    /** How close sound is to this style's sounds in a role: the mean similarity to its nearest
        reference sounds. A track index leaves that track out. */
    float fit (Role, const Embedding&, std::optional<size_t> leaveOut = {}) const;

    bool has (Role) const;

    model::Json toJson() const;     // everything, for the file
    static StyleProfile fromJson (const model::Json&);

    /** Tempo, swing, loudness, balance and levels across the tracks (medians and ranges). */
    model::Json measurements() const;
};

/** Hears a track as a style's references are heard: six stretches through its middle, each
    split into drums, bass and the rest, with what each part sounds like, the parts' levels, the
    mix's loudness and balance, and the groove (the tempo from the kick unless it's known). With
    wholeTrack, the whole track is split (about a fifth of its length) and its arrangement heard
    too. Blocks for seconds to a minute or more. Throws if the file can't be read or is shorter
    than 8 seconds. progress hears how far it is (0 to 1); returning false stops it with an error. */
StyleProfile::Track hearTrack (Separator&, Clap&, const std::filesystem::path&, std::optional<double> knownTempo = {},
                               const std::function<bool (double)>& progress = {}, bool wholeTrack = false);

/** The version of hearTrack: maps heard by an older one are used for what they have, and can be
    heard again. */
int currentAnalysisVersion();

class MapStore;
struct SongMap;

/** The user's styles, learnt from their reference tracks: every track is heard once into a song
    map (MapStore), and a style is the maps of its folder that are in use. Tracks are heard one at
    a time from a queue (separation and listening take a few seconds per track), which is kept on
    disk and carries on after a restart. Thread-safe. */
class Styles
{
public:
    /** Maps go in mapsFolder. Styles heard before maps (one profile per style in
        oldProfilesFolder) become maps the first time. */
    Styles (const Models&, Clap&, std::filesystem::path mapsFolder, std::filesystem::path oldProfilesFolder = {});
    ~Styles();

    /** Where styles were kept before maps. */
    static std::filesystem::path getDefaultFolder();

    /** Where the user keeps reference tracks, one folder per style: GROOVEKIT_REFERENCES, or
        ~/Music/GrooveKit/References. */
    static std::filesystem::path getReferencesFolder();

    MapStore& getMaps()                 { return *maps; }
    const MapStore& getMaps() const     { return *maps; }

    /** A folder of reference tracks in the references folder, and how it stands against the
        style heard from it: tracks not heard yet (new or changed) and heard ones that are gone. */
    struct ReferenceFolder
    {
        std::string name;
        std::filesystem::path path;
        int files = 0;
        std::optional<std::string> style;
        int unheard = 0, gone = 0;
    };

    /** The style folders in a references folder: every folder in it but Albums, Mixes and
        Not used, which hold whole albums, full mixes and leftovers. */
    std::vector<ReferenceFolder> referenceFolders (const std::filesystem::path& root) const;

    /** A reference track and how it stands: its audio (if it's here), its map (if it's heard),
        whether it's in use, and its status: "new", "changed" (since it was heard), "older" (heard
        by an older hearing), "heard", "queued", "hearing", "failed", "skipped", "imported" (a map
        without audio here) or "gone" (heard here, its audio since removed). */
    struct Song
    {
        std::string id, style, title;
        std::optional<std::filesystem::path> file;
        std::shared_ptr<const SongMap> map;
        std::string status;
        bool inUse = true;
        std::string error;
    };

    /** Every song in the references folder's style folders (and the folders styles were heard
        from elsewhere), and every map, by style and title. */
    std::vector<Song> songs (const std::filesystem::path& root) const;

    /** Hearing, now and next. A batch counts the songs queued since the queue was last empty. */
    struct Progress
    {
        bool running = false, paused = false;
        std::string style;                          // the song being heard's
        int tracks = 0, analysed = 0, failed = 0;   // in this batch
        std::string error;                          // why the last song failed

        struct Item { std::string id, style, title; std::filesystem::path file; };
        std::optional<Item> current;
        double currentProgress = 0;                 // 0 to 1
        int64_t currentStarted = 0;                 // ms since 1970
        std::vector<Item> queued;

        struct Done { Item item; bool ok = true; std::string error; double seconds = 0; int64_t at = 0; };
        std::vector<Done> recent;                   // newest first
        double secondsPerSong = 0;                  // of the songs heard lately
    };

    using Observer = std::function<void()>;

    /** Called (from any thread) whenever hearing moves on or a map changes. */
    void setObserver (Observer);

    std::vector<std::string> names() const;

    /** The style learnt from its maps in use, or null without any. */
    std::shared_ptr<const StyleProfile> get (const std::string& name) const;

    /** Queues a folder's tracks that aren't heard yet or changed since, as a style (named after
        the folder unless named), and starts hearing. Returns the style's name. */
    std::string add (const std::filesystem::path& folder, std::string name);

    /** Queues songs (style and audio file) to be heard, again if they were. */
    void hear (const std::vector<std::pair<std::string, std::filesystem::path>>& songs);

    /** Hearing stops after the stretch it's on, and the song goes back to the front of the
        queue; resume carries on. start begins hearing the queue unless paused. */
    void pause();
    void resume();
    void start();

    /** Takes songs out of the queue, or all of them. The song being heard, if it's one, stops
        after the stretch it's on and keeps the map it had (or none); hearing moves on. */
    void unqueue (const std::vector<std::string>& ids);
    void clearQueue();

    /** Deletes songs' maps and skips them. */
    void forget (const std::vector<std::string>& ids);

    Progress getProgress() const;

    /** Waits until the queue is empty or paused. */
    void wait();

    /** Stops hearing (the queue is kept) and waits for it. */
    void stop();

private:
    const Models& models;
    Clap& clap;
    std::unique_ptr<MapStore> maps;

    mutable std::mutex mutex;
    Progress progress;
    std::thread thread;
    std::atomic<bool> cancel { false }, pausing { false };
    std::atomic<bool> skipping { false };   // the song being heard was taken out of the queue
    Observer observer;

    struct Cached { uint64_t version = 0; std::shared_ptr<const StyleProfile> profile; };
    mutable std::mutex cacheMutex;
    mutable std::map<std::string, Cached> cache;

    void run();
    void notify();

    /** Writes the queue to queue.json; the caller holds the lock. */
    void saveQueue();
    void loadQueue();
    void migrate (const std::filesystem::path& oldProfilesFolder);
};

} // namespace gk::listening
