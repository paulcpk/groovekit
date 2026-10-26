#pragma once

#include <memory>

namespace tracktion { inline namespace engine { class Engine; } }

namespace gk
{

/** Owns the JUCE runtime and the Tracktion Engine for a headless process. */
class EngineContext
{
public:
    EngineContext();
    ~EngineContext();

    tracktion::Engine& getEngine();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace gk
