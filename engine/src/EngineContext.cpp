#include "EngineContext.h"

#include "mixer/FaderPlugin.h"
#include "mixer/LimiterPlugin.h"
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
}

struct EngineContext::Impl
{
    explicit Impl (Options o) : engine ("GrooveKit", nullptr, std::make_unique<HeadlessBehaviour> (o.outOfProcessPluginScanning)) {}

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
    plugins.createBuiltInType<mixer::FaderPlugin>();
    plugins.createBuiltInType<mixer::LimiterPlugin>();
}
EngineContext::~EngineContext() = default;

tracktion::Engine& EngineContext::getEngine()
{
    return impl->engine;
}

} // namespace gk
