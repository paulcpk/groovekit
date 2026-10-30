#include "PluginProcessor.h"

namespace chamber
{

const std::vector<gkp::Preset>& getPresets()
{
    static const std::vector<gkp::Preset> presets {
        { "Init", {} },

        { "Rooms: Small Room", { { "size", 0.15f }, { "decay", 0.5f }, { "predelay", 4 }, { "damping", 6000 }, { "diffusion", 0.8f }, { "early", 0.7f }, { "mix", 0.25f } } },
        { "Rooms: Studio Live Room", { { "size", 0.3f }, { "decay", 0.9f }, { "predelay", 8 }, { "damping", 8000 }, { "early", 0.6f }, { "modDepth", 0.2f }, { "mix", 0.3f } } },
        { "Rooms: Wood Room", { { "size", 0.25f }, { "decay", 0.7f }, { "damping", 3500 }, { "lowCut", 120 }, { "early", 0.8f }, { "diffusion", 0.9f }, { "mix", 0.3f } } },
        { "Rooms: Tiled Bathroom", { { "size", 0.2f }, { "decay", 1.4f }, { "damping", 14000 }, { "highCut", 18000 }, { "diffusion", 0.5f }, { "early", 0.9f }, { "modDepth", 0.1f }, { "mix", 0.3f } } },
        { "Rooms: Chamber", { { "size", 0.45f }, { "decay", 1.8f }, { "predelay", 12 }, { "damping", 7000 }, { "early", 0.5f }, { "mix", 0.35f } } },

        { "Halls: Concert Hall", { { "size", 0.7f }, { "decay", 2.8f }, { "predelay", 25 }, { "damping", 5500 }, { "early", 0.4f }, { "modDepth", 0.3f }, { "mix", 0.35f } } },
        { "Halls: Big Hall", { { "size", 0.9f }, { "decay", 4.5f }, { "predelay", 35 }, { "damping", 4500 }, { "lowCut", 100 }, { "early", 0.3f }, { "mix", 0.4f } } },
        { "Halls: Cathedral", { { "size", 1 }, { "decay", 9 }, { "predelay", 60 }, { "damping", 3000 }, { "lowCut", 90 }, { "highCut", 9000 }, { "diffusion", 0.9f }, { "early", 0.25f }, { "modDepth", 0.4f }, { "mix", 0.45f } } },
        { "Halls: Arena", { { "size", 1 }, { "decay", 6 }, { "predelay", 90 }, { "damping", 4000 }, { "early", 0.6f }, { "diffusion", 0.6f }, { "mix", 0.4f } } },
        { "Halls: Warm Hall", { { "size", 0.75f }, { "decay", 3.5f }, { "predelay", 20 }, { "damping", 2800 }, { "highCut", 7000 }, { "modDepth", 0.35f }, { "mix", 0.35f } } },

        { "Plates: Bright Plate", { { "size", 0.5f }, { "decay", 2.2f }, { "predelay", 0 }, { "damping", 14000 }, { "lowCut", 150 }, { "diffusion", 1 }, { "early", 0 }, { "modRate", 1.2f }, { "modDepth", 0.5f }, { "mix", 0.3f } } },
        { "Plates: Dark Plate", { { "size", 0.55f }, { "decay", 2.8f }, { "damping", 3500 }, { "highCut", 6000 }, { "lowCut", 120 }, { "diffusion", 1 }, { "early", 0 }, { "modDepth", 0.4f }, { "mix", 0.3f } } },
        { "Plates: Vocal Plate", { { "size", 0.45f }, { "decay", 1.9f }, { "predelay", 40 }, { "damping", 9000 }, { "lowCut", 200 }, { "diffusion", 1 }, { "early", 0.1f }, { "duck", 0.5f }, { "mix", 0.3f } } },
        { "Plates: Snare Plate", { { "size", 0.4f }, { "decay", 1.2f }, { "predelay", 15 }, { "damping", 10000 }, { "lowCut", 250 }, { "diffusion", 1 }, { "early", 0.2f }, { "mix", 0.35f } } },

        { "Techno: Dub Tail", { { "size", 0.8f }, { "decay", 5 }, { "predelay", 30 }, { "damping", 2500 }, { "lowCut", 150 }, { "highCut", 5000 }, { "modDepth", 0.5f }, { "duck", 0.4f }, { "mix", 0.5f } } },
        { "Techno: Kick Room", { { "size", 0.2f }, { "decay", 0.35f }, { "predelay", 0 }, { "damping", 4000 }, { "lowCut", 40 }, { "highCut", 3000 }, { "early", 0.9f }, { "width", 0.4f }, { "mix", 0.2f } } },
        { "Techno: Rumble Maker", { { "size", 0.9f }, { "decay", 1.6f }, { "predelay", 5 }, { "damping", 1200 }, { "lowCut", 30 }, { "highCut", 160 }, { "diffusion", 1 }, { "early", 0 }, { "width", 0.3f }, { "mix", 1 } } },
        { "Techno: Clap Splash", { { "size", 0.5f }, { "decay", 1.1f }, { "predelay", 10 }, { "damping", 9000 }, { "lowCut", 300 }, { "diffusion", 0.95f }, { "early", 0.5f }, { "mix", 0.45f } } },
        { "Techno: Hat Air", { { "size", 0.35f }, { "decay", 0.8f }, { "damping", 16000 }, { "lowCut", 2000 }, { "highCut", 18000 }, { "early", 0.3f }, { "mix", 0.25f } } },
        { "Techno: Stab Hall", { { "size", 0.7f }, { "decay", 2.4f }, { "predelay", 20 }, { "damping", 6000 }, { "lowCut", 250 }, { "duck", 0.6f }, { "mix", 0.4f } } },
        { "Techno: Techno Wash", { { "size", 1 }, { "decay", 12 }, { "predelay", 40 }, { "damping", 3000 }, { "lowCut", 200 }, { "highCut", 7000 }, { "modDepth", 0.6f }, { "duck", 0.7f }, { "mix", 0.5f } } },

        { "Ambient: Ambient Wash", { { "size", 1 }, { "decay", 16 }, { "predelay", 50 }, { "damping", 4000 }, { "lowCut", 120 }, { "diffusion", 1 }, { "early", 0.1f }, { "modRate", 0.3f }, { "modDepth", 0.7f }, { "mix", 0.6f } } },
        { "Ambient: Frozen", { { "size", 0.8f }, { "decay", 30 }, { "damping", 8000 }, { "freeze", 1 }, { "modDepth", 0.5f }, { "early", 0 }, { "mix", 0.7f } } },
        { "Ambient: Cloud", { { "size", 0.9f }, { "decay", 8 }, { "predelay", 80 }, { "damping", 5000 }, { "lowCut", 300 }, { "highCut", 10000 }, { "diffusion", 1 }, { "early", 0 }, { "modRate", 0.2f }, { "modDepth", 0.8f }, { "mix", 0.55f } } },
        { "Ambient: Deep Space", { { "size", 1 }, { "decay", 22 }, { "predelay", 120 }, { "damping", 2500 }, { "lowCut", 80 }, { "highCut", 5000 }, { "early", 0 }, { "modDepth", 0.4f }, { "mix", 0.5f } } },
        { "Ambient: Pad Glue", { { "size", 0.6f }, { "decay", 3 }, { "predelay", 15 }, { "damping", 7000 }, { "lowCut", 250 }, { "modDepth", 0.5f }, { "width", 1 }, { "mix", 0.3f } } },

        { "Special: Gated Short", { { "size", 0.9f }, { "decay", 0.3f }, { "predelay", 0 }, { "damping", 9000 }, { "diffusion", 1 }, { "early", 1 }, { "mix", 0.45f } } },
        { "Special: Ducked Vocal Space", { { "size", 0.65f }, { "decay", 2.6f }, { "predelay", 45 }, { "damping", 6000 }, { "lowCut", 220 }, { "duck", 0.85f }, { "mix", 0.4f } } },
        { "Special: Spring-ish", { { "size", 0.3f }, { "decay", 1.6f }, { "predelay", 0 }, { "damping", 6000 }, { "lowCut", 200 }, { "highCut", 6000 }, { "diffusion", 0.2f }, { "early", 0.3f }, { "modRate", 3 }, { "modDepth", 0.9f }, { "mix", 0.35f } } },
        { "Special: Mono Box", { { "size", 0.1f }, { "decay", 0.25f }, { "damping", 5000 }, { "early", 1 }, { "width", 0 }, { "mix", 0.3f } } },
        { "Special: Wide Shimmerless", { { "size", 0.85f }, { "decay", 6 }, { "damping", 12000 }, { "lowCut", 400 }, { "diffusion", 1 }, { "early", 0 }, { "modRate", 0.6f }, { "modDepth", 0.6f }, { "width", 1 }, { "mix", 0.5f } } },
    };

    return presets;
}

} // namespace chamber
