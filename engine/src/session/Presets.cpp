#include "Presets.h"

#include <fstream>
#include <set>

namespace gk::presets
{

using model::Json;

namespace
{
    // Starting points, in the effects' own units (see the effect catalogue). Parameters a
    // preset leaves out take their defaults.
    constexpr const char* factoryJson = R"json({
  "eq": [
    { "name": "Flat", "params": {} },
    { "name": "Low cut 80 Hz", "params": { "band1Type": 1, "band1Freq": 80, "band1Q": 0.71 } },
    { "name": "Kick punch", "params": { "band1Type": 2, "band1Freq": 60, "band1GainDb": 3, "band3Freq": 350, "band3GainDb": -4, "band3Q": 1.4, "band5Freq": 3500, "band5GainDb": 3 } },
    { "name": "Bass tighten", "params": { "band1Type": 1, "band1Freq": 35, "band2Freq": 250, "band2GainDb": -3, "band4Freq": 800, "band4GainDb": 2 } },
    { "name": "Vocal presence", "params": { "band1Type": 1, "band1Freq": 100, "band2Freq": 300, "band2GainDb": -2, "band5Freq": 3000, "band5GainDb": 3, "band7GainDb": 2, "band7Freq": 10000 } },
    { "name": "Hi-hat clean", "params": { "band1Type": 1, "band1Freq": 300, "band7GainDb": 2, "band7Freq": 9000 } },
    { "name": "Air", "params": { "band7GainDb": 4, "band7Freq": 12000 } },
    { "name": "Telephone", "params": { "band1Type": 1, "band1Freq": 400, "band4Freq": 1500, "band4GainDb": 4, "band7Type": 6, "band7Freq": 3000 } }
  ],
  "compressor": [
    { "name": "Gentle glue", "params": { "thresholdDb": -18, "ratio": 2, "attackMs": 30, "releaseMs": 150, "outputDb": 2 } },
    { "name": "Drum bus", "params": { "thresholdDb": -20, "ratio": 4, "attackMs": 10, "releaseMs": 100, "outputDb": 4 } },
    { "name": "Vocal", "params": { "thresholdDb": -22, "ratio": 3, "attackMs": 5, "releaseMs": 80, "outputDb": 4 } },
    { "name": "Smash", "params": { "thresholdDb": -35, "ratio": 10, "attackMs": 0.3, "releaseMs": 50, "outputDb": 10 } }
  ],
  "limiter": [
    { "name": "Streaming (-1 dB ceiling)", "params": { "inputGainDb": 0, "ceilingDb": -1, "releaseMs": 100 } },
    { "name": "Loud", "params": { "inputGainDb": 6, "ceilingDb": -0.3, "releaseMs": 60 } }
  ],
  "reverb": [
    { "name": "Small room", "params": { "preDelayMs": 5, "size": 25, "decay": 0.6, "damping": 7000, "lowCut": 200, "diffusion": 60, "width": 80 }, "mix": 0.2 },
    { "name": "Plate", "params": { "preDelayMs": 10, "size": 45, "decay": 1.8, "damping": 9000, "lowCut": 200, "diffusion": 90, "width": 100 }, "mix": 0.25 },
    { "name": "Hall", "params": { "preDelayMs": 25, "size": 80, "decay": 3.5, "damping": 5000, "lowCut": 150, "diffusion": 75, "width": 100 }, "mix": 0.25 },
    { "name": "Snare verb", "params": { "preDelayMs": 0, "size": 40, "decay": 1.2, "damping": 8000, "lowCut": 300, "diffusion": 80, "width": 100 }, "mix": 0.3 },
    { "name": "Huge", "params": { "preDelayMs": 40, "size": 100, "decay": 9, "damping": 4000, "lowCut": 250, "diffusion": 80, "width": 100 }, "mix": 0.3 }
  ],
  "delay": [
    { "name": "1/8 dotted ping-pong", "params": { "sync": 6, "feedback": 35, "pingPong": 1, "lowCut": 200, "highCut": 6000 }, "mix": 0.25 },
    { "name": "Quarter echo", "params": { "sync": 8, "feedback": 30, "lowCut": 150, "highCut": 7000 }, "mix": 0.25 },
    { "name": "Slapback", "params": { "sync": 0, "timeMs": 110, "feedback": 5, "lowCut": 100, "highCut": 9000 }, "mix": 0.3 },
    { "name": "Dub", "params": { "sync": 9, "feedback": 70, "lowCut": 300, "highCut": 2500 }, "mix": 0.35 },
    { "name": "Wide 1/16", "params": { "sync": 2, "feedback": 20, "pingPong": 1 }, "mix": 0.2 }
  ],
  "chorus": [
    { "name": "Subtle", "params": { "rate": 0.5, "depthMs": 2, "delayMs": 12, "feedback": 0, "width": 100 }, "mix": 0.3 },
    { "name": "Lush", "params": { "rate": 0.8, "depthMs": 5, "delayMs": 15, "feedback": 0, "width": 100 }, "mix": 0.5 },
    { "name": "Flanger", "params": { "rate": 0.25, "depthMs": 3, "delayMs": 5, "feedback": 60, "width": 100 }, "mix": 0.5 }
  ],
  "phaser": [
    { "name": "Slow sweep", "params": { "depth": 5, "rate": 0.2, "feedback": 0.6 } },
    { "name": "Fast", "params": { "depth": 4, "rate": 2, "feedback": 0.5 } }
  ],
  "distortion": [
    { "name": "Warm saturation", "params": { "type": 0, "driveDb": 6, "tone": 12000, "outputDb": -3 } },
    { "name": "Crunch", "params": { "type": 2, "driveDb": 18, "tone": 7000, "outputDb": -8 } },
    { "name": "Fuzz", "params": { "type": 1, "driveDb": 36, "tone": 5000, "outputDb": -14 } },
    { "name": "Fold", "params": { "type": 3, "driveDb": 18, "tone": 9000, "outputDb": -8 } },
    { "name": "Lo-fi crush", "params": { "type": 4, "driveDb": 30, "tone": 6000, "outputDb": -4 } }
  ],
  "lowpass": [
    { "name": "Muffled", "params": { "frequency": 800 } },
    { "name": "Darker", "params": { "frequency": 4000 } }
  ],
  "highpass": [
    { "name": "Low cut 100 Hz", "params": { "frequency": 100 } },
    { "name": "Thin", "params": { "frequency": 600 } }
  ],
  "sampler": [
    { "name": "Reset", "envelope": { "attack": 0, "decay": 0, "sustain": 1, "release": 0.01 }, "filter": { "type": "off" } },
    { "name": "Pluck", "envelope": { "attack": 0, "decay": 0.25, "sustain": 0, "release": 0.15 },
      "filter": { "type": "lowpass", "cutoff": 800, "resonance": 0.3, "envelopeAmount": 4, "envelope": { "attack": 0, "decay": 0.3, "sustain": 0, "release": 0.2 } } },
    { "name": "Stab", "envelope": { "attack": 0, "decay": 0.15, "sustain": 0, "release": 0.05 },
      "filter": { "type": "lowpass", "cutoff": 1500, "resonance": 0.5, "envelopeAmount": 3, "envelope": { "attack": 0, "decay": 0.12, "sustain": 0, "release": 0.05 } } },
    { "name": "Pad", "envelope": { "attack": 0.6, "decay": 1, "sustain": 0.8, "release": 1.5 },
      "filter": { "type": "lowpass", "cutoff": 2500, "resonance": 0.1, "envelopeAmount": 1, "envelope": { "attack": 1, "decay": 2, "sustain": 0.5, "release": 1.5 } } },
    { "name": "Swell", "envelope": { "attack": 1.5, "decay": 0, "sustain": 1, "release": 0.8 },
      "filter": { "type": "lowpass", "cutoff": 400, "resonance": 0.25, "envelopeAmount": 5, "envelope": { "attack": 1.5, "decay": 0, "sustain": 1, "release": 0.8 } } },
    { "name": "Dark", "filter": { "type": "lowpass", "cutoff": 600, "resonance": 0.2 } },
    { "name": "Thin", "filter": { "type": "highpass", "cutoff": 500, "resonance": 0.1 } },
    { "name": "Telephone", "filter": { "type": "bandpass", "cutoff": 1500, "resonance": 0.4 } }
  ]
})json";

    std::filesystem::path fileFor (const std::string& kind, const std::string& name, const std::filesystem::path& folder)
    {
        std::string clean;

        for (auto c : name)
            clean += std::isalnum (static_cast<unsigned char> (c)) || c == ' ' || c == '-' || c == '_' || c == '.' ? c : '-';

        while (! clean.empty() && (clean.front() == '.' || clean.front() == ' '))
            clean.erase (clean.begin());

        if (clean.empty())
            throw std::runtime_error ("a preset needs a name");

        return folder / kind / (clean.substr (0, 80) + ".json");
    }

    void checkKind (const std::string& kind)
    {
        if (! factory().contains (kind))
            throw std::runtime_error ("no presets for \"" + kind + "\"");
    }
}

const Json& factory()
{
    static const auto presets = Json::parse (factoryJson);
    return presets;
}

Json list (const std::string& kind, const std::filesystem::path& folder)
{
    checkKind (kind);
    auto user = Json::array();
    std::error_code ec;

    if (std::filesystem::is_directory (folder / kind, ec))
    {
        std::vector<std::filesystem::path> files;

        for (auto& entry : std::filesystem::directory_iterator (folder / kind, ec))
            if (entry.path().extension() == ".json")
                files.push_back (entry.path());

        std::sort (files.begin(), files.end());

        for (auto& file : files)
        {
            try
            {
                auto preset = Json::parse (std::ifstream (file));
                preset["name"] = file.stem().string();
                user.push_back (preset);
            }
            catch (const std::exception&)
            {
                // A damaged file is left out.
            }
        }
    }

    return { { "factory", factory()[kind] }, { "user", user } };
}

void save (const std::string& kind, const Json& preset, const std::filesystem::path& folder)
{
    checkKind (kind);

    if (! preset.is_object() || ! preset.contains ("name") || ! preset["name"].is_string())
        throw std::runtime_error ("a preset is an object with a name");

    const auto file = fileFor (kind, preset["name"].get<std::string>(), folder);
    std::filesystem::create_directories (file.parent_path());
    auto stored = preset;
    stored.erase ("name");
    std::ofstream (file) << stored.dump (2) << "\n";
}

void remove (const std::string& kind, const std::string& name, const std::filesystem::path& folder)
{
    checkKind (kind);
    std::error_code ec;
    std::filesystem::remove (fileFor (kind, name, folder), ec);
}

} // namespace gk::presets
