#pragma once

#include "Session.h"
#include "Steps.h"
#include "render/Render.h"

namespace tracktion { inline namespace engine { class Engine; } }

namespace gk
{

struct MasterOptions
{
    double targetLufs = -14;
    double ceilingDb = -1;
    std::optional<double> releaseMs;
    RenderOptions render;           // the range measured; the whole song by default
    ExecuteOptions execute;
};

/** Sets the master limiter so the song reaches a target integrated loudness.

    Adds a limiter (id "master-limiter") at the end of the master insert if there isn't one,
    then renders, measures and adjusts its input gain until the loudness is within 0.2 LU
    (at most eight renders). The final setting is applied as one undoable change.
    Returns the gain, the measured loudness and true peak, and how many renders it took.
*/
model::Json masterToLoudness (tracktion::Engine&, Session&, const MasterOptions&);

/** The same as steps: the song compiled, and the limiter setting applied, on the message thread;
    the render and the limiter passes pooled. (When effects follow the limiter on master, every
    pass renders the song and all of it stays on the message thread.) */
std::shared_ptr<Steps> masterToLoudnessSteps (tracktion::Engine&, Session&, const MasterOptions&);

inline constexpr const char* masterLimiterId = "master-limiter";

} // namespace gk
