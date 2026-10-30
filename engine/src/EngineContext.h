#pragma once

#include <exception>
#include <filesystem>
#include <functional>
#include <memory>

namespace tracktion { inline namespace engine { class Engine; } }

namespace gk
{

/** Owns the JUCE runtime and the Tracktion Engine for a headless process. */
class EngineContext
{
public:
    struct Options
    {
        /** Scan plugins in a child copy of this executable, which must then call
            runPluginScanChildIfRequested() first thing in main(). */
        bool outOfProcessPluginScanning = false;

        /** Where Tracktion keeps its settings (devices, caches); empty for getAppDataFolder(). */
        std::filesystem::path settingsFolder;
    };

    EngineContext();
    explicit EngineContext (Options);
    ~EngineContext();

    /** If this process was started as a plugin-scanning child, serves scan requests until the
        parent goes away (then exits) and never returns. Otherwise returns straight away. */
    static void runPluginScanChildIfRequested (int argc, char* argv[]);

    tracktion::Engine& getEngine();

    /** Runs work that takes long (a render, a hearing) on one of a few pool threads, and brings
        what it returns back to the message thread with done (its result, or the exception the
        work threw), so the message thread serves other requests meanwhile. The work must not
        touch the Session or create Edits (message thread only); it may render Edits made for it.
        done, and everything it captured, go on the message thread. Jobs still running when the
        context goes are waited for. */
    using Job = std::function<std::shared_ptr<void>()>;
    using Done = std::function<void (std::shared_ptr<void> result, std::exception_ptr error)>;
    void runJob (Job work, Done done);

    /** ~/Library/Application Support/GrooveKit: settings, the sample library index, the plugin list. */
    static std::filesystem::path getAppDataFolder();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace gk
