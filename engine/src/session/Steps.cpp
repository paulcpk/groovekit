#include "Steps.h"

#include "EngineContext.h"

namespace gk
{

bool Steps::hasPooledSteps() const
{
    for (size_t i = next; i < steps.size(); ++i)
        if (steps[i].pooled)
            return true;

    return false;
}

void Steps::runNow()
{
    while (next < steps.size())
        steps[next++].run();
}

void Steps::runLater (EngineContext& context, Done done)
{
    try
    {
        continueLater (context, std::move (done));
    }
    catch (...)
    {
        done (std::current_exception());
    }
}

void Steps::continueLater (EngineContext& context, Done done)
{
    // Message-thread steps run here, in a row; the first pooled step goes to the pool, and its
    // completion (back on the message thread) continues from the step after it.
    while (next < steps.size() && ! steps[next].pooled)
        steps[next++].run();

    if (next >= steps.size())
    {
        done (nullptr);
        return;
    }

    auto& step = steps[next++];

    context.runJob ([run = step.run] () -> std::shared_ptr<void>
    {
        run();
        return nullptr;
    },
    [this, &context, done] (std::shared_ptr<void>, std::exception_ptr error)
    {
        if (error)
        {
            done (error);
            return;
        }

        try
        {
            continueLater (context, done);
        }
        catch (...)
        {
            done (std::current_exception());
        }
    });
}

} // namespace gk
