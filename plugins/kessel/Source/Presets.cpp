#include "PluginProcessor.h"

namespace kessel
{

// Ids as in Parameters.h. Click types: 0 noise, 1 tok, 2 snap, 3 zap. Body shapes: 0 sine,
// 1 triangle, 2 soft saw, 3 square. Drive types: 0 warm, 1 hard, 2 fold, 3 fuzz, 4 crush. Tune is
// the tail's note (29 = F1), times in milliseconds, frequencies in hertz, drive and crunch in dB.
const std::vector<gkp::Preset>& getPresets()
{
    static const std::vector<gkp::Preset> presets {
        { "Init", {} },

        //==================================================================== Hardstyle
        { "Hardstyle: Euphoric Punch", { { "tune", 31 }, { "pitchStart", 40 }, { "pitchTime", 24 }, { "pitchCurve", 0.7f }, { "hold", 170 }, { "decay", 280 },
            { "drive", 26 }, { "crunch", 14 }, { "crunchFreq", 1400 }, { "tone", 7000 }, { "sub", 0.35f }, { "clickType", 1 }, { "clickLevel", 0.4f }, { "clickTone", 3200 },
            { "compress", 0.4f }, { "clip", 0.4f } } },
        { "Hardstyle: Raw Screech", { { "tune", 30 }, { "pitchStart", 46 }, { "pitchTime", 20 }, { "pitchCurve", 0.8f }, { "glide", -3 }, { "hold", 220 }, { "decay", 320 },
            { "driveType", 3 }, { "drive", 36 }, { "crunch", 18 }, { "crunchFreq", 2200 }, { "tone", 9000 }, { "sub", 0.3f }, { "clickType", 2 }, { "clickLevel", 0.35f },
            { "clickTone", 5000 }, { "compress", 0.5f }, { "clip", 0.55f } } },
        { "Hardstyle: Saw Tail", { { "tune", 31 }, { "bodyShape", 2 }, { "pitchStart", 36 }, { "pitchTime", 28 }, { "pitchCurve", 0.65f }, { "hold", 200 }, { "decay", 300 },
            { "driveType", 2 }, { "drive", 22 }, { "crunch", 10 }, { "crunchFreq", 1100 }, { "tone", 6500 }, { "sub", 0.4f }, { "clickLevel", 0.3f }, { "compress", 0.45f },
            { "clip", 0.45f } } },
        { "Hardstyle: Tok Kick", { { "tune", 32 }, { "pitchStart", 34 }, { "pitchTime", 22 }, { "pitchCurve", 0.75f }, { "hold", 150 }, { "decay", 260 },
            { "drive", 30 }, { "crunch", 20 }, { "crunchFreq", 900 }, { "tone", 5500 }, { "sub", 0.3f }, { "clickType", 1 }, { "clickLevel", 0.55f }, { "clickTone", 2400 },
            { "clickDecay", 14 }, { "compress", 0.35f }, { "clip", 0.5f } } },
        { "Hardstyle: Classic Gated", { { "tune", 30 }, { "pitchStart", 42 }, { "pitchTime", 30 }, { "pitchCurve", 0.55f }, { "hold", 240 }, { "decay", 120 },
            { "driveType", 1 }, { "drive", 24 }, { "crunch", 12 }, { "crunchFreq", 1600 }, { "tone", 8000 }, { "sub", 0.25f }, { "clickLevel", 0.3f }, { "compress", 0.3f },
            { "clip", 0.35f } } },
        { "Hardstyle: Glide Down", { { "tune", 33 }, { "pitchStart", 44 }, { "pitchTime", 26 }, { "pitchCurve", 0.7f }, { "glide", -7 }, { "hold", 260 }, { "decay", 340 },
            { "drive", 28 }, { "crunch", 15 }, { "crunchFreq", 1300 }, { "tone", 7500 }, { "sub", 0.3f }, { "clickType", 1 }, { "clickLevel", 0.4f }, { "compress", 0.4f },
            { "clip", 0.45f } } },

        //==================================================================== Hardcore
        { "Hardcore: Rotterdam Gabber", { { "tune", 34 }, { "pitchStart", 30 }, { "pitchTime", 30 }, { "pitchCurve", 0.5f }, { "hold", 100 }, { "decay", 230 },
            { "drive", 40 }, { "crunch", 10 }, { "crunchFreq", 600 }, { "tone", 5000 }, { "sub", 0.2f }, { "clickType", 0 }, { "clickLevel", 0.4f }, { "clickTone", 4000 },
            { "compress", 0.5f }, { "clip", 0.6f } } },
        { "Hardcore: Mainstream Pitch", { { "tune", 35 }, { "pitchStart", 38 }, { "pitchTime", 20 }, { "pitchCurve", 0.85f }, { "hold", 140 }, { "decay", 220 },
            { "driveType", 1 }, { "drive", 34 }, { "crunch", 16 }, { "crunchFreq", 1200 }, { "tone", 7000 }, { "sub", 0.25f }, { "clickType", 1 }, { "clickLevel", 0.45f },
            { "compress", 0.45f }, { "clip", 0.6f } } },
        { "Hardcore: Industrial Crush", { { "tune", 33 }, { "pitchStart", 34 }, { "pitchTime", 34 }, { "pitchCurve", 0.4f }, { "hold", 90 }, { "decay", 260 },
            { "driveType", 4 }, { "drive", 30 }, { "crunch", 8 }, { "crunchFreq", 700 }, { "tone", 6000 }, { "sub", 0.35f }, { "clickType", 0 }, { "clickLevel", 0.5f },
            { "clickTone", 3000 }, { "compress", 0.5f }, { "clip", 0.5f } } },
        { "Hardcore: Frenchcore Punch", { { "tune", 36 }, { "pitchStart", 36 }, { "pitchTime", 16 }, { "pitchCurve", 0.8f }, { "hold", 70 }, { "decay", 160 },
            { "driveType", 3 }, { "drive", 38 }, { "crunch", 16 }, { "crunchFreq", 1800 }, { "tone", 8000 }, { "sub", 0.2f }, { "clickType", 2 }, { "clickLevel", 0.5f },
            { "clickTone", 6000 }, { "compress", 0.55f }, { "clip", 0.7f } } },
        { "Hardcore: Uptempo Terror", { { "tune", 37 }, { "pitchStart", 40 }, { "pitchTime", 12 }, { "pitchCurve", 0.9f }, { "hold", 50 }, { "decay", 120 },
            { "driveType", 1 }, { "drive", 44 }, { "crunch", 18 }, { "crunchFreq", 2000 }, { "tone", 9000 }, { "sub", 0.15f }, { "clickType", 2 }, { "clickLevel", 0.6f },
            { "clickTone", 7000 }, { "compress", 0.6f }, { "clip", 0.75f } } },
        { "Hardcore: Early Boom", { { "tune", 31 }, { "pitchStart", 28 }, { "pitchTime", 40 }, { "pitchCurve", 0.4f }, { "hold", 60 }, { "decay", 420 },
            { "drive", 32 }, { "crunch", 6 }, { "crunchFreq", 500 }, { "tone", 4000 }, { "sub", 0.4f }, { "clickType", 0 }, { "clickLevel", 0.35f }, { "compress", 0.4f },
            { "clip", 0.5f } } },

        //==================================================================== Hard techno
        { "Hard Techno: Rumble Kick", { { "tune", 30 }, { "pitchStart", 36 }, { "pitchTime", 48 }, { "pitchCurve", 0.45f }, { "hold", 110 }, { "decay", 380 },
            { "drive", 15 }, { "crunch", 4 }, { "tone", 4000 }, { "sub", 0.3f }, { "clickLevel", 0.15f }, { "rumble", 0.65f }, { "rumbleSize", 2.0f }, { "rumbleTone", 130 },
            { "rumbleDrive", 0.45f }, { "rumbleDuck", 0.9f }, { "compress", 0.35f }, { "clip", 0.3f } } },
        { "Hard Techno: Schranz Hammer", { { "tune", 31 }, { "pitchStart", 34 }, { "pitchTime", 42 }, { "pitchCurve", 0.5f }, { "hold", 100 }, { "decay", 320 },
            { "driveType", 1 }, { "drive", 22 }, { "crunch", 5 }, { "crunchFreq", 800 }, { "tone", 3500 }, { "sub", 0.3f }, { "clickType", 0 }, { "clickLevel", 0.2f },
            { "clickTone", 5000 }, { "compress", 0.45f }, { "clip", 0.5f } } },
        { "Hard Techno: Distorted Rumble", { { "tune", 30 }, { "pitchStart", 38 }, { "pitchTime", 46 }, { "pitchCurve", 0.5f }, { "hold", 100 }, { "decay", 360 },
            { "drive", 17 }, { "crunch", 3 }, { "crunchFreq", 700 }, { "tone", 4000 }, { "sub", 0.3f }, { "clickLevel", 0.15f }, { "rumble", 0.75f }, { "rumbleSize", 2.6f },
            { "rumbleTone", 170 }, { "rumbleDrive", 0.85f }, { "rumbleDuck", 0.95f }, { "compress", 0.4f }, { "clip", 0.4f } } },
        { "Hard Techno: Industrial Pound", { { "tune", 30 }, { "pitchStart", 30 }, { "pitchTime", 50 }, { "pitchCurve", 0.4f }, { "hold", 110 }, { "decay", 360 },
            { "driveType", 1 }, { "drive", 16 }, { "crunch", 2 }, { "crunchFreq", 600 }, { "tone", 3200 }, { "sub", 0.35f }, { "clickType", 0 }, { "clickLevel", 0.2f },
            { "clickTone", 2500 }, { "rumble", 0.4f }, { "rumbleSize", 1.6f }, { "rumbleTone", 110 }, { "rumbleDrive", 0.6f }, { "compress", 0.4f }, { "clip", 0.35f } } },
        { "Hard Techno: Warehouse Boom", { { "tune", 29 }, { "pitchStart", 40 }, { "pitchTime", 52 }, { "pitchCurve", 0.4f }, { "hold", 120 }, { "decay", 520 },
            { "drive", 11 }, { "tone", 4000 }, { "sub", 0.45f }, { "clickLevel", 0.15f }, { "rumble", 0.55f }, { "rumbleSize", 3.2f }, { "rumbleTone", 100 },
            { "rumbleDrive", 0.3f }, { "rumbleDuck", 0.85f }, { "compress", 0.3f }, { "clip", 0.25f } } },
        { "Hard Techno: Tight Driver", { { "tune", 32 }, { "pitchStart", 32 }, { "pitchTime", 36 }, { "pitchCurve", 0.55f }, { "hold", 80 }, { "decay", 260 },
            { "driveType", 3 }, { "drive", 18 }, { "crunch", 6 }, { "crunchFreq", 1000 }, { "tone", 4000 }, { "sub", 0.25f }, { "clickType", 2 }, { "clickLevel", 0.2f },
            { "clickTone", 5500 }, { "compress", 0.5f }, { "clip", 0.45f } } },

        //==================================================================== Techno
        { "Techno: Classic Punch", { { "tune", 29 }, { "pitchStart", 34 }, { "pitchTime", 30 }, { "pitchCurve", 0.45f }, { "hold", 20 }, { "decay", 330 },
            { "drive", 8 }, { "tone", 6000 }, { "sub", 0.25f }, { "clickType", 0 }, { "clickLevel", 0.2f }, { "clickTone", 3500 }, { "compress", 0.25f }, { "clip", 0.15f } } },
        { "Techno: Deep Rumble", { { "tune", 28 }, { "pitchStart", 30 }, { "pitchTime", 36 }, { "pitchCurve", 0.4f }, { "hold", 30 }, { "decay", 460 },
            { "drive", 4 }, { "tone", 6000 }, { "sub", 0.35f }, { "clickLevel", 0.25f }, { "rumble", 0.55f }, { "rumbleSize", 2.4f }, { "rumbleTone", 110 },
            { "rumbleDrive", 0.25f }, { "rumbleDuck", 0.85f }, { "compress", 0.25f }, { "clip", 0.15f } } },
        { "Techno: Berlin Thud", { { "tune", 29 }, { "pitchStart", 26 }, { "pitchTime", 34 }, { "pitchCurve", 0.35f }, { "hold", 10 }, { "decay", 260 },
            { "drive", 10 }, { "tone", 2800 }, { "sub", 0.3f }, { "clickType", 0 }, { "clickLevel", 0.2f }, { "clickTone", 1500 }, { "compress", 0.3f }, { "clip", 0.2f } } },
        { "Techno: Peak Time", { { "tune", 30 }, { "pitchStart", 38 }, { "pitchTime", 28 }, { "pitchCurve", 0.5f }, { "hold", 30 }, { "decay", 300 },
            { "drive", 14 }, { "crunch", 2 }, { "crunchFreq", 1200 }, { "tone", 5000 }, { "sub", 0.3f }, { "clickType", 1 }, { "clickLevel", 0.25f }, { "compress", 0.35f },
            { "clip", 0.3f } } },
        { "Techno: Detroit Soft", { { "tune", 31 }, { "pitchStart", 24 }, { "pitchTime", 40 }, { "pitchCurve", 0.4f }, { "hold", 10 }, { "decay", 280 },
            { "bodyShape", 1 }, { "drive", 4 }, { "tone", 6000 }, { "sub", 0.2f }, { "clickType", 0 }, { "clickLevel", 0.15f }, { "clickTone", 3000 }, { "compress", 0.2f },
            { "clip", 0.1f } } },

        //==================================================================== Clean
        { "Clean: Sine Boom", { { "tune", 29 }, { "pitchStart", 18 }, { "pitchTime", 50 }, { "pitchCurve", 0.4f }, { "hold", 80 }, { "decay", 1400 },
            { "drive", 0 }, { "tone", 6000 }, { "sub", 0.6f }, { "clickType", 0 }, { "clickLevel", 0.15f }, { "compress", 0.1f }, { "clip", 0.1f } } },
        { "Clean: House Kick", { { "tune", 31 }, { "pitchStart", 30 }, { "pitchTime", 28 }, { "pitchCurve", 0.45f }, { "hold", 15 }, { "decay", 260 },
            { "drive", 6 }, { "tone", 14000 }, { "sub", 0.2f }, { "clickType", 0 }, { "clickLevel", 0.3f }, { "clickTone", 6000 }, { "clickDecay", 6 }, { "compress", 0.25f },
            { "clip", 0.15f } } },
        { "Clean: Tight Sub", { { "tune", 29 }, { "pitchStart", 22 }, { "pitchTime", 20 }, { "pitchCurve", 0.5f }, { "hold", 0 }, { "decay", 200 },
            { "drive", 2 }, { "tone", 5000 }, { "sub", 0.5f }, { "clickType", 1 }, { "clickLevel", 0.2f }, { "compress", 0.2f }, { "clip", 0.1f } } },
        { "Clean: Long Boom", { { "tune", 27 }, { "pitchStart", 14 }, { "pitchTime", 60 }, { "pitchCurve", 0.3f }, { "glide", -2 }, { "hold", 120 }, { "decay", 2200 },
            { "drive", 6 }, { "tone", 4000 }, { "sub", 0.5f }, { "clickLevel", 0.15f }, { "compress", 0.15f }, { "clip", 0.2f } } },

        //==================================================================== FX
        { "FX: Laser Zap", { { "tune", 33 }, { "pitchStart", 72 }, { "pitchTime", 90 }, { "pitchCurve", 0.3f }, { "hold", 20 }, { "decay", 380 },
            { "drive", 12 }, { "tone", 12000 }, { "sub", 0.2f }, { "clickType", 3 }, { "clickLevel", 0.6f }, { "clickTone", 3000 }, { "clickDecay", 30 }, { "compress", 0.3f },
            { "clip", 0.3f } } },
        { "FX: Bitcrushed", { { "tune", 30 }, { "pitchStart", 40 }, { "pitchTime", 40 }, { "hold", 60 }, { "decay", 300 },
            { "driveType", 4 }, { "drive", 40 }, { "tone", 9000 }, { "sub", 0.3f }, { "clickType", 0 }, { "clickLevel", 0.4f }, { "compress", 0.4f }, { "clip", 0.4f } } },
        { "FX: Fold Monster", { { "tune", 28 }, { "bodyShape", 1 }, { "pitchStart", 48 }, { "pitchTime", 50 }, { "pitchCurve", 0.6f }, { "hold", 200 }, { "decay", 600 },
            { "driveType", 2 }, { "drive", 34 }, { "crunch", 12 }, { "crunchFreq", 500 }, { "tone", 6000 }, { "sub", 0.35f }, { "rumble", 0.4f }, { "rumbleSize", 2.0f },
            { "compress", 0.45f }, { "clip", 0.5f } } },
        { "FX: Square Thump", { { "tune", 29 }, { "bodyShape", 3 }, { "pitchStart", 30 }, { "pitchTime", 30 }, { "hold", 20 }, { "decay", 240 },
            { "drive", 10 }, { "tone", 2400 }, { "sub", 0.4f }, { "clickType", 1 }, { "clickLevel", 0.3f }, { "compress", 0.3f }, { "clip", 0.25f } } },
    };

    return presets;
}

} // namespace kessel
