#include "Ort.h"

#include <dlfcn.h>
#include <mutex>

namespace gk::listening
{

namespace
{
    std::mutex loadMutex;
    bool loaded = false;
}

void loadOnnxRuntime (const std::filesystem::path& dylib)
{
    std::lock_guard lock (loadMutex);

    if (loaded)
        return;

    // Never unloaded: sessions and the environment live until the process ends.
    auto* handle = dlopen (dylib.c_str(), RTLD_NOW | RTLD_LOCAL);

    if (handle == nullptr)
        throw std::runtime_error ("Couldn't load ONNX Runtime from " + dylib.string() + ": " + dlerror());

    using GetApiBase = const OrtApiBase* (*)();
    auto getApiBase = reinterpret_cast<GetApiBase> (dlsym (handle, "OrtGetApiBase"));
    const auto* api = getApiBase != nullptr ? getApiBase()->GetApi (ORT_API_VERSION) : nullptr;

    if (api == nullptr)
        throw std::runtime_error ("ONNX Runtime at " + dylib.string() + " isn't the version GrooveKit needs");

    Ort::InitApi (api);
    loaded = true;
}

Ort::Env& getOrtEnv()
{
    static Ort::Env env (ORT_LOGGING_LEVEL_ERROR, "groovekit");
    return env;
}

Ort::Session openSession (const std::filesystem::path& model, int threads)
{
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads (threads);
    options.SetInterOpNumThreads (1);
    options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);

    // Idle workers sleep instead of spinning: they share the Mac with audio, and with each other
    // when the library and a style are heard at once.
    options.AddConfigEntry ("session.intra_op.allow_spinning", "0");
    options.AddConfigEntry ("session.inter_op.allow_spinning", "0");
    return Ort::Session (getOrtEnv(), model.c_str(), options);
}

} // namespace gk::listening
