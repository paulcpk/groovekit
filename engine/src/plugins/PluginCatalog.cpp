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

    addBundledPlugins();
}

std::vector<fs::path> PluginCatalog::getBundledPluginFolders()
{
    std::vector<fs::path> folders;

    if (auto env = std::getenv ("GROOVEKIT_BUNDLED_PLUGINS"))
        folders.emplace_back (env);

    // In the app: Contents/MacOS/groovekit-engine, beside Contents/Resources.
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    folders.push_back (fs::path (exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/Plug-Ins").getFullPathName().toStdString()));

   #ifdef GK_BUILT_PLUGINS
    folders.emplace_back (GK_BUILT_PLUGINS);
   #endif

    return folders;
}

void PluginCatalog::addBundledPlugins()
{
    auto& manager = engine.getPluginManager();
    juce::AudioPluginFormat* vst3 = nullptr;

    for (auto* format : manager.pluginFormatManager.getFormats())
        if (format->getName() == "VST3")
            vst3 = format;

    if (vst3 == nullptr)
        return;

    auto& list = manager.knownPluginList;
    bool changed = false;

    // The first folder that has them wins: the app's own copy over a build tree's.
    for (auto& folder : getBundledPluginFolders())
    {
        std::error_code ec;
        juce::StringArray paths;

        for (auto& entry : fs::directory_iterator (folder, ec))
            if (entry.path().extension() == ".vst3")
                paths.add (juce::File (entry.path().string()).getFullPathName());   // as the list keeps them, without ".."

        if (paths.isEmpty())
            continue;

        // Scanned once, and again when they change.
        for (auto& path : paths)
        {
            if (! list.isListingUpToDate (path, *vst3))
            {
                juce::OwnedArray<juce::PluginDescription> found;
                list.scanAndAddFile (path, false, found, *vst3);
                changed = true;
            }
        }

        // Copies elsewhere (another build, an older app) are forgotten, so each is listed once.
        for (auto& type : list.getTypes())
        {
            if (type.manufacturerName == "GrooveKit" && ! paths.contains (type.fileOrIdentifier))
            {
                list.removeType (type);
                changed = true;
            }
        }

        break;
    }

    if (changed)
        save();
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

std::optional<juce::PluginDescription> PluginCatalog::find (const juce::KnownPluginList& list, const std::string& uid)
{

    if (auto d = list.getTypeForIdentifierString (uid))
        return *d;

    // Identifiers end in a hash of the plugin's file and its id: "VST3-Triad-1a2b3c-49b6ccb2". The
    // same plugin moved to another file (reinstalled, or GrooveKit's own in the app rather than a
    // build) keeps its format, name and id.
    const auto id = juce::String (uid);
    const auto withoutId = id.upToLastOccurrenceOf ("-", false, false);
    const auto prefix = withoutId.upToLastOccurrenceOf ("-", true, false);
    const auto suffix = id.fromLastOccurrenceOf ("-", true, false);

    for (auto& d : list.getTypes())
    {
        const auto other = d.createIdentifierString();

        if (other.startsWith (prefix) && other.endsWith (suffix)
            && other.substring (prefix.length()).upToLastOccurrenceOf ("-", false, false).containsOnly ("0123456789abcdef"))
            return d;
    }

    return {};
}

std::optional<juce::PluginDescription> PluginCatalog::find (const std::string& uid) const
{
    return find (engine.getPluginManager().knownPluginList, uid);
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
