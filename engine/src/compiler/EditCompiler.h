#pragma once

#include <set>

#include "model/Project.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk::mixer { class FaderPlugin; class EffectMixBlend; }
namespace gk::sampler { class AudioChannelPlugin; class StackOutputPlugin; }

namespace gk
{

/** A Tracktion Edit built from a project, with the mapping back to model ids.

    Mapping:
    - each mixer insert becomes an audio track holding its effects (each between a tap and a
      blend that mix the dry signal back in), sends and fader;
      non-master inserts output into their target insert's track, master goes to the device
    - each channel becomes a hidden track with its instrument and fader, feeding its insert
      (a sampler only when it might play notes)
    - each pattern clip becomes one MIDI clip per channel that has notes in the pattern,
      with the pattern repeated to fill the clip
    - a channel's audio clips go on one hidden track per arrangement row they're in, holding
      the first layer's gain, pan and envelope, the channel's filter and a copy of the
      channel's fader, feeding the channel's insert
    - automation clips become automation curves on the targeted parameters
    - a multi-sampler's channel track is its bus (its output's trim, drive and ceiling, then the
      fader); each layer plays on tracks of its own (one for notes, one per arrangement row for
      audio clips, where a loop is laid back to back over the clip) with a fader for its mix,
      feeding the bus
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

    /** Channels' audio clips: their tracks, faders and shaping stages, one of each per playlist
        track the clips are on. */
    std::map<model::Id, std::vector<tracktion::AudioTrack*>> audioChannelTracks;
    std::map<model::Id, std::vector<mixer::FaderPlugin*>> audioChannelFaders;
    std::map<model::Id, std::vector<sampler::AudioChannelPlugin*>> audioChannelPlugins;

    /** The Tracktion clips that play each playlist clip (a pattern clip has one per channel). */
    std::map<model::Id, std::vector<tracktion::Clip*>> clips;

    /** Multi-samplers: each one's output (trim, drive, ceiling), and each layer's tracks that play
        notes (hits) and its faders (its mix, on every track the layer plays on), by channel and layer. */
    std::map<model::Id, sampler::StackOutputPlugin*> stackOutputs;
    std::map<model::Id, std::vector<std::pair<model::Id, tracktion::AudioTrack*>>> stackLayerTracks;
    std::map<model::Id, std::map<model::Id, std::vector<mixer::FaderPlugin*>>> stackLayerFaders;
    std::map<model::Id, std::map<model::Id, std::vector<sampler::AudioChannelPlugin*>>> stackAudioPlugins;

    /** Instruments as "channel:<id>" (a multi-sampler layer's as "channel:<id>/<layer>") and effects as "effect:<insert>/<effect>". */
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
    std::filesystem::path cacheDir {};

    /** The channel the keyboards play: its sampler is loaded even without notes. */
    std::optional<model::Id> liveChannel {};
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

/** Fills a MIDI clip with the notes one channel plays in a pattern clip: the pattern repeated to
    fill the clip from its offset, swung by the song. Whatever the clip held is replaced. */
/** shift moves every note later (earlier when negative), as a multi-sampler layer's offset does. */
void fillPatternClip (tracktion::MidiClip&, const model::Project&, const model::Pattern&, const model::PatternClip&, const model::Id& channel,
                      model::Ticks shift = 0);

/** A multi-sampler layer's offset in ticks at the song's tempo. */
model::Ticks stackShift (const model::Project&, const model::StackLayer&);

/** The swing applied to a note's start: notes on an off-beat 16th move later by up to half a 16th. */
model::Ticks swungStart (const model::Project&, model::Ticks start);

} // namespace gk
