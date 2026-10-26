#pragma once

#include <set>

#include "model/Project.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk::mixer { class FaderPlugin; class EffectMixBlend; }
namespace gk::sampler { class AudioChannelPlugin; }

namespace gk
{

/** A Tracktion Edit built from a project, with the mapping back to model ids.

    Mapping:
    - each mixer insert becomes an audio track holding its effects (each between a tap and a
      blend that mix the dry signal back in), sends and fader;
      non-master inserts output into their target insert's track, master goes to the device
    - each channel becomes a hidden track with its instrument and fader, feeding its insert
    - each pattern clip becomes one MIDI clip per channel that has notes in the pattern,
      with the pattern repeated to fill the clip
    - an audio channel's clips go on one hidden track per playlist track they're on, holding
      the channel's envelope and filter, the playlist track's fader and the channel's fader,
      feeding the track's insert if it has one, else the channel's
    - automation clips become automation curves on the targeted parameters
*/
struct CompiledEdit
{
    CompiledEdit() = default;
    CompiledEdit (CompiledEdit&&) = default;
    CompiledEdit& operator= (CompiledEdit&&);

    /** The Edit goes to the EditGraveyard rather than being deleted on the spot. */
    ~CompiledEdit();

    std::unique_ptr<tracktion::Edit> edit;

    std::map<model::Id, tracktion::AudioTrack*> insertTracks;
    std::map<model::Id, tracktion::AudioTrack*> channelTracks;

    /** The volume and pan stage of each insert and channel, which also meters them. */
    std::map<model::Id, mixer::FaderPlugin*> insertFaders, channelFaders;

    /** The volume and pan stages of each playlist track's audio clips (one per audio channel on it). */
    std::map<model::Id, std::vector<mixer::FaderPlugin*>> trackFaders;

    /** Audio channels: their tracks, faders and envelope stages, one of each per playlist track
        their clips are on. */
    std::map<model::Id, std::vector<tracktion::AudioTrack*>> audioChannelTracks;
    std::map<model::Id, std::vector<mixer::FaderPlugin*>> audioChannelFaders;
    std::map<model::Id, std::vector<sampler::AudioChannelPlugin*>> audioChannelPlugins;

    /** The Tracktion clips that play each playlist clip (a pattern clip has one per channel). */
    std::map<model::Id, std::vector<tracktion::Clip*>> clips;

    /** Instruments as "channel:<id>" and effects as "effect:<insert>/<effect>". */
    std::map<std::string, tracktion::Plugin*> plugins;

    /** The mix stage after each effect, as "<insert>/<effect>". */
    std::map<std::string, mixer::EffectMixBlend*> effectMixes;

    /** Sends as "<insert>/<send>". */
    std::map<std::string, tracktion::AuxSendPlugin*> sends;

    /** Plugin latency on the longest path to the output. Tracktion aligns every path to it and
        removes it from offline renders; live playback (and recording alignment) will need it. */
    double outputLatencySeconds = 0;
};

struct CompileOptions
{
    /** Relative sample paths resolve against this folder. */
    std::filesystem::path projectDir;
    tracktion::Edit::EditRole role = tracktion::Edit::forRendering;

    /** Where time-stretched copies of audio clips are kept. Empty means the default. */
    std::filesystem::path cacheDir;
};

/** Which inserts are heard: all of them, or with some soloed, those with the inserts feeding them
    and the ones they feed (by output or send). */
std::set<model::Id> audibleInserts (const model::Project&);

/** Whether a channel is heard: not muted, and soloed if any channel is. */
bool isChannelAudible (const model::Project&, const model::Channel&);

/** Whether a playlist clip is heard: it and its track not muted, and its track soloed if any is. */
bool isClipAudible (const model::Project&, const model::Clip&);

/** Validates the project and builds the Edit. Throws std::runtime_error on invalid input or missing files. */
CompiledEdit compileProject (tracktion::Engine&, const model::Project&, const CompileOptions&);

} // namespace gk
