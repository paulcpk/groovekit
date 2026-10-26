#include "PluginCatalog.h"

#include "EngineContext.h"

namespace gk
{

namespace fs = std::filesystem;

PluginCatalog::PluginCatalog (tracktion::Engine& e, fs::path file)
    : engine (e), listFile (std::move (file))
{
    if (auto xml = juce::parseXML (juce::File (listFile.string())))
        engine.getPluginManager().knownPluginList.recreateFromXml (*xml);
}

PluginCatalog::~PluginCatalog()
{
    stopTimer();
}

fs::path PluginCatalog::getDefaultListFile()
{
    if (auto env = std::getenv ("GROOVEKIT_PLUGIN_LIST"))
        return env;

    return fs::path (EngineContext::getAppDataFolder()) / "plugins.xml";
}

bool PluginCatalog::startScan (std::function<void (const Progress&)> callback,
                               std::function<bool (const juce::String&)> filter)
{
    if (progress.running)
        return false;

    onProgress = std::move (callback);
    scanners.clear();
    progress = { true, 0, 0, {} };

    auto& manager = engine.getPluginManager();
    const auto deadMansPedal = juce::File ((listFile.parent_path() / "plugin-scan-in-progress.txt").string());
    fs::create_directories (listFile.parent_path());

    for (auto format : manager.pluginFormatManager.getFormats())
    {
        const auto name = format->getName();

        if (name != "VST3" && name != "AudioUnit")
            continue;

        auto scanner = std::make_unique<juce::PluginDirectoryScanner> (manager.knownPluginList, *format,
                                                                       format->getDefaultLocationsToSearch(),
                                                                       true, deadMansPedal, true);
        auto files = format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, true);

        if (filter)
        {
            juce::StringArray accepted;

            for (auto& f : files)
                if (filter (f))
                    accepted.add (f);

            files = accepted;
            scanner->setFilesOrIdentifiersToScan (files);
        }

        progress.total += files.size();
        scanners.push_back (std::move (scanner));
    }

    if (onProgress)
        onProgress (progress);

    startTimer (1);
    return true;
}

void PluginCatalog::timerCallback()
{
    if (scanners.empty())
    {
        finishScan();
        return;
    }

    auto& scanner = *scanners.front();
    juce::String name;
    progress.current = scanner.getNextPluginFileThatWillBeScanned().toStdString();

    if (! scanner.scanNextFile (true, name))
        scanners.erase (scanners.begin());

    ++progress.scanned;

    if (onProgress)
        onProgress (progress);
}

void PluginCatalog::finishScan()
{
    stopTimer();
    progress.running = false;
    progress.current.clear();
    save();

    if (onProgress)
        onProgress (progress);
}

void PluginCatalog::save() const
{
    if (auto xml = engine.getPluginManager().knownPluginList.createXml())
    {
        fs::create_directories (listFile.parent_path());
        xml->writeTo (juce::File (listFile.string()));
    }
}

std::vector<juce::PluginDescription> PluginCatalog::getPlugins() const
{
    std::vector<juce::PluginDescription> list;

    for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
        if (d.pluginFormatName == "VST3" || d.pluginFormatName == "AudioUnit")
            list.push_back (d);

    return list;
}

std::optional<juce::PluginDescription> PluginCatalog::find (const std::string& uid) const
{
    if (auto d = engine.getPluginManager().knownPluginList.getTypeForIdentifierString (uid))
        return *d;

    return {};
}

std::string PluginCatalog::formatName (const juce::PluginDescription& d)
{
    return d.pluginFormatName == "AudioUnit" ? "AU" : d.pluginFormatName.toStdString();
}

model::Json PluginCatalog::toJson (const juce::PluginDescription& d)
{
    return {
        { "uid", d.createIdentifierString().toStdString() },
        { "name", d.name.toStdString() },
        { "manufacturer", d.manufacturerName.toStdString() },
        { "format", formatName (d) },
        { "instrument", d.isInstrument },
        { "category", d.category.toStdString() },
        { "inputs", d.numInputChannels },
        { "outputs", d.numOutputChannels },
    };
}

} // namespace gk
