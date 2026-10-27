#pragma once

#include "Clap.h"
#include "Groove.h"
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
    mix's loudness and balance, and the groove (the tempo from the kick unless it's known). Blocks
    for several seconds. Throws if the file can't be read or is shorter than 8 seconds. */
StyleProfile::Track hearTrack (Separator&, Clap&, const std::filesystem::path&, std::optional<double> knownTempo = {});

/** The user's styles: one profile per folder of reference tracks, built in the background
    (separation and listening take a few seconds per track) and kept in a folder of JSON files.
    Thread-safe. */
class Styles
{
public:
    Styles (const Models&, Clap&, std::filesystem::path folder);
    ~Styles();

    static std::filesystem::path getDefaultFolder();

    struct Progress
    {
        bool running = false;
        std::string style;
        int tracks = 0, analysed = 0, failed = 0;
        std::string error;
    };

    using ProgressCallback = std::function<void (const Progress&)>;

    std::vector<std::string> names() const;
    std::shared_ptr<const StyleProfile> get (const std::string& name) const;

    /** Adds a folder of reference tracks as a style (named after the folder unless named) and
        starts analysing it. Adding it again re-analyses new and changed tracks. Throws if
        another analysis is running. */
    std::string add (const std::filesystem::path& folder, std::string name, ProgressCallback);
    void remove (const std::string& name);

    Progress getProgress() const;
    void wait();

    /** Stops a running analysis and waits for it. */
    void stop();

private:
    const Models& models;
    Clap& clap;
    const std::filesystem::path folder;

    mutable std::mutex mutex;
    std::map<std::string, std::shared_ptr<const StyleProfile>> profiles;
    Progress progress;
    std::thread thread;
    std::atomic<bool> cancel { false };

    void analyse (StyleProfile, ProgressCallback);

    /** Makes the profile with these tracks the style's, on disk and here. */
    void publish (StyleProfile&, const std::vector<StyleProfile::Track>&);
    std::filesystem::path fileFor (const std::string& name) const;
};

} // namespace gk::listening
