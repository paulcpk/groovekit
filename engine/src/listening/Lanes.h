#pragma once

#include "Separator.h"

#include <string>

namespace gk::listening
{

/** Finer lanes than the stems, worked out from them by filtering rather than a model, so they're
    rough but need nothing more than the kept stems:

    - drums-low: the drums below 150 Hz, the kick and its rumble (and a tom's body);
    - drums-mid: the drums from 150 Hz to 5 kHz, the claps, snares and percussion (and the kick's click);
    - drums-high: the drums above 5 kHz, the hats, rides, cymbals and shakers;
    - music-sustained: what holds in the music, pads, atmosphere and drones (sound that stays in its
      frequencies for most of a second);
    - music-short: the rest of the music, stabs, plucks, leads and arpeggios.

    The drums' three add up to the drums (as crossovers do); the music's two to the music. */
inline constexpr const char* laneNames[] = { "drums-low", "drums-mid", "drums-high", "music-sustained", "music-short" };

bool isLane (const std::string&);

/** The stem a lane is worked out from: "drums" or "music". */
std::string stemOfLane (const std::string& lane);

/** A lane from its stem (stereo, at the separator's rate). Throws for an unknown lane. */
juce::AudioBuffer<float> laneOf (const juce::AudioBuffer<float>& stem, const std::string& lane);

/** One kept stem (drums, bass, music or vocals) read from its folder: a stretch of it in seconds
    (to 0 for the end). None if it isn't there. */
std::optional<juce::AudioBuffer<float>> loadStem (const std::filesystem::path& folder, const std::string& name, double from = 0, double to = 0);

/** Writes stereo audio at the separator's rate as 24-bit FLAC. */
void writeFlac (const juce::AudioBuffer<float>&, const std::filesystem::path&);

} // namespace gk::listening
