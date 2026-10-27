#pragma once

#include "model/JsonIO.h"

#include <tracktion_engine/tracktion_engine.h>

#include <filesystem>

namespace gk
{

/** The VST3 and AU plugins on this machine, as found by scanning, and GrooveKit's own (the
    Triad synth), which are registered when the catalog opens without a scan.

    Scanning runs on the message thread one plugin at a time, so the engine keeps answering
    in between. With out-of-process scanning enabled (see EngineContext) each plugin is
    loaded in a child process, so one that crashes is skipped instead of taking the engine
    down. The list is kept in an XML file between runs.
*/
class PluginCatalog : private juce::Timer
{
public:
    PluginCatalog (tracktion::Engine&, std::filesystem::path listFile);
    ~PluginCatalog() override;

    static std::filesystem::path getDefaultListFile();

    /** Where GrooveKit's own plugins are: GROOVEKIT_BUNDLED_PLUGINS, the app's Resources/Plug-Ins,
        and (for development) the build tree. */
    static std::vector<std::filesystem::path> getBundledPluginFolders();

    struct Progress
    {
        bool running = false;
        int scanned = 0, total = 0;
        std::string current;
    };

    /** Scans the default VST3 and AU locations, or only the files and identifiers the filter
        accepts. Returns false if a scan is already running. */
    bool startScan (std::function<void (const Progress&)> onProgress = {},
                    std::function<bool (const juce::String& fileOrIdentifier)> filter = {});
    Progress getProgress() const        { return progress; }

    std::vector<juce::PluginDescription> getPlugins() const;

    /** By identifier (PluginDescription::createIdentifierString), or the same plugin (format,
        name and id) at another file when it has moved. */
    std::optional<juce::PluginDescription> find (const std::string& uid) const;
    static std::optional<juce::PluginDescription> find (const juce::KnownPluginList&, const std::string& uid);

    static model::Json toJson (const juce::PluginDescription&);

    /** "VST3" or "AU", the format names used in projects. */
    static std::string formatName (const juce::PluginDescription&);

private:
    tracktion::Engine& engine;
    const std::filesystem::path listFile;
    std::vector<std::unique_ptr<juce::PluginDirectoryScanner>> scanners;
    std::function<void (const Progress&)> onProgress;
    Progress progress;

    void addBundledPlugins();
    void timerCallback() override;
    void finishScan();
    void save() const;
};

} // namespace gk
