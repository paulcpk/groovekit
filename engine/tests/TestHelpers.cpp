#include "TestHelpers.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <tracktion_engine/tracktion_engine.h>

namespace gk::test
{

namespace
{
    std::unique_ptr<EngineContext> context;
    std::vector<std::function<void()>> shutdownActions;

    /** Destroys the engine before static destruction, so JUCE's leak detectors see a clean shutdown. */
    class EngineShutdown : public Catch::EventListenerBase
    {
    public:
        using EventListenerBase::EventListenerBase;

        void testRunEnded (const Catch::TestRunStats&) override
        {
            for (auto& action : shutdownActions)
                action();

            shutdownActions.clear();
            context.reset();
        }
    };
}

void atShutdown (std::function<void()> action)
{
    shutdownActions.push_back (std::move (action));
}

EngineContext& getEngineContext()
{
    if (context == nullptr)
        context = std::make_unique<EngineContext>();

    return *context;
}

} // namespace gk::test

CATCH_REGISTER_LISTENER (gk::test::EngineShutdown)
