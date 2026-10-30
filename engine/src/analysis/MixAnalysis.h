#pragma once

#include "compiler/EditCompiler.h"
#include "render/Render.h"
#include "session/Steps.h"

#include <set>

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

/** The same as steps (the Edit compiled and the stems' mutes set on the message thread, the
    renders pooled), with the result in the steps' result. */
std::shared_ptr<Steps> analyseMixSteps (tracktion::Engine&, const model::Project&, const CompileOptions&,
                                        const RenderOptions&, const std::filesystem::path& workDir);

/** What a combination check knows about the song besides its sound. */
struct MixCheckContext
{
    std::map<std::string, model::ChannelPart> parts;     // each channel's part
    std::set<std::string> ducked;                        // channels something ducks (a keyed compressor or a duck)
    std::vector<float> referenceBalance;                 // the style's spectral balance, if it has references
    double barSeconds = 2;
};

/** Renders the mix and each channel, and finds what goes wrong between the parts, worst first:
    parts outside the low end that carry it, low parts sharing it with nothing ducking, parts
    covering each other in the same bands, the mix's balance against the references' (the top
    above all), and highs that never move. Each problem has "severity" (0-10), "kind",
    "channels", "what" and "fix". Also gives each channel's level and where its energy sits. */
model::Json checkMix (tracktion::Engine&, const model::Project&, const CompileOptions&, const RenderOptions&,
                      const std::filesystem::path& workDir, const MixCheckContext&);

std::shared_ptr<Steps> checkMixSteps (tracktion::Engine&, const model::Project&, const CompileOptions&, const RenderOptions&,
                                      const std::filesystem::path& workDir, const MixCheckContext&);

/** Reads the arrangement from the song itself (no rendering): which parts play in each bar, and
    where too many play at once or come in together. A part is a channel heard in a bar (its
    notes or its audio clips); beds don't count (fx parts playing through most of the song).
    Problems, as in checkMix: "tooManyParts" (more than maxParts at once: when something comes,
    something has to go) and "entersTogether" (two or more parts that weren't playing in the 16
    bars before come in on the same bar: one at a time). Also "bars": the parts per bar. */
model::Json checkArrangement (const model::Project&, const std::map<std::string, model::ChannelPart>& parts, int maxParts = 6);

/** Renders the channel alone and the rest of the song without it (the range in options), kept
    beside file as "-alone" and "-rest" so they can play in sync with the rest faded under the
    channel, and the two together to file. Says how the channel sits with the rest: "fit" 0-100 (100 when the bands it lives in are its own), the
    bands where it covers the rest ("covers"), its share of each band and its level against the
    mix. For trying candidate sounds in place before choosing one. */
model::Json placeInMix (tracktion::Engine&, const model::Project&, const model::Id& channel, const CompileOptions&,
                        const RenderOptions&, const std::filesystem::path& file);

/** placeInMix in two halves: the projects and their Edits, made on the message thread, and the
    renders and measurements, which any thread may do. */
struct Placement
{
    model::Project alone, rest;
    std::optional<CompiledEdit> aloneEdit, restEdit;
    std::filesystem::path file, aloneFile, restFile;
};

std::shared_ptr<Placement> preparePlacement (tracktion::Engine&, const model::Project&, const model::Id& channel,
                                             const CompileOptions&, const std::filesystem::path& file);
model::Json renderPlacement (Placement&, const RenderOptions&);

} // namespace gk::analysis
