#pragma once

#include "model/Project.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk
{

/** A Tracktion Edit built from a project, with the mapping back to model ids.

    Mapping:
    - each mixer insert becomes an audio track holding its effects, sends and fader;
      non-master inserts output into their target insert's track, master goes to the device
    - each channel becomes a hidden track with its instrument and fader, feeding its insert
    - each pattern clip becomes one MIDI clip per channel that has notes in the pattern,
      with the pattern repeated to fill the clip
    - audio clips go on one hidden track per target insert
    - automation clips become automation curves on the targeted parameters
*/
struct CompiledEdit
{
    std::unique_ptr<tracktion::Edit> edit;

    std::map<model::Id, tracktion::AudioTrack*> insertTracks;
    std::map<model::Id, tracktion::AudioTrack*> channelTracks;
};

struct CompileOptions
{
    /** Relative sample paths resolve against this folder. */
    std::filesystem::path projectDir;
    tracktion::Edit::EditRole role = tracktion::Edit::forRendering;
};

/** Validates the project and builds the Edit. Throws std::runtime_error on invalid input or missing files. */
CompiledEdit compileProject (tracktion::Engine&, const model::Project&, const CompileOptions&);

} // namespace gk
