#include "PluginProcessor.h"

namespace echo
{

// Divisions: 0 1/32, 1 1/16T, 2 1/16, 3 1/16., 4 1/8T, 5 1/8, 6 1/8., 7 1/4T, 8 1/4, 9 1/4., 10 1/2T,
// 11 1/2, 12 1/2., 13 1/1, 14 2/1. Modes: 0 stereo, 1 ping-pong, 2 dual.
const std::vector<gkp::Preset>& getPresets()
{
    static const std::vector<gkp::Preset> presets {
        { "Init", {} },

        { "Timed: Eighth", { { "division", 5 }, { "feedback", 0.4f }, { "highCut", 7000 }, { "mix", 0.3f } } },
        { "Timed: Dotted Eighth", { { "division", 6 }, { "feedback", 0.45f }, { "highCut", 6000 }, { "mix", 0.3f } } },
        { "Timed: Quarter", { { "division", 8 }, { "feedback", 0.5f }, { "highCut", 5000 }, { "mix", 0.28f } } },
        { "Timed: Triplet", { { "division", 4 }, { "feedback", 0.4f }, { "highCut", 6500 }, { "mix", 0.28f } } },
        { "Timed: Sixteenth Ratchet", { { "division", 2 }, { "feedback", 0.6f }, { "highCut", 9000 }, { "lowCut", 300 }, { "mix", 0.35f } } },
        { "Timed: Half Bar", { { "division", 11 }, { "feedback", 0.55f }, { "highCut", 4500 }, { "mix", 0.3f } } },

        { "Ping-pong: Wide Ping-pong", { { "mode", 1 }, { "division", 5 }, { "feedback", 0.5f }, { "highCut", 7000 }, { "width", 1 }, { "mix", 0.3f } } },
        { "Ping-pong: Dotted Ping-pong", { { "mode", 1 }, { "division", 6 }, { "feedback", 0.55f }, { "highCut", 5500 }, { "mix", 0.3f } } },
        { "Ping-pong: Sixteenth Bounce", { { "mode", 1 }, { "division", 2 }, { "feedback", 0.5f }, { "highCut", 10000 }, { "lowCut", 250 }, { "mix", 0.3f } } },
        { "Ping-pong: Slow Bounce", { { "mode", 1 }, { "division", 9 }, { "feedback", 0.6f }, { "highCut", 4000 }, { "saturation", 0.3f }, { "mix", 0.35f } } },

        { "Tape: Tape Echo", { { "division", 6 }, { "feedback", 0.5f }, { "highCut", 3500 }, { "lowCut", 150 }, { "saturation", 0.5f }, { "wow", 0.35f }, { "wowRate", 0.6f }, { "mix", 0.3f } } },
        { "Tape: Dark Tape", { { "division", 5 }, { "feedback", 0.55f }, { "highCut", 2200 }, { "lowCut", 200 }, { "saturation", 0.7f }, { "wow", 0.3f }, { "mix", 0.3f } } },
        { "Tape: Space Echo", { { "mode", 2 }, { "division", 5 }, { "ratio", 1.5f }, { "feedback", 0.6f }, { "highCut", 4000 }, { "saturation", 0.5f }, { "wow", 0.4f }, { "wowRate", 0.35f }, { "mix", 0.35f } } },
        { "Tape: Wobbly Tape", { { "sync", 0 }, { "timeMs", 420 }, { "feedback", 0.5f }, { "highCut", 3000 }, { "saturation", 0.6f }, { "wow", 0.9f }, { "wowRate", 1.2f }, { "mix", 0.3f } } },
        { "Tape: Worn Cassette", { { "sync", 0 }, { "timeMs", 310 }, { "feedback", 0.45f }, { "highCut", 2500 }, { "lowCut", 300 }, { "saturation", 0.8f }, { "wow", 0.6f }, { "wowRate", 2 }, { "mix", 0.3f } } },

        { "Dub: Dub Delay", { { "division", 6 }, { "feedback", 0.7f }, { "highCut", 3000 }, { "lowCut", 200 }, { "saturation", 0.4f }, { "diffusion", 0.3f }, { "mix", 0.4f } } },
        { "Dub: Siren Tail", { { "mode", 1 }, { "division", 8 }, { "feedback", 0.8f }, { "highCut", 2500 }, { "lowCut", 400 }, { "saturation", 0.6f }, { "wow", 0.4f }, { "mix", 0.45f } } },
        { "Dub: Infinite Dub", { { "division", 5 }, { "feedback", 0.95f }, { "highCut", 2000 }, { "lowCut", 300 }, { "saturation", 0.7f }, { "diffusion", 0.5f }, { "mix", 0.5f } } },
        { "Dub: Frozen Loop", { { "division", 8 }, { "freeze", 1 }, { "highCut", 4000 }, { "saturation", 0.3f }, { "mix", 0.6f } } },

        { "Techno: Stab Echo", { { "division", 6 }, { "feedback", 0.35f }, { "highCut", 5000 }, { "lowCut", 400 }, { "duck", 0.5f }, { "mix", 0.3f } } },
        { "Techno: Hat Echo", { { "division", 2 }, { "feedback", 0.4f }, { "highCut", 12000 }, { "lowCut", 2000 }, { "width", 1 }, { "mix", 0.25f } } },
        { "Techno: Clap Echo", { { "mode", 1 }, { "division", 3 }, { "feedback", 0.3f }, { "highCut", 6000 }, { "lowCut", 500 }, { "mix", 0.3f } } },
        { "Techno: Rumble Echo", { { "division", 5 }, { "feedback", 0.65f }, { "highCut", 180 }, { "lowCut", 30 }, { "saturation", 0.5f }, { "width", 0.2f }, { "mix", 0.6f } } },
        { "Techno: Ducked Echo", { { "division", 6 }, { "feedback", 0.55f }, { "highCut", 4500 }, { "lowCut", 250 }, { "duck", 0.9f }, { "mix", 0.4f } } },
        { "Techno: Industrial Smear", { { "mode", 2 }, { "division", 4 }, { "ratio", 0.75f }, { "feedback", 0.7f }, { "highCut", 3500 }, { "saturation", 0.9f }, { "diffusion", 0.8f }, { "mix", 0.4f } } },

        { "Ambient: Cloud Delay", { { "division", 9 }, { "feedback", 0.75f }, { "highCut", 5000 }, { "lowCut", 200 }, { "diffusion", 1 }, { "wow", 0.3f }, { "wowRate", 0.2f }, { "mix", 0.45f } } },
        { "Ambient: Smeared", { { "mode", 1 }, { "division", 6 }, { "feedback", 0.8f }, { "highCut", 3500 }, { "diffusion", 1 }, { "saturation", 0.3f }, { "mix", 0.5f } } },
        { "Ambient: Pad Echo", { { "division", 11 }, { "feedback", 0.6f }, { "highCut", 4000 }, { "lowCut", 150 }, { "diffusion", 0.6f }, { "wow", 0.2f }, { "mix", 0.35f } } },
        { "Ambient: Long Wash", { { "division", 13 }, { "feedback", 0.85f }, { "highCut", 3000 }, { "lowCut", 250 }, { "diffusion", 1 }, { "wow", 0.3f }, { "mix", 0.5f } } },

        { "Rhythmic: Dual 3 to 2", { { "mode", 2 }, { "division", 5 }, { "ratio", 1.5f }, { "feedback", 0.5f }, { "highCut", 6000 }, { "mix", 0.3f } } },
        { "Rhythmic: Dual 4 to 3", { { "mode", 2 }, { "division", 5 }, { "ratio", 0.75f }, { "feedback", 0.5f }, { "highCut", 6000 }, { "mix", 0.3f } } },
        { "Rhythmic: Short Slapback", { { "sync", 0 }, { "timeMs", 95 }, { "feedback", 0.1f }, { "highCut", 8000 }, { "mix", 0.3f } } },
        { "Rhythmic: Doubler", { { "sync", 0 }, { "timeMs", 28 }, { "feedback", 0 }, { "highCut", 12000 }, { "wow", 0.2f }, { "wowRate", 0.8f }, { "mix", 0.4f } } },
        { "Rhythmic: Haas Wide", { { "mode", 2 }, { "sync", 0 }, { "timeMs", 1 }, { "ratio", 2 }, { "feedback", 0 }, { "highCut", 16000 }, { "mix", 0.5f } } },
    };

    return presets;
}

} // namespace echo
