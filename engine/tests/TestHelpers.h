#pragma once

#include "EngineContext.h"
#include "api/ApiHandler.h"

#include <juce_core/juce_core.h>

#include <filesystem>
#include <functional>
#include <unistd.h>

namespace gk::test
{

/** The repository root, from GK_REPO_ROOT (set by CTest) or the current directory. */
inline std::filesystem::path repoRoot()
{
    if (auto env = std::getenv ("GK_REPO_ROOT"))
        return env;

    return std::filesystem::current_path();
}

inline std::filesystem::path fixture (const std::string& name)
{
    return repoRoot() / "tests" / "fixtures" / "samples" / name;
}

/** A per-process scratch folder for rendered files. */
inline std::filesystem::path tempDir()
{
    static const auto dir = []
    {
        auto d = std::filesystem::temp_directory_path() / ("groovekit-tests-" + std::to_string (getpid()));
        std::filesystem::create_directories (d);
        return d;
    }();

    return dir;
}

/** One engine shared by every test in the process. */
EngineContext& getEngineContext();

/** Runs before the shared engine is destroyed, for objects that need the message thread to go away cleanly. */
void atShutdown (std::function<void()>);

inline tracktion::Engine& getEngine()
{
    return getEngineContext().getEngine();
}

/** Sends one request to an ApiHandler and waits for its response: methods that render or hear
    answer later, from a pool thread, through the message loop. */
model::Json callApi (ApiHandler&, ApiHandler::Client&, const std::string& method, model::Json params = model::Json::object());

} // namespace gk::test
