#pragma once

// ONNX Runtime's C++ API, set up by loadOnnxRuntime() from a library loaded at run time (it's
// downloaded with the listening models, not linked). Include it through this header only, so
// every file agrees on ORT_API_MANUAL_INIT.
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#undef ORT_API_MANUAL_INIT

#include <filesystem>

namespace gk::listening
{

/** Loads ONNX Runtime from the dylib at path, once per process (later calls do nothing).
    Throws std::runtime_error if it can't. */
void loadOnnxRuntime (const std::filesystem::path& dylib);

/** The process's ONNX Runtime environment, after loadOnnxRuntime(). */
Ort::Env& getOrtEnv();

/** A model on the CPU, using up to threads cores. */
Ort::Session openSession (const std::filesystem::path& model, int threads);

} // namespace gk::listening
