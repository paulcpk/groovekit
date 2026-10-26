#pragma once

#include "compiler/EditCompiler.h"
#include "render/Render.h"

namespace gk::analysis
{

/** Renders the mix and one stem per source, then reports how they fit together.

    Sources are channels, plus one per insert that audio clips play into. Each stem is the
    mix with every other source muted, so it includes the effects it passes through. A pair
    of sources masks each other in a band when both contribute at least a quarter of the
    mix's energy there.

    The result has "mix" (the detailed analysis), "stems" (loudness, peak and band energy per
    source) and "masking" (pairs with the bands they share). Files go to workDir and are deleted.
*/
model::Json analyseMix (tracktion::Engine&, const model::Project&, const CompileOptions&,
                        const RenderOptions&, const std::filesystem::path& workDir);

} // namespace gk::analysis
