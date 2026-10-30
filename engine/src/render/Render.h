#pragma once

#include "compiler/EditCompiler.h"

namespace gk
{

struct RenderOptions
{
    double sampleRate = 48000;
    int bitDepth = 24;
    int blockSize = 512;

    /** Range to render in ticks. Defaults to the whole song (0 to the end of the last clip). */
    std::optional<model::Ticks> start, end;

    /** Extra seconds after the end, for release tails, reverbs and delays. */
    double tailSeconds = 0;

    /** Hears how far the render is (0 to 1), on the rendering thread, every percent or so. */
    std::function<void (float)> progress;
};

/** Renders a compiled project to a stereo WAV file, blocking; plugin latency is compensated. Throws on failure. */
void renderToWav (const CompiledEdit&, const model::Project&, const std::filesystem::path& output, const RenderOptions&);

} // namespace gk
