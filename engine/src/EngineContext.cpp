#include "EngineContext.h"

#include "compiler/EditGraveyard.h"

#include "live/MidiClockPlugin.h"
#include "mixer/FaderPlugin.h"
#include "mixer/LimiterPlugin.h"
#include "plugins/PluginWindows.h"
#include "sampler/AudioChannelPlugin.h"
#include "sampler/SamplerPlugin.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk
{

namespace
{
    /** Headless behaviour: no audio device is opened unless something asks for one. */
    struct HeadlessBehaviour : tracktion::EngineBehaviour
    {
        explicit HeadlessBehaviour (bool scanOutOfProcess) : outOfProcess (scanOutOfProcess) {}

        bool autoInitialiseDeviceManager() override     { return false; }
        bool canScanPluginsOutOfProcess() override      { return outOfProcess; }

        const bool outOfProcess;
    };

    /** Tracktion's settings, in a chosen folder instead of ~/Library/GrooveKit. */
    struct FolderPropertyStorage : tracktion::PropertyStorage
    {
        explicit FolderPropertyStorage (std::filesystem::path f) : PropertyStorage ("GrooveKit"), folder (std::move (f)) {}

        juce::File getAppPrefsFolder() override     { return juce::File (folder.string()); }
        juce::File getAppCacheFolder() override     { return juce::File ((folder / "cache").string()); }

        const std::filesystem::path folder;
    };

    std::unique_ptr<tracktion::PropertyStorage> createStorage (const EngineContext::Options& o)
    {
        const auto folder = o.settingsFolder.empty() ? EngineContext::getAppDataFolder() : o.settingsFolder;
        std::filesystem::create_directories (folder);
        return std::make_unique<FolderPropertyStorage> (folder);
    }
}

struct EngineContext::Impl
{
    explicit Impl (Options o)
        : engine (createStorage (o), std::make_unique<PluginWindowBehaviour>(), std::make_unique<HeadlessBehaviour> (o.outOfProcessPluginScanning)) {}

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    tracktion::Engine engine;
};

void EngineContext::runPluginScanChildIfRequested (int argc, char* argv[])
{
    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    if (! args.joinIntoString (" ").contains ("PluginScan"))
        return;

    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (! tracktion::PluginManager::startChildProcessPluginScan (args.joinIntoString (" ")))
        return;

    // The child exits by itself when the parent disconnects.
    for (;;)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1000);
}

EngineContext::EngineContext() : EngineContext (Options {}) {}

EngineContext::EngineContext (Options options) : impl (std::make_unique<Impl> (options))
{
    auto& plugins = impl->engine.getPluginManager();
    plugins.createBuiltInType<sampler::SamplerPlugin>();
    plugins.createBuiltInType<sampler::AudioChannelPlugin>();
    plugins.createBuiltInType<mixer::FaderPlugin>();
    plugins.createBuiltInType<MidiClockPlugin>();
    plugins.createBuiltInType<mixer::LimiterPlugin>();
}
EngineContext::~EngineContext()
{
    // Discarded Edits must go before the Engine they belong to.
    EditGraveyard::getInstance().flush();
}

std::filesystem::path EngineContext::getAppDataFolder()
{
    // JUCE's userApplicationDataDirectory is ~/Library on macOS.
    auto library = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    return std::filesystem::path (library.getFullPathName().toStdString()) / "Application Support" / "GrooveKit";
}

tracktion::Engine& EngineContext::getEngine()
{
    return impl->engine;
}

} // namespace gk
