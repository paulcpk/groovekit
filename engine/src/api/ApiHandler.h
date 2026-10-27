#pragma once

#include "session/Session.h"

namespace gk
{

class EngineContext;
class LiveEngine;
class PluginCatalog;
namespace library { class SampleLibrary; }
namespace listening { class Listening; }

/** JSON-RPC 2.0 over any message transport. Lives on the message thread.

    Requests: execute, undo, redo, get, getSummary, getHistory, render, analyze, analyzeMix, master,
    listEffects, hello, library.*, plugins.*, listening.*, styles.*, and with a live engine
    transport.*, record.* and audio.*.
    Notifications to clients: "snapshot" on connect, "patch" after every change from any client,
    "libraryProgress" and "pluginScanProgress" while scanning, "listening" while listening models
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
                listening::Listening* = nullptr);
    ~ApiHandler();

    /** Opens another project folder in place of this session (see project.open and project.new).
        Throws if it can't (e.g. another engine serves it); the switch itself happens later,
        after the response is sent, and replaces this handler. Not set: projects can't be switched. */
    std::function<void (const std::filesystem::path&)> onOpenProject;

    /** Where project.recent reads the list from; empty for <app data>/recent.json. */
    std::filesystem::path recentFile;

    /** Where the user's presets are kept; empty for <app data>/presets. */
    std::filesystem::path presetsFolder;

    /** Called for every change; the server sends the message to all clients. */
    void setBroadcast (Send);

    /** The notification a client gets when it connects. */
    std::string getSnapshotMessage() const;

    /** Handles one incoming message and sends the response through client.send. */
    void handleMessage (const std::string& text, Client&);

    /** The JSON-RPC result for one method call. Throws on errors. */
    model::Json call (const std::string& method, const model::Json& params, Client&);

private:
    Session& session;
    EngineContext& context;
    library::SampleLibrary* library;
    LiveEngine* live;
    PluginCatalog* plugins;
    listening::Listening* listening;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    Send broadcast;
    int listenerId = 0;
    std::string lastOrigin;
    int renderCount = 0;

    std::map<std::string, std::pair<std::filesystem::file_time_type, model::Json>> peaksCache;

    model::Json render (const model::Json& params);
    model::Json peaks (const model::Json& params);
    model::Json captureState (const std::string& pluginKey, const std::string& origin);
    model::Json callLibrary (const std::string& method, const model::Json& params);
    model::Json callLive (const std::string& method, const model::Json& params, Client&);
    model::Json callPlugins (const std::string& method, const model::Json& params, Client&);
    model::Json callProject (const std::string& method, const model::Json& params);
    model::Json callVoice (const std::string& method, const model::Json& params);
    model::Json callListening (const std::string& method, const model::Json& params);
    model::Json compareToStyle (const model::Json& params);
    model::Json importIntoProject (const std::string& method, const model::Json& params);
    void startLibraryScan();
};

} // namespace gk
