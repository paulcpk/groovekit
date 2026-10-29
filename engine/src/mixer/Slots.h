#pragma once

#include "session/Transaction.h"
#include "model/Project.h"

namespace gk::mixer
{

/** Where a part sits in the mix: what its insert's slot EQ (the effect "slot", first on the
    insert) cuts away so the parts don't cover each other. Each part has a range: lows (kick,
    rumble, sub bass), low mids (percussion loops), low and upper mids (synths), upper mids
    (claps, effects), upper mids and highs (hats). Cuts are 24 dB/octave, a steep high cut 48.
    In the EQ: band 1 the low cut, band 5 the second stage of a steep high cut, band 6 the top
    shelf, band 7 the high cut; bands 2 to 4 are free for notches and peaks. */
struct Slot
{
    double lowCutHz = 0;            // 0: none
    double highCutHz = 0;           // 0: none
    bool steep = false;             // the high cut twice
    double topShelfDb = 0;          // a high shelf taming the top, 0 for none
    double topShelfHz = 10000;
    std::string from = "default";   // or "references", when a style's stems moved the cuts

    model::Json toJson() const;
    bool operator== (const Slot&) const = default;
};

/** Each part's slot for techno and house, before any style's references. */
Slot defaultSlot (model::ChannelPart);

/** The part's range, in words: "lows", "low mids", "low and upper mids", "upper mids", "upper mids and highs". */
const char* rangeOf (model::ChannelPart);

/** The slot moved to where a style's references keep that part: stem is the median spectral
    balance (analysis::bandEdges) of their bass stem for bass, their music stem for synths and
    vocals. Other parts, and an empty stem, keep the slot. Each cut stays within a range that
    keeps the part clear of its neighbours. */
Slot fitToReferences (Slot, model::ChannelPart, const std::vector<float>& stem);

/** What a channel most likely plays: its name says it when it can, then its first sample's
    category, whether it loops and how much of it is below 250 Hz (lowShare, 0 to 1). */
model::ChannelPart guessPart (const std::string& name, const std::string& category, bool loop, float lowShare);

/** The slot EQ as an effect. */
model::BuiltinEffect slotEffect (const Slot&);

/** The slot the channel's insert has (the effect "slot", an EQ), if it has one. */
std::optional<Slot> readSlot (const model::Project&, const model::Channel&);

/** Commands that give the channel its own insert when it shares one or plays into master (a new
    insert named after it, feeding where the channel did), put the slot first on it (or update
    the one there) and set the channel's part. */
std::vector<model::Command> slotCommands (const model::Project&, const model::Id& channel, model::ChannelPart, const Slot&);

/** A part from its name ("kick", "synth"...). */
std::optional<model::ChannelPart> partFromName (const std::string&);

/** Whether a part holds the low end (kick, rumble, drums, bass). */
bool isLowPart (model::ChannelPart);

} // namespace gk::mixer
