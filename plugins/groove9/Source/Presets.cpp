#include "PluginProcessor.h"

namespace groove9
{

// Dials run 0 to 1 (0 to 10 on the panel), Volume in dB. Levels default to 0.7 (crash and ride
// 0.6), the hats' decays to 0.6, the other dials to 0.5. Presets set the sound only, not the
// patterns.
const std::vector<gkp::Preset>& getPresets()
{
    static const std::vector<gkp::Preset> presets {
        { "Classic: Default", {} },
        { "Classic: Punchy", { { "bdAttack", 0.75f }, { "bdDecay", 0.45f }, { "sdSnappy", 0.65f }, { "sdTone", 0.6f } } },
        { "Classic: Long Kick", { { "bdDecay", 0.85f }, { "bdTune", 0.4f }, { "bdLevel", 0.8f } } },
        { "Classic: Open Room", { { "ohDecay", 0.85f }, { "chDecay", 0.75f }, { "cpLevel", 0.75f }, { "crLevel", 0.7f } } },

        { "House: Crisp", { { "bdAttack", 0.6f }, { "bdDecay", 0.5f }, { "sdTone", 0.65f }, { "sdSnappy", 0.6f }, { "chDecay", 0.45f }, { "ohDecay", 0.5f },
            { "cpLevel", 0.75f } } },
        { "House: Deep", { { "bdTune", 0.35f }, { "bdDecay", 0.65f }, { "bdAttack", 0.35f }, { "sdTone", 0.35f }, { "ohDecay", 0.65f }, { "rdLevel", 0.55f },
            { "ohPan", 0.2f }, { "rdPan", -0.25f } } },
        { "House: Garage Shuffle", { { "bdDecay", 0.4f }, { "bdAttack", 0.55f }, { "rsLevel", 0.75f }, { "chDecay", 0.4f }, { "ohDecay", 0.4f },
            { "sdTune", 0.6f } } },
        { "House: Chicago", { { "bdDecay", 0.6f }, { "bdTune", 0.55f }, { "cpLevel", 0.8f }, { "chDecay", 0.5f }, { "ohDecay", 0.55f }, { "drive", 0.12f } } },

        { "Techno: Hard Kick", { { "bdAttack", 0.9f }, { "bdTune", 0.65f }, { "bdDecay", 0.45f }, { "bdLevel", 0.85f }, { "drive", 0.35f }, { "chDecay", 0.35f } } },
        { "Techno: Rumble", { { "bdDecay", 0.8f }, { "bdTune", 0.3f }, { "bdAttack", 0.4f }, { "drive", 0.25f }, { "rsLevel", 0.6f }, { "rdLevel", 0.5f } } },
        { "Techno: Minimal", { { "bdDecay", 0.35f }, { "bdAttack", 0.6f }, { "rsLevel", 0.75f }, { "chDecay", 0.3f }, { "ohDecay", 0.35f }, { "rsPan", -0.25f },
            { "chPan", 0.25f } } },
        { "Techno: Warehouse", { { "bdDecay", 0.7f }, { "bdAttack", 0.8f }, { "drive", 0.45f }, { "cpLevel", 0.8f }, { "ohDecay", 0.7f }, { "gain", -5 } } },

        { "Acid: Classic", { { "bdDecay", 0.55f }, { "bdAttack", 0.7f }, { "sdSnappy", 0.7f }, { "chDecay", 0.4f }, { "ohDecay", 0.45f } } },
        { "Electro: Tight", { { "bdDecay", 0.35f }, { "bdTune", 0.7f }, { "sdTune", 0.7f }, { "sdSnappy", 0.75f }, { "cpLevel", 0.75f }, { "chDecay", 0.3f } } },
        { "Electro: Toms", { { "ltTune", 0.35f }, { "mtTune", 0.5f }, { "htTune", 0.65f }, { "ltDecay", 0.7f }, { "mtDecay", 0.7f }, { "htDecay", 0.7f },
            { "ltPan", -0.4f }, { "htPan", 0.4f } } },

        { "Hip Hop: Boom", { { "bdTune", 0.2f }, { "bdDecay", 0.9f }, { "bdAttack", 0.3f }, { "bdLevel", 0.85f }, { "sdTone", 0.35f }, { "sdTune", 0.35f } } },
        { "Hip Hop: Dusty", { { "bdTune", 0.4f }, { "bdDecay", 0.6f }, { "sdTone", 0.25f }, { "sdSnappy", 0.45f }, { "chDecay", 0.4f }, { "drive", 0.3f },
            { "gain", -5 } } },

        { "Industrial: Dirty", { { "drive", 0.8f }, { "bdAttack", 1 }, { "bdDecay", 0.6f }, { "sdSnappy", 1 }, { "sdTone", 0.9f }, { "gain", -7 } } },
        { "Industrial: Crushed", { { "drive", 1 }, { "bdTune", 0.8f }, { "bdDecay", 0.4f }, { "cpLevel", 0.85f }, { "rsLevel", 0.8f }, { "gain", -9 } } },

        { "Trance: Driving", { { "bdTune", 0.55f }, { "bdDecay", 0.55f }, { "bdAttack", 0.7f }, { "ohDecay", 0.5f }, { "cpLevel", 0.75f }, { "crLevel", 0.65f } } },
        { "Breaks: Snappy", { { "sdSnappy", 0.85f }, { "sdTone", 0.7f }, { "sdTune", 0.6f }, { "bdDecay", 0.45f }, { "rdLevel", 0.55f } } },
        { "Lo-Fi: Muffled", { { "sdTone", 0.1f }, { "bdAttack", 0.1f }, { "chLevel", 0.5f }, { "ohLevel", 0.5f }, { "crLevel", 0.45f }, { "rdLevel", 0.45f },
            { "drive", 0.2f }, { "gain", -5 } } },
        { "Pitched: Low Kit", { { "bdTune", 0.15f }, { "sdTune", 0.2f }, { "ltTune", 0.1f }, { "mtTune", 0.2f }, { "htTune", 0.3f }, { "crTune", 0.3f },
            { "rdTune", 0.3f } } },
        { "Pitched: High Kit", { { "bdTune", 0.85f }, { "sdTune", 0.85f }, { "ltTune", 0.7f }, { "mtTune", 0.8f }, { "htTune", 0.9f }, { "crTune", 0.7f },
            { "rdTune", 0.7f } } },
        { "Wide: Stereo Kit", { { "ltPan", -0.5f }, { "htPan", 0.5f }, { "chPan", 0.3f }, { "ohPan", 0.3f }, { "crPan", -0.35f }, { "rdPan", 0.35f },
            { "rsPan", -0.3f }, { "cpPan", 0.15f } } },
    };
    return presets;
}

// Sixteen grooves to start from, one per pattern: rows by code ("x" a step), "ac" the accents,
// "fl" the flams.
const std::vector<rhythm::FactoryPattern>& getPatterns()
{
    static const std::vector<rhythm::FactoryPattern> patterns {
        { "House", { { "bd", "x...x...x...x..." }, { "cp", "....x.......x..." }, { "ch", "x.x.x.x.x.x.x.x." }, { "oh", "..x...x...x...x." },
                     { "ac", "x...x...x...x..." } } },
        { "Deep House", { { "bd", "x...x...x...x..." }, { "rs", "....x.......x..." }, { "oh", "..x...x...x...x." }, { "rd", "x.x.x.x.x.x.x.x." },
                          { "cp", "............x..." }, { "ac", "x...x...x...x..." } } },
        { "Techno", { { "bd", "x...x...x...x..." }, { "ch", "xxxxxxxxxxxxxxxx" }, { "oh", "..x...x...x...x." }, { "cp", "....x.......x..." },
                      { "ac", "x.x.x.x.x.x.x.x." } } },
        { "Driving Techno", { { "bd", "x...x...x...x..." }, { "rs", "...x..x....x..x." }, { "oh", "..x...x...x...x." }, { "rd", "xxxxxxxxxxxxxxxx" },
                              { "sd", "....x.......x..x" }, { "ac", "x...x...x...x..." } } },
        { "Acid", { { "bd", "x...x...x...x..." }, { "sd", "....x.......x..." }, { "ch", "x.xxx.xxx.xxx.xx" }, { "oh", "..x...x...x...x." },
                    { "cp", "....x.......x..." }, { "ac", "x...x...x...x..." } } },
        { "Garage", { { "bd", "x.........x....." }, { "sd", "....x.......x..." }, { "rs", "...x....x..x...." }, { "ch", "x.x.x.x.x.x.x.x." },
                      { "oh", "......x.......x." }, { "ac", "x...x.....x.x..." }, { "fl", "............x..." } } },
        { "Breakbeat", { { "bd", "x.x.......x....." }, { "sd", "....x..x.x..x..x" }, { "ch", "x.x.x.x.x.x.x.x." }, { "oh", "......x........." },
                         { "ac", "....x.......x..." }, { "fl", ".........x......" } } },
        { "Electro", { { "bd", "x.....x...x....." }, { "sd", "....x.......x..." }, { "cp", "....x.......x..." }, { "ch", "x.x.x.x.x.x.x.x." },
                       { "lt", "..............x." }, { "ac", "x.....x.....x..." } } },
        { "Hip Hop", { { "bd", "x......x..x....." }, { "sd", "....x.......x..." }, { "ch", "x.x.x.x.x.x.x.x." }, { "oh", "..............x." },
                       { "ac", "....x.......x..." } } },
        { "Trance", { { "bd", "x...x...x...x..." }, { "oh", "..x...x...x...x." }, { "cp", "....x.......x..." }, { "ch", "x...x...x...x..." },
                      { "cr", "x..............." }, { "ac", "x...x...x...x..." } } },
        { "Disco", { { "bd", "x...x...x...x..." }, { "sd", "....x.......x..." }, { "oh", "..x...x...x...x." }, { "ch", "x.x.x.x.x.x.x.x." },
                     { "ac", "....x.......x..." } } },
        { "Shuffle Groove", { { "bd", "x.....x...x....." }, { "rs", "..x...x...x..x.." }, { "ch", "x.xxx.xxx.xxx.xx" }, { "cp", "....x.......x..." },
                              { "ac", "x...x...x...x..." } } },
        { "Tom Fill", { { "bd", "x...x...x...x..." }, { "sd", "....x..........." }, { "ht", "........xx......" }, { "mt", "..........xx...." },
                        { "lt", "............xxxx" }, { "ac", "x.......x...x..x" }, { "fl", "........x...x..." } } },
        { "Half Time", { { "bd", "x.........x....." }, { "sd", "........x......." }, { "ch", "x.x.x.x.x.x.x.x." }, { "oh", "......x.......x." },
                         { "cr", "x..............." }, { "ac", "x.......x......." } } },
        { "Ride Groove", { { "bd", "x...x...x...x..." }, { "rd", "x.xxx.xxx.xxx.xx" }, { "cp", "....x.......x..." }, { "rs", ".......x.......x" },
                           { "ac", "x...x...x...x..." } } },
        { "Minimal", { { "bd", "x...x...x...x..." }, { "rs", "...x.....x...x.." }, { "ch", "..x...x...x...x." }, { "cp", "............x..." },
                       { "ac", "x...x...x...x..." } } },
    };
    return patterns;
}

} // namespace groove9
