#pragma once

#include "model/Project.h"

#include <exception>
#include <functional>
#include <memory>
#include <vector>

namespace gk
{

class EngineContext;

/** Work that takes long, as steps in order: some must run on the message thread (compiling an
    Edit, setting its tracks' mutes, applying a result to the session), the rest (rendering,
    measuring, hearing) may run on a pool thread while the message thread serves other requests.

    runNow() runs every step on the calling thread, for callers that block anyway (tests, the
    command line). runLater() runs the message-thread steps on the message thread and hands the
    others to the engine's job pool, then calls done (with the exception a step threw, if any).
    The steps' shared state lives in the Steps object, which a caller keeps alive until done. */
class Steps
{
public:
    using Step = std::function<void()>;
    using Done = std::function<void (std::exception_ptr error)>;

    /** A step for the message thread. */
    void here (Step step)       { steps.push_back ({ false, std::move (step) }); }

    /** A step any thread may run. */
    void pooled (Step step)     { steps.push_back ({ true, std::move (step) }); }

    /** Whether any step is left for the pool: without one, runLater answers at once. */
    bool hasPooledSteps() const;

    void runNow();

    /** Message thread only; the caller keeps this object alive until done is called. */
    void runLater (EngineContext&, Done done);

    /** What the steps produced. */
    model::Json result;

private:
    struct Item
    {
        bool pooled;
        Step run;
    };

    std::vector<Item> steps;
    size_t next = 0;

    void continueLater (EngineContext&, Done done);
};

} // namespace gk
