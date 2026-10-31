#include "PluginProcessor.h"

namespace groove8
{

// Dials run 0 to 1 (0 to 10 on the panel); Bass Drum Pitch in semitones, Volume in dB. The
// switches: 0 tom, rim shot or clap, 1 conga, claves or maracas. Levels default to 0.7, the
// open hat's decay to 0.3, the other dials to 0.5. Presets set the sound only, not the patterns.
const std::vector<gkp::Preset>& getPresets()
{
    static const std::vector<gkp::Preset> presets {
        { "Classic: Default", {} },
        { "Classic: Long Boom", { { "bdDecay", 0.85f }, { "bdTone", 0.3f }, { "bdLevel", 0.8f } } },
        { "Classic: Tight Kit", { { "bdDecay", 0.22f }, { "bdTone", 0.6f }, { "sdSnappy", 0.62f }, { "ohDecay", 0.12f }, { "cyDecay", 0.25f } } },
        { "Classic: Bright", { { "bdTone", 0.85f }, { "sdTone", 0.75f }, { "sdSnappy", 0.7f }, { "cyTone", 0.85f }, { "chLevel", 0.75f } } },
        { "Classic: Congas", { { "ltSwitch", 1 }, { "mtSwitch", 1 }, { "htSwitch", 1 }, { "rsSwitch", 1 }, { "cpSwitch", 1 }, { "ltTuning", 0.4f },
            { "mtTuning", 0.5f }, { "htTuning", 0.6f }, { "ltPan", -0.35f }, { "htPan", 0.35f } } },

        { "Hip Hop: Deep Boom", { { "bdDecay", 0.95f }, { "bdTone", 0.2f }, { "bdPitch", -3 }, { "bdLevel", 0.85f }, { "sdTone", 0.3f }, { "sdSnappy", 0.45f },
            { "ohDecay", 0.2f } } },
        { "Hip Hop: Trap Sub", { { "bdDecay", 1 }, { "bdTone", 0.35f }, { "bdPitch", -5 }, { "bdLevel", 0.9f }, { "drive", 0.12f }, { "sdTone", 0.6f },
            { "sdSnappy", 0.7f }, { "chLevel", 0.6f }, { "ohDecay", 0.15f }, { "cpLevel", 0.75f } } },
        { "Hip Hop: Boom Bap", { { "bdDecay", 0.5f }, { "bdTone", 0.65f }, { "sdTone", 0.55f }, { "sdSnappy", 0.75f }, { "sdLevel", 0.8f }, { "ohDecay", 0.2f } } },
        { "Hip Hop: Dusty", { { "bdDecay", 0.6f }, { "bdTone", 0.15f }, { "sdTone", 0.2f }, { "sdSnappy", 0.5f }, { "cyTone", 0.2f }, { "drive", 0.3f },
            { "gain", -5 } } },

        { "Electro: Long Boom", { { "bdDecay", 0.9f }, { "bdTone", 0.45f }, { "bdLevel", 0.85f }, { "cpLevel", 0.8f }, { "cbLevel", 0.6f } } },
        { "Electro: Robot Funk", { { "bdDecay", 0.6f }, { "bdTone", 0.75f }, { "rsSwitch", 1 }, { "rsLevel", 0.6f }, { "cbLevel", 0.75f }, { "sdTone", 0.65f },
            { "cbPan", 0.3f }, { "rsPan", -0.3f } } },
        { "Electro: Freestyle", { { "bdDecay", 0.7f }, { "sdTone", 0.8f }, { "sdSnappy", 0.8f }, { "ltSwitch", 1 }, { "mtSwitch", 1 }, { "htSwitch", 1 },
            { "cpLevel", 0.8f } } },

        { "House: Crisp", { { "bdDecay", 0.4f }, { "bdTone", 0.65f }, { "sdSnappy", 0.6f }, { "ohDecay", 0.25f }, { "chLevel", 0.75f }, { "cpLevel", 0.75f } } },
        { "House: Deep", { { "bdDecay", 0.55f }, { "bdTone", 0.25f }, { "sdTone", 0.3f }, { "ohDecay", 0.4f }, { "cyTone", 0.3f }, { "cpLevel", 0.65f },
            { "ohPan", 0.2f }, { "chPan", 0.2f } } },
        { "House: Garage", { { "bdDecay", 0.45f }, { "bdTone", 0.5f }, { "rsSwitch", 1 }, { "sdTone", 0.6f }, { "sdSnappy", 0.65f }, { "ohDecay", 0.18f },
            { "ltSwitch", 1 }, { "htSwitch", 1 } } },

        { "Techno: Hard Kick", { { "bdDecay", 0.35f }, { "bdTone", 0.9f }, { "bdLevel", 0.9f }, { "drive", 0.35f }, { "chLevel", 0.6f }, { "ohDecay", 0.15f } } },
        { "Techno: Rumble", { { "bdDecay", 0.8f }, { "bdTone", 0.4f }, { "bdPitch", -1 }, { "drive", 0.25f }, { "rsLevel", 0.6f }, { "cyDecay", 0.7f } } },
        { "Techno: Minimal", { { "bdDecay", 0.25f }, { "bdTone", 0.55f }, { "rsLevel", 0.75f }, { "chLevel", 0.6f }, { "ohDecay", 0.1f }, { "cbLevel", 0.5f },
            { "rsPan", -0.25f }, { "chPan", 0.25f } } },

        { "Industrial: Dirty", { { "drive", 0.75f }, { "bdTone", 1 }, { "bdDecay", 0.5f }, { "sdTone", 0.9f }, { "sdSnappy", 1 }, { "gain", -6 } } },
        { "Industrial: Crushed", { { "drive", 1 }, { "bdDecay", 0.3f }, { "bdTone", 0.8f }, { "sdSnappy", 0.85f }, { "cpLevel", 0.85f }, { "cyTone", 1 },
            { "gain", -8 } } },

        { "Miami: Bass Boom", { { "bdDecay", 1 }, { "bdTone", 0.3f }, { "bdPitch", -2 }, { "bdLevel", 0.9f }, { "sdTone", 0.6f }, { "sdSnappy", 0.7f },
            { "cpLevel", 0.75f } } },
        { "Latin: Percussion", { { "ltSwitch", 1 }, { "mtSwitch", 1 }, { "htSwitch", 1 }, { "rsSwitch", 1 }, { "cpSwitch", 1 }, { "cbLevel", 0.75f },
            { "ltPan", -0.4f }, { "mtPan", -0.1f }, { "htPan", 0.3f }, { "rsPan", 0.45f }, { "cpPan", -0.3f }, { "cbPan", 0.2f } } },
        { "Pop: Ballad", { { "bdDecay", 0.45f }, { "bdTone", 0.35f }, { "sdTone", 0.3f }, { "sdSnappy", 0.35f }, { "cyDecay", 0.85f }, { "cyTone", 0.4f },
            { "ohDecay", 0.35f } } },
        { "Lo-Fi: Muffled", { { "bdTone", 0 }, { "sdTone", 0 }, { "sdSnappy", 0.3f }, { "cyTone", 0 }, { "drive", 0.2f }, { "chLevel", 0.55f }, { "gain", -5 } } },
        { "Wide: Stereo Kit", { { "ltPan", -0.5f }, { "htPan", 0.5f }, { "chPan", 0.3f }, { "ohPan", 0.3f }, { "cyPan", -0.35f }, { "cbPan", 0.4f },
            { "rsPan", -0.3f }, { "cpPan", 0.15f } } },
    };
    return presets;
}

// Sixteen grooves to start from, one per pattern: rows by code ("x" a step), "ac" the accents.
const std::vector<rhythm::FactoryPattern>& getPatterns()
{
    static const std::vector<rhythm::FactoryPattern> patterns {
        { "Four on the Floor", { { "bd", "x...x...x...x..." }, { "cp", "....x.......x..." }, { "ch", "x.x.x.x.x.x.x.x." }, { "oh", "..x...x...x...x." },
                                 { "ac", "x...x...x...x..." } } },
        { "Electro Funk", { { "bd", "x.....x...x....." }, { "sd", "....x.......x..." }, { "ch", "x.x.x.x.x.x.x.x." }, { "cb", ".......x......x." },
                            { "cp", "....x.......x..." }, { "ac", "x.....x.....x..." } } },
        { "Boom Bap", { { "bd", "x......x..x....." }, { "sd", "....x.......x..." }, { "ch", "x.x.x.x.x.x.x.x." }, { "oh", "..............x." },
                        { "ac", "....x.......x..." } } },
        { "Trap", { { "bd", "x......x..x....." }, { "cp", "........x......." }, { "sd", "........x......." }, { "ch", "xxx.x.xxx.xxxx.x" },
                    { "oh", "......x........." }, { "ac", "x.......x......." } } },
        { "Miami Bass", { { "bd", "x..x..x...x..x.." }, { "sd", "....x.......x..." }, { "cp", "....x.......x..." }, { "ch", "x.xxx.xxx.xxx.xx" },
                          { "ac", "x...x.......x..." } } },
        { "Techno", { { "bd", "x...x...x...x..." }, { "rs", "......x.......x." }, { "ch", "..x...x...x...x." }, { "cp", "....x.......x..." },
                      { "cy", "x..............." }, { "ac", "x...x...x...x..." } } },
        { "Breakbeat", { { "bd", "x.x.......x....." }, { "sd", "....x..x.x..x..x" }, { "ch", "x.x.x.x.x.x.x.x." }, { "oh", "......x........." },
                         { "ac", "....x.......x..." } } },
        { "Disco", { { "bd", "x...x...x...x..." }, { "sd", "....x.......x..." }, { "oh", "..x...x...x...x." }, { "ch", "x.x.x.x.x.x.x.x." },
                     { "cb", "x.......x......." }, { "ac", "....x.......x..." } } },
        { "Synth Pop", { { "bd", "x.......x.x....." }, { "sd", "....x.......x..." }, { "cp", "....x.......x..." }, { "ch", "x.x.x.x.x.x.x.x." },
                         { "lt", "..............xx" }, { "ac", "x...x...x...x..." } } },
        { "Slow Jam", { { "bd", "x.....x.x......." }, { "sd", "....x.......x..." }, { "ch", "x.x.x.x.x.x.x.x." }, { "rs", "...........x...." },
                        { "oh", "..............x." }, { "ac", "x...x.......x..." } } },
        { "Latin", { { "rs", "x..x..x...x.x..." }, { "lt", "..x...x...x...x." }, { "mt", "...x.......x...." }, { "ht", ".x...x...x...x.." },
                     { "cp", "x.xxx.xxx.xxx.xx" }, { "bd", "x..x....x..x...." }, { "cb", "x...x...x...x..." }, { "ac", "x.....x...x....." } } },
        { "Dembow", { { "bd", "x...x...x...x..." }, { "sd", "...x..x....x..x." }, { "ch", "x.x.x.x.x.x.x.x." }, { "ac", "...x..x....x..x." } } },
        { "Two-Step", { { "bd", "x.........x....." }, { "sd", "....x.......x..." }, { "rs", "...x....x..x...." }, { "ch", "x.xx.x.xx.x.x.xx" },
                        { "ac", "x...x.....x.x..." } } },
        { "Electro Toms", { { "bd", "x.....x..x......" }, { "sd", "....x.......x..." }, { "ch", "x.x.x.x.x.x.x.x." }, { "lt", "...........x...x" },
                            { "mt", "..........x...x." }, { "ht", ".........x...x.." }, { "ac", "x...x.......x..." } } },
        { "Half Time", { { "bd", "x.........x....." }, { "sd", "........x......." }, { "ch", "x.x.x.x.x.x.x.x." }, { "oh", "......x.......x." },
                         { "cy", "x..............." }, { "ac", "x.......x......." } } },
        { "Minimal", { { "bd", "x...x...x...x..." }, { "rs", "...x.....x...x.." }, { "ch", "..x...x...x...x." }, { "cb", ".....x.........x" },
                       { "ac", "x...x...x...x..." } } },
    };
    return patterns;
}

} // namespace groove8
