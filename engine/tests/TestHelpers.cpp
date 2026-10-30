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
        context = std::make_unique<EngineContext> (EngineContext::Options { .settingsFolder = tempDir() / "settings" });

    return *context;
}


model::Json callApi (ApiHandler& handler, ApiHandler::Client& client, const std::string& method, model::Json params)
{
    static int nextId = 1;
    const auto id = nextId++;
    std::optional<model::Json> response;
    auto previous = client.send;

    client.send = [&, previous] (const std::string& m)
    {
        auto json = model::Json::parse (m);

        if (json.value ("id", -1) == id)
            response = json;
        else if (previous)
            previous (m);
    };

    handler.handleMessage (model::Json { { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", params } }.dump(), client);

    const auto deadline = juce::Time::getMillisecondCounter() + 600000;

    while (! response && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

    client.send = previous;

    if (! response)
        throw std::runtime_error (method + " wasn't answered in time");

    return *response;
}

} // namespace gk::test

CATCH_REGISTER_LISTENER (gk::test::EngineShutdown)
