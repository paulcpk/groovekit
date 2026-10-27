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
    { "name": "Gentle glue", "params": { "thresholdDb": -18, "ratio": 2, "attackMs": 30, "releaseMs": 150, "outputDb": 2, "kneeDb": 6 } },
    { "name": "Mix bus glue", "params": { "thresholdDb": -14, "ratio": 2, "attackMs": 30, "releaseMs": 300, "outputDb": 2, "kneeDb": 10, "detectLowCut": 90 } },
    { "name": "Drum bus", "params": { "thresholdDb": -20, "ratio": 4, "attackMs": 10, "releaseMs": 100, "outputDb": 4, "kneeDb": 3 } },
    { "name": "Kick punch", "params": { "thresholdDb": -16, "ratio": 4, "attackMs": 30, "releaseMs": 80, "outputDb": 3, "kneeDb": 2 } },
    { "name": "Snare snap", "params": { "thresholdDb": -22, "ratio": 5, "attackMs": 15, "releaseMs": 60, "outputDb": 5 } },
    { "name": "Bass even", "params": { "thresholdDb": -22, "ratio": 4, "attackMs": 15, "releaseMs": 120, "outputDb": 4, "kneeDb": 6 } },
    { "name": "Hats tame", "params": { "thresholdDb": -24, "ratio": 3, "attackMs": 2, "releaseMs": 50, "outputDb": 3, "kneeDb": 4, "detectLowCut": 300 } },
    { "name": "Vocal", "params": { "thresholdDb": -22, "ratio": 3, "attackMs": 5, "releaseMs": 80, "outputDb": 4, "kneeDb": 6, "detectLowCut": 100 } },
    { "name": "Pumping", "params": { "thresholdDb": -30, "ratio": 8, "attackMs": 1, "releaseMs": 180, "outputDb": 8 } },
    { "name": "Room crush", "params": { "thresholdDb": -42, "ratio": 20, "attackMs": 0.1, "releaseMs": 100, "outputDb": 16 } },
    { "name": "Peak catcher", "params": { "thresholdDb": -6, "ratio": 20, "attackMs": 0.1, "releaseMs": 50, "outputDb": 0, "kneeDb": 2 } },
    { "name": "Smash", "params": { "thresholdDb": -35, "ratio": 10, "attackMs": 0.3, "releaseMs": 50, "outputDb": 10 } }
  ],
  "multiband": [
    { "name": "Gentle master", "params": { "bands": 0, "lowFreq": 120, "highFreq": 5000, "kneeDb": 12, "detect": 1,
      "band1ThresholdDb": -20, "band1Ratio": 1.8, "band1AttackMs": 30, "band1ReleaseMs": 250,
      "band2ThresholdDb": -18, "band2Ratio": 1.5, "band2AttackMs": 15, "band2ReleaseMs": 180,
      "band3ThresholdDb": -22, "band3Ratio": 1.8, "band3AttackMs": 5, "band3ReleaseMs": 120 } },
    { "name": "Loud master", "params": { "bands": 1, "lowFreq": 100, "midFreq": 900, "highFreq": 6000, "inputDb": 3, "kneeDb": 8,
      "band1ThresholdDb": -18, "band1Ratio": 3, "band1AttackMs": 20, "band1ReleaseMs": 200, "band1GainDb": 2,
      "band2ThresholdDb": -18, "band2Ratio": 2.5, "band2AttackMs": 10, "band2ReleaseMs": 150, "band2GainDb": 1.5,
      "band3ThresholdDb": -20, "band3Ratio": 2.5, "band3AttackMs": 5, "band3ReleaseMs": 100, "band3GainDb": 1.5,
      "band4ThresholdDb": -22, "band4Ratio": 3, "band4AttackMs": 2, "band4ReleaseMs": 80, "band4GainDb": 2 } },
    { "name": "Techno master", "params": { "bands": 1, "lowFreq": 90, "midFreq": 700, "highFreq": 7000, "kneeDb": 6,
      "band1ThresholdDb": -14, "band1Ratio": 3, "band1AttackMs": 30, "band1ReleaseMs": 120, "band1GainDb": 1,
      "band2ThresholdDb": -18, "band2Ratio": 2, "band2AttackMs": 10, "band2ReleaseMs": 120,
      "band3ThresholdDb": -20, "band3Ratio": 2.5, "band3AttackMs": 4, "band3ReleaseMs": 90, "band3GainDb": 1,
      "band4ThresholdDb": -26, "band4Ratio": 3, "band4AttackMs": 1, "band4ReleaseMs": 60 } },
    { "name": "Dense both ways", "params": { "bands": 0, "lowFreq": 90, "highFreq": 2500, "inputDb": 3, "outputDb": -4, "kneeDb": 6,
      "band1ThresholdDb": -30, "band1Ratio": 6, "band1BelowDb": -42, "band1BelowRatio": 3, "band1AttackMs": 15, "band1ReleaseMs": 130, "band1GainDb": 3,
      "band2ThresholdDb": -30, "band2Ratio": 6, "band2BelowDb": -42, "band2BelowRatio": 3, "band2AttackMs": 5, "band2ReleaseMs": 100, "band2GainDb": 3,
      "band3ThresholdDb": -32, "band3Ratio": 6, "band3BelowDb": -44, "band3BelowRatio": 3, "band3AttackMs": 1, "band3ReleaseMs": 80, "band3GainDb": 3 } },
    { "name": "Dense, a touch", "params": { "bands": 0, "lowFreq": 90, "highFreq": 2500, "kneeDb": 10,
      "band1ThresholdDb": -24, "band1Ratio": 3, "band1BelowDb": -40, "band1BelowRatio": 1.6, "band1AttackMs": 15, "band1ReleaseMs": 150,
      "band2ThresholdDb": -24, "band2Ratio": 3, "band2BelowDb": -40, "band2BelowRatio": 1.6, "band2AttackMs": 6, "band2ReleaseMs": 120,
      "band3ThresholdDb": -26, "band3Ratio": 3, "band3BelowDb": -42, "band3BelowRatio": 1.6, "band3AttackMs": 2, "band3ReleaseMs": 90 } },
    { "name": "Drum bus punch", "params": { "bands": 0, "lowFreq": 120, "highFreq": 4000, "kneeDb": 4,
      "band1ThresholdDb": -20, "band1Ratio": 4, "band1AttackMs": 25, "band1ReleaseMs": 90, "band1GainDb": 3,
      "band2ThresholdDb": -22, "band2Ratio": 3, "band2AttackMs": 12, "band2ReleaseMs": 80, "band2GainDb": 2,
      "band3ThresholdDb": -24, "band3Ratio": 2.5, "band3AttackMs": 3, "band3ReleaseMs": 60, "band3GainDb": 1 } },
    { "name": "Bass tight and even", "params": { "bands": 0, "lowFreq": 90, "highFreq": 900, "kneeDb": 6,
      "band1ThresholdDb": -22, "band1Ratio": 5, "band1AttackMs": 20, "band1ReleaseMs": 150, "band1GainDb": 3,
      "band2ThresholdDb": -24, "band2Ratio": 3, "band2AttackMs": 8, "band2ReleaseMs": 100, "band2GainDb": 2,
      "band3ThresholdDb": -18, "band3Ratio": 1, "band3GainDb": -2 } },
    { "name": "Control the low end", "params": { "bands": 0, "lowFreq": 140, "highFreq": 5000, "kneeDb": 6,
      "band1ThresholdDb": -22, "band1Ratio": 4, "band1AttackMs": 25, "band1ReleaseMs": 180,
      "band2Ratio": 1, "band3Ratio": 1 } },
    { "name": "Tame harsh highs", "params": { "bands": 0, "lowFreq": 150, "highFreq": 4500, "kneeDb": 6,
      "band1Ratio": 1, "band2Ratio": 1,
      "band3ThresholdDb": -28, "band3Ratio": 4, "band3AttackMs": 1, "band3ReleaseMs": 70 } },
    { "name": "De-ess", "params": { "bands": 1, "lowFreq": 150, "midFreq": 4500, "highFreq": 10000, "kneeDb": 3,
      "band1Ratio": 1, "band2Ratio": 1,
      "band3ThresholdDb": -30, "band3Ratio": 6, "band3AttackMs": 0.5, "band3ReleaseMs": 50,
      "band4Ratio": 1 } },
    { "name": "Hats and cymbals smooth", "params": { "bands": 0, "lowFreq": 400, "highFreq": 7000, "kneeDb": 8, "detect": 1,
      "band1Ratio": 1, "band1GainDb": -2,
      "band2ThresholdDb": -24, "band2Ratio": 2.5, "band2AttackMs": 3, "band2ReleaseMs": 60,
      "band3ThresholdDb": -28, "band3Ratio": 3, "band3AttackMs": 1, "band3ReleaseMs": 50 } },
    { "name": "Pad lift", "params": { "bands": 0, "lowFreq": 200, "highFreq": 3000, "kneeDb": 12, "detect": 1,
      "band1Ratio": 1, "band1BelowDb": -36, "band1BelowRatio": 2, "band1ReleaseMs": 300,
      "band2Ratio": 1.5, "band2BelowDb": -36, "band2BelowRatio": 2.5, "band2ReleaseMs": 250,
      "band3Ratio": 1.5, "band3BelowDb": -40, "band3BelowRatio": 3, "band3ReleaseMs": 200 } },
    { "name": "Lo-fi squash", "params": { "bands": 0, "lowFreq": 200, "highFreq": 3000, "inputDb": 8, "outputDb": -6, "kneeDb": 0,
      "band1ThresholdDb": -36, "band1Ratio": 12, "band1AttackMs": 2, "band1ReleaseMs": 60, "band1GainDb": 6,
      "band2ThresholdDb": -36, "band2Ratio": 12, "band2AttackMs": 1, "band2ReleaseMs": 50, "band2GainDb": 8,
      "band3ThresholdDb": -40, "band3Ratio": 12, "band3AttackMs": 0.5, "band3ReleaseMs": 40, "band3GainDb": 4 } },
    { "name": "Neutral (does nothing)", "params": { "band1Ratio": 1, "band2Ratio": 1, "band3Ratio": 1, "band4Ratio": 1 } }
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
  "exciter": [
    { "name": "Clean tube preamp", "params": { "model": 0, "driveDb": 4, "warmth": 30, "harmonics": 10, "harmonicsHz": 5000 } },
    { "name": "Warm triode", "params": { "model": 0, "driveDb": 9, "warmth": 60, "harmonics": 15, "harmonicsHz": 4500 } },
    { "name": "Hot triode", "params": { "model": 0, "driveDb": 16, "warmth": 70, "harmonics": 20, "harmonicsHz": 4000, "outputDb": -1 } },
    { "name": "Pentode push", "params": { "model": 1, "driveDb": 10, "warmth": 20, "harmonics": 15, "harmonicsHz": 4000 } },
    { "name": "Power tube bloom", "params": { "model": 1, "driveDb": 7, "warmth": 40, "harmonics": 10, "body": 30 } },
    { "name": "Tape, fast and clean", "params": { "model": 2, "driveDb": 5, "warmth": 20, "harmonics": 8, "harmonicsHz": 8000 } },
    { "name": "Tape, slow and saturated", "params": { "model": 2, "driveDb": 14, "warmth": 35, "harmonics": 0, "body": 20 } },
    { "name": "Transformer: fat bass", "params": { "model": 3, "driveDb": 12, "warmth": 50, "harmonics": 0, "body": 50 } },
    { "name": "Console summing", "params": { "model": 4, "driveDb": 4, "warmth": 25, "harmonics": 5, "harmonicsHz": 9000 } },
    { "name": "Console, driven channel", "params": { "model": 4, "driveDb": 12, "warmth": 40, "harmonics": 12, "harmonicsHz": 6000 } },
    { "name": "Transistor bite", "params": { "model": 5, "driveDb": 14, "warmth": 50, "harmonics": 20, "harmonicsHz": 3500, "outputDb": -1 } },
    { "name": "Vocal air", "params": { "model": 4, "driveDb": 2, "warmth": 20, "harmonics": 45, "harmonicsHz": 7000 } },
    { "name": "Hi-hat sparkle", "params": { "model": 0, "driveDb": 3, "warmth": 20, "harmonics": 55, "harmonicsHz": 8000 } },
    { "name": "Snare crack", "params": { "model": 5, "driveDb": 8, "warmth": 30, "harmonics": 35, "harmonicsHz": 3000 } },
    { "name": "Kick weight", "params": { "model": 3, "driveDb": 8, "warmth": 60, "harmonics": 5, "harmonicsHz": 3000, "body": 60 } },
    { "name": "Bass on small speakers", "params": { "model": 0, "driveDb": 8, "warmth": 60, "harmonics": 0, "body": 80 } },
    { "name": "Drum bus glow", "params": { "model": 2, "driveDb": 8, "warmth": 40, "harmonics": 15, "harmonicsHz": 6000, "body": 15 } },
    { "name": "Master polish", "params": { "model": 2, "driveDb": 3, "warmth": 25, "harmonics": 8, "harmonicsHz": 10000, "body": 5 } },
    { "name": "Old radio", "params": { "model": 1, "driveDb": 20, "warmth": 60, "harmonics": 30, "harmonicsHz": 2000, "outputDb": -3 } }
  ],
  "bitcrusher": [
    { "name": "Subtle grit", "params": { "bits": 12, "rateHz": 22050, "tone": 16000 } },
    { "name": "12-bit sampler", "params": { "bits": 12, "rateHz": 26040, "tone": 13000 } },
    { "name": "Vintage drum machine", "params": { "driveDb": 3, "bits": 8, "rateHz": 22050, "tone": 11000 } },
    { "name": "8-bit console", "params": { "bits": 8, "rateHz": 16000 } },
    { "name": "4-bit", "params": { "bits": 4, "rateHz": 11025, "outputDb": -3 } },
    { "name": "Telephone", "params": { "driveDb": 6, "bits": 8, "rateHz": 8000, "tone": 3400, "outputDb": -2 } },
    { "name": "Metallic", "params": { "bits": 10, "rateHz": 3000, "tone": 9000 } },
    { "name": "Noisy and broken", "params": { "driveDb": 8, "bits": 5, "rateHz": 6000, "jitter": 60, "tone": 7000, "outputDb": -6 } },
    { "name": "Destroyed", "params": { "driveDb": 12, "bits": 2, "rateHz": 1500, "jitter": 20, "tone": 5000, "outputDb": -10 } }
  ],
  "stereo": [
    { "name": "Neutral", "params": {} },
    { "name": "Swap sides", "params": { "leftToLeft": 0, "rightToLeft": 100, "leftToRight": 100, "rightToRight": 0 } },
    { "name": "Mono", "params": { "leftToLeft": 50, "rightToLeft": 50, "leftToRight": 50, "rightToRight": 50 } },
    { "name": "Left side only", "params": { "leftToLeft": 100, "rightToLeft": 0, "leftToRight": 100, "rightToRight": 0 } },
    { "name": "Right side only", "params": { "leftToLeft": 0, "rightToLeft": 100, "leftToRight": 0, "rightToRight": 100 } },
    { "name": "Sides only", "params": { "leftToLeft": 50, "rightToLeft": -50, "leftToRight": -50, "rightToRight": 50 } },
    { "name": "Invert right", "params": { "rightToRight": -100 } },
    { "name": "Narrow", "params": { "width": 50 } },
    { "name": "Wide", "params": { "width": 160, "monoBelow": 120 } },
    { "name": "Haas wide", "params": { "delayMs": 12, "width": 110, "monoBelow": 150 } },
    { "name": "Mono bass", "params": { "monoBelow": 120 } },
    { "name": "Club safe", "params": { "width": 110, "monoBelow": 180 } },
    { "name": "Lean left", "params": { "leftToLeft": 100, "rightToLeft": 40, "leftToRight": 0, "rightToRight": 60, "pan": -20 } }
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
