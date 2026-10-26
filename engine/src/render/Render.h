#pragma once

#include "model/Project.h"

#include <tracktion_engine/tracktion_engine.h>

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
};

/** Renders an Edit compiled from the project to a stereo WAV file, blocking. Throws on failure. */
void renderToWav (tracktion::Edit&, const model::Project&, const std::filesystem::path& output, const RenderOptions&);

} // namespace gk
