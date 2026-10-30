#include "EngineContext.h"

#include "compiler/EditGraveyard.h"

#include "live/MidiClockPlugin.h"
#include "effects/Effects.h"
#include "effects/EqPlugin.h"
#include "effects/Saturation.h"
#include "effects/Shapers.h"
#include "effects/StereoPlugin.h"
#include "mixer/EffectMix.h"
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

        // Each effect takes three plugins (mix tap, effect, blend), so the default 16 per track
        // would turn away a sixth effect. The song decides how much there is, not these limits.
        tracktion::EditLimits getEditLimits() override  { return { .maxNumTracks = 100000, .maxClipsInTrack = 1000000,
                                                                    .maxPluginsOnClip = 1000, .maxPluginsOnTrack = 10000,
                                                                    .maxNumMasterPlugins = 1000 }; }

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
    /** Two at a time: a render uses several cores itself. */
    juce::ThreadPool jobs { juce::ThreadPoolOptions().withNumberOfThreads (2).withThreadName ("GrooveKit jobs") };
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
    plugins.createBuiltInType<effects::EqPlugin>();
    plugins.createBuiltInType<effects::DelayPlugin>();
    plugins.createBuiltInType<effects::ReverbPlugin>();
    plugins.createBuiltInType<effects::ChorusPlugin>();
    plugins.createBuiltInType<effects::DistortionPlugin>();
    plugins.createBuiltInType<effects::CompressorPlugin>();
    plugins.createBuiltInType<effects::MultibandPlugin>();
    plugins.createBuiltInType<effects::ExciterPlugin>();
    plugins.createBuiltInType<effects::BitcrusherPlugin>();
    plugins.createBuiltInType<effects::DuckPlugin>();
    plugins.createBuiltInType<effects::ClipperPlugin>();
    plugins.createBuiltInType<effects::StereoPlugin>();
    plugins.createBuiltInType<mixer::EffectMixTap>();
    plugins.createBuiltInType<mixer::EffectMixBlend>();
    plugins.createBuiltInType<MidiClockPlugin>();
    plugins.createBuiltInType<mixer::LimiterPlugin>();
}
EngineContext::~EngineContext()
{
    // Jobs may still hold Edits: they finish first, then the discarded Edits go, before the Engine
    // they belong to.
    impl->jobs.removeAllJobs (false, 600000);
    EditGraveyard::getInstance().flush();
}

void EngineContext::runJob (Job work, Done done)
{
    impl->jobs.addJob ([work = std::move (work), done = std::move (done)]
    {
        std::shared_ptr<void> result;
        std::exception_ptr error;

        try
        {
            result = work();
        }
        catch (...)
        {
            error = std::current_exception();
        }

        // The result and done, and whatever they hold, are released on the message thread.
        juce::MessageManager::callAsync ([done, result = std::move (result), error] { done (result, error); });
    });
}

std::filesystem::path EngineContext::getAppDataFolder()
{
    // GROOVEKIT_APP_DATA keeps a test engine's settings, indexes and styles apart from the app's.
    if (auto env = std::getenv ("GROOVEKIT_APP_DATA"))
        return env;

    // JUCE's userApplicationDataDirectory is ~/Library on macOS.
    auto library = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    return std::filesystem::path (library.getFullPathName().toStdString()) / "Application Support" / "GrooveKit";
}

tracktion::Engine& EngineContext::getEngine()
{
    return impl->engine;
}

} // namespace gk
