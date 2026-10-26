#include "EngineContext.h"

#include "mixer/FaderPlugin.h"
#include "sampler/SamplerPlugin.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk
{

namespace
{
    /** Headless behaviour: no audio device is opened unless something asks for one. */
    struct HeadlessBehaviour : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override { return false; }
    };
}

struct EngineContext::Impl
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    tracktion::Engine engine { "GrooveKit", nullptr, std::make_unique<HeadlessBehaviour>() };
};

EngineContext::EngineContext() : impl (std::make_unique<Impl>())
{
    auto& plugins = impl->engine.getPluginManager();
    plugins.createBuiltInType<sampler::SamplerPlugin>();
    plugins.createBuiltInType<mixer::FaderPlugin>();
}
EngineContext::~EngineContext() = default;

tracktion::Engine& EngineContext::getEngine()
{
    return impl->engine;
}

} // namespace gk
