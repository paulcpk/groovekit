#include "PluginProcessor.h"

namespace groove3
{

const std::vector<gkp::Preset>& getPresets()
{
    // Ids as in Parameters.h, values in their own units (times in milliseconds; a volume decay of
    // 4000 holds). Choices by index: waveform 0 saw, 1 square; sweep speed 0 fast, 1 normal,
    // 2 slow; resonance range 0 normal, 1 high; muffler 0 off, 1 soft, 2 hard; sub octave 0 one
    // down, 1 two. What a preset leaves out takes its default (the unmodified machine's sound).
    static const std::vector<gkp::Preset> presets {
        { "Classic: Squelch", {} },
        { "Classic: Rubber Saw", { { "cutoff", 0.35f }, { "resonance", 0.55f }, { "envMod", 0.65f }, { "decay", 600 }, { "accent", 0.7f } } },
        { "Classic: Square Pluck", { { "waveform", 1 }, { "cutoff", 0.38f }, { "resonance", 0.5f }, { "envMod", 0.55f }, { "decay", 250 }, { "accent", 0.5f } } },
        { "Classic: Screamer", { { "cutoff", 0.5f }, { "resonance", 0.92f }, { "envMod", 0.7f }, { "decay", 900 }, { "accent", 0.85f } } },
        { "Classic: Dark Roll", { { "cutoff", 0.28f }, { "resonance", 0.6f }, { "envMod", 0.35f }, { "decay", 300 }, { "accent", 0.5f } } },
        { "Classic: Open Wide", { { "cutoff", 0.62f }, { "resonance", 0.78f }, { "envMod", 0.3f }, { "decay", 1500 } } },
        { "Classic: Square Acid", { { "waveform", 1 }, { "cutoff", 0.45f }, { "resonance", 0.8f }, { "envMod", 0.6f }, { "decay", 700 }, { "accent", 0.75f } } },

        { "Modded: Long Slide", { { "slideTime", 180 }, { "ampDecay", 4000 }, { "resonance", 0.75f }, { "cutoff", 0.4f } } },
        { "Modded: Accent Wow", { { "sweepSpeed", 2 }, { "resonanceRange", 1 }, { "accent", 0.9f }, { "accentDecay", 600 }, { "resonance", 0.55f } } },
        { "Modded: Fast Wup", { { "sweepSpeed", 0 }, { "accent", 0.8f }, { "accentDecay", 120 }, { "resonance", 0.75f } } },
        { "Modded: Singing Filter", { { "volume", -6 }, { "resonanceRange", 1 }, { "resonance", 0.95f }, { "tracking", 1 }, { "cutoff", 0.5f }, { "envMod", 0.2f } } },
        { "Modded: Soft Pad Bass", { { "softAttack", 25 }, { "ampDecay", 4000 }, { "decay", 2000 }, { "envMod", 0.25f }, { "resonance", 0.5f }, { "cutoff", 0.35f } } },
        { "Modded: FM Grit", { { "filterFm", 0.75f }, { "volume", -8 }, { "overdrive", 0.45f }, { "resonance", 0.7f }, { "cutoff", 0.45f } } },
        { "Modded: Overdrive", { { "overdrive", 0.6f }, { "muffler", 1 }, { "resonance", 0.7f } } },
        { "Modded: Muffled", { { "muffler", 2 }, { "overdrive", 0.5f }, { "cutoff", 0.4f }, { "resonance", 0.6f } } },
        { "Modded: Tracking Lead", { { "tracking", 1 }, { "cutoff", 0.55f }, { "resonance", 0.82f }, { "envMod", 0.4f }, { "decay", 800 } } },
        { "Modded: Short Blips", { { "ampDecay", 120 }, { "decay", 120 }, { "accentDecay", 80 }, { "resonance", 0.65f }, { "cutoff", 0.45f } } },

        { "Hard: Distorted Acid", { { "distortion", 0.55f }, { "resonance", 0.8f }, { "envMod", 0.6f }, { "decay", 500 }, { "accent", 0.8f } } },
        { "Hard: Warehouse Rage", { { "distortion", 0.8f }, { "overdrive", 0.5f }, { "resonance", 0.85f }, { "sweepSpeed", 0 }, { "accent", 0.9f }, { "cutoff", 0.45f } } },
        { "Hard: Square Fuzz", { { "waveform", 1 }, { "distortion", 0.7f }, { "muffler", 1 }, { "resonance", 0.75f } } },
        { "Hard: Feedback Scream", { { "resonanceRange", 1 }, { "resonance", 0.9f }, { "filterFm", 0.5f }, { "distortion", 0.5f } } },

        { "Bass: Sub Line", { { "sub", 0.6f }, { "cutoff", 0.3f }, { "resonance", 0.35f }, { "envMod", 0.3f }, { "decay", 300 }, { "accent", 0.4f }, { "ampDecay", 600 } } },
        { "Bass: Deep Square", { { "waveform", 1 }, { "sub", 0.5f }, { "subOctave", 1 }, { "cutoff", 0.25f }, { "resonance", 0.3f }, { "envMod", 0.25f } } },
        { "Bass: Rolling Techno", { { "sub", 0.4f }, { "cutoff", 0.33f }, { "resonance", 0.5f }, { "envMod", 0.45f }, { "decay", 200 }, { "ampDecay", 220 }, { "overdrive", 0.4f } } },
        { "Bass: Mellow", { { "softAttack", 8 }, { "cutoff", 0.3f }, { "resonance", 0.25f }, { "envMod", 0.2f }, { "decay", 800 } } },
    };

    return presets;
}

} // namespace groove3
