#pragma once

#include "model/Project.h"

namespace gk
{

/** A compact overview of a project for agents: positions in bars (1-based) next to ticks,
    note counts per channel instead of notes, and effect chains by kind.
    The full document is available for drilling down.
*/
model::Json summarizeProject (const model::Project&);

} // namespace gk
