#pragma once

#include "library/SampleLibrary.h"
#include "session/Session.h"
#include "session/Steps.h"

namespace gk
{

class EngineContext;
class LiveEngine;
class PluginCatalog;
namespace library { class StarterPack; }
namespace guides { struct Guide; }
namespace listening { class Listening; struct StyleProfile; }

/** JSON-RPC 2.0 over any message transport. Lives on the message thread.

    Requests: execute, undo, redo, get, getSummary, getHistory, render, analyze, analyzeMix, master,
    listEffects, hello, library.*, plugins.*, listening.*, styles.*, chats.*, and with a live engine
    transport.*, record.*, audio.* and machines.*.
    Notifications to clients: "snapshot" on connect, "patch" after every change from any client,
    "libraryProgress" and "pluginScanProgress" while scanning, "starter" while the starter samples
    download, "listening" while listening models
    download, the library is heard or a style is analysed, "transport" while playing, "levels"
    while there's signal.
    See docs/protocol.md.
*/
class ApiHandler
{
public:
    using Send = std::function<void (const std::string& message)>;

    struct Client
    {
        std::string origin = "client";
        Send send;
    };

    ApiHandler (Session&, EngineContext&, library::SampleLibrary* = nullptr, LiveEngine* = nullptr, PluginCatalog* = nullptr,
                listening::Listening* = nullptr, library::StarterPack* = nullptr);
    ~ApiHandler();

    /** What the plugins with their windows open (in front or in the background) sound like now,
        into the project, as closing their windows does: before the song is saved, or closed. */
    void captureOpenPluginStates();

    /** Opens another project folder in place of this session (see project.open and project.new).
        Throws if it can't (e.g. another engine serves it); the switch itself happens later,
        after the response is sent, and replaces this handler. Not set: projects can't be switched. */
    std::function<void (const std::filesystem::path&)> onOpenProject;

    /** Where project.recent reads the list from; empty for <app data>/recent.json. */
    std::filesystem::path recentFile;

    /** Where the user's presets are kept; empty for <app data>/presets. */
    std::filesystem::path presetsFolder;

    /** Where the agent's conversations are kept; empty for <app data>/chats. */
    std::filesystem::path chatsFolder;

    /** Where the user keeps reference tracks; empty for ~/Music/GrooveKit/References. */
    std::filesystem::path referencesFolder;

    /** Where the user's notes for the agent are kept; empty for <app data>/agent-notes.json. */
    std::filesystem::path notesFile;

    /** Where the setting for sounds suggested in the background is kept; empty for
        <app data>/suggestions.json. */
    std::filesystem::path suggestionsFile;

    /** Lets this handler pick out sounds for the song in the background when the user has that on
        (suggestions.*): only the app's engine does, not tests or the command line. */
    void enableBackgroundSuggestions();

    /** Stops picking out sounds in the background (for quitting): what's running stops after its step. */
    void stopBackgroundWork()       { backgroundSuggestions = false; stopSuggestions(); }

    /** Called for every change; the server sends the message to all clients. */
    void setBroadcast (Send);

    /** The notification a client gets when it connects. */
    std::string getSnapshotMessage() const;

    /** Handles one incoming message and sends the response through client.send: at once, or
        once a method that renders or hears is done on a pool thread (see dispatch). */
    void handleMessage (const std::string& text, Client&);

    /** The JSON-RPC result for one method call, waited for. Throws on errors. */
    model::Json call (const std::string& method, const model::Json& params, Client&);

private:
    Session& session;
    EngineContext& context;
    library::SampleLibrary* library;
    LiveEngine* live;
    PluginCatalog* plugins;
    listening::Listening* listening;
    library::StarterPack* starter;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    Send broadcast;
    int listenerId = 0;
    std::string lastOrigin;
    int renderCount = 0;

    std::map<std::string, std::pair<std::filesystem::file_time_type, model::Json>> peaksCache;

    /** Samples analysed for the mix methods, by path and change time, so a channel's sound is
        decoded and classified once rather than on every call. */
    std::map<std::string, std::pair<std::filesystem::file_time_type, library::SampleEntry>> entryCache;

    /** Choices put to the user (A/B/C), by id: what was offered and what they picked (-1 none of
        them; unset while waiting). */
    struct Offer
    {
        model::Json offer;
        std::optional<int> choice;
        std::string note;               // "Other": what the user wants instead, in their words
        bool more = false;              // "Another round": none of these, and as many new ones again
        bool closed = false;            // The window closed without a choice: not this part, for now
    };

    std::map<int, Offer> offers;
    int offerCount = 0;

    /** The last few voice messages' audio (16 kHz mono), by job id, newest last: a melody hummed
        in one can be placed later. */
    std::vector<std::pair<std::string, std::vector<float>>> voiceRecordings;

    /** Methods that render or hear (render, analyze, analyzeMix, master, mix.check, mix.audition,
        picks.offer, styles.compare) don't answer from dispatch: they set this, steps the message
        thread and the pool take turns on, and handleMessage sends the response when they're done,
        so the message thread serves other requests meanwhile. */
    std::shared_ptr<Steps> deferred;

    /** The JSON-RPC result for one method call, or an empty result with deferred set. Throws on errors. */
    model::Json dispatch (const std::string& method, const model::Json& params, Client&);
    static model::Json errorResponse (const model::Json& id, std::exception_ptr);

    model::Json callSession (const std::string& method, const model::Json& params, Client&);
    model::Json callRender (const std::string& method, const model::Json& params, Client&);
    model::Json callPresets (const std::string& method, const model::Json& params);
    model::Json render (const model::Json& params);
    model::Json peaks (const model::Json& params);

    /** The style a mix method works against: the one named in params, else the song's; a guide's
        name stands for the style whose references go with it. Null without one. */
    std::shared_ptr<const listening::StyleProfile> styleProfile (const model::Json& params) const;

    /** mix.audition as steps: the trials are built and their Edits compiled on the message
        thread, rendered and measured on the pool; result is the audition's. With tolerant,
        a candidate that can't be tried is reported with an error instead of failing the whole. */
    struct AuditionOptions
    {
        /** Where the auditions go; empty for <cache>/auditions/in-song. */
        std::filesystem::path folder;
        /** The song already rendered over the same bars (at 44.1 kHz), to put each candidate in
            without rendering the song again: a candidate in a slot of its own is then rendered alone,
            from a project of only itself (the song's tempo, key and master). */
        std::filesystem::path rest;
    };

    std::shared_ptr<Steps> auditionSteps (const model::Json& params, bool tolerant, const AuditionOptions& = {});
    model::Json captureState (const std::string& pluginKey, const std::string& origin);
    model::Json callLibrary (const std::string& method, const model::Json& params);
    model::Json callLive (const std::string& method, const model::Json& params, Client&);
    model::Json callPlugins (const std::string& method, const model::Json& params, Client&);
    model::Json callProject (const std::string& method, const model::Json& params);
    model::Json callVoice (const std::string& method, const model::Json& params);
    model::Json callMelody (const std::string& method, const model::Json& params);
    model::Json callListening (const std::string& method, const model::Json& params);
    model::Json callMaps (const std::string& method, const model::Json& params);
    model::Json callVoices (const std::string& method, const model::Json& params);
    model::Json callMatch (const std::string& method, const model::Json& params);
    model::Json matchMaster (const model::Json& params, Client&);
    model::Json callMix (const std::string& method, const model::Json& params, Client&);
    model::Json callChats (const std::string& method, const model::Json& params);
    model::Json callNotes (const std::string& method, const model::Json& params);
    model::Json callGuides (const std::string& method, const model::Json& params);

    // Sounds suggested for the song, picked out and tried in the background (SuggestionsApi.cpp).
    struct Suggestions;
    std::shared_ptr<Suggestions> suggestions;
    bool backgroundSuggestions = false;
    Suggestions& getSuggestions();
    std::filesystem::path suggestionsSettingsFile() const;
    model::Json callSuggestions (const std::string& method, const model::Json& params);
    model::Json suggestionsJson (bool full, const std::string& which, int limit);
    void songChangedForSuggestions();
    void startSuggestions();
    void trySuggestions (std::shared_ptr<std::atomic<bool>> round, int64_t edit, int pass, size_t next);
    void stopSuggestions();
    void notifySuggestions();

    /** The guide a style or a folder of references goes with, by name, among the guides listed
        once for the request; null without one. */
    static model::Json guideFor (const std::vector<guides::Guide>&, const std::string& styleName);

    /** What the library knows of a sample, else an analysis of the file (kept in entryCache).
        Throws std::runtime_error if the file can't be read. */
    library::SampleEntry entryFor (const std::filesystem::path&);
    model::Json compareToStyle (const model::Json& params);
    model::Json importIntoProject (const std::string& method, const model::Json& params);
    void startLibraryScan();
};

} // namespace gk
