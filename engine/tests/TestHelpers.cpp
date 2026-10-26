#include "TestHelpers.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <tracktion_engine/tracktion_engine.h>

namespace gk::test
{

namespace
{
    std::unique_ptr<EngineContext> context;

    /** Destroys the engine before static destruction, so JUCE's leak detectors see a clean shutdown. */
    class EngineShutdown : public Catch::EventListenerBase
    {
    public:
        using EventListenerBase::EventListenerBase;

        void testRunEnded (const Catch::TestRunStats&) override
        {
            context.reset();
        }
    };
}

EngineContext& getEngineContext()
{
    if (context == nullptr)
        context = std::make_unique<EngineContext>();

    return *context;
}

} // namespace gk::test

CATCH_REGISTER_LISTENER (gk::test::EngineShutdown)
