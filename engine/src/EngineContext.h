#pragma once

#include <filesystem>
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

    /** ~/Library/Application Support/GrooveKit: settings, the sample library index, the plugin list. */
    static std::filesystem::path getAppDataFolder();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace gk
