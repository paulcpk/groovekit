#pragma once

#include "model/JsonIO.h"

#include <tracktion_engine/tracktion_engine.h>

#include <filesystem>

namespace gk
{

/** The VST3 and AU plugins on this machine, as found by scanning.

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

    /** By identifier (PluginDescription::createIdentifierString). */
    std::optional<juce::PluginDescription> find (const std::string& uid) const;

    static model::Json toJson (const juce::PluginDescription&);

    /** "VST3" or "AU", the format names used in projects. */
    static std::string formatName (const juce::PluginDescription&);

private:
    tracktion::Engine& engine;
    const std::filesystem::path listFile;
    std::vector<std::unique_ptr<juce::PluginDirectoryScanner>> scanners;
    std::function<void (const Progress&)> onProgress;
    Progress progress;

    void timerCallback() override;
    void finishScan();
    void save() const;
};

} // namespace gk
