#pragma once

#include "model/JsonIO.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <optional>
#include <string>
#include <vector>

namespace gk::listening
{

/** What a kick and its rumble sound like, in numbers. Measured the same way on a reference
    track's clean stretch, a library sample and a channel of the song, so they can be compared:
    the fingerprints say "a dark techno kick", these say "a body at 48 Hz ringing 140 ms, a
    distorted rumble 9 dB under the hit". */
struct KickSound
{
    double bodyHz = 0;                      // the note of the body, 40 to 140 ms after the hit
    double attackMs = 0;                    // the low end's rise to its peak
    double decayMs = 0;                     // the body: from the peak until 12 dB down
    float clickDb = -60;                    // 2-8 kHz in the first 20 ms against the body's low end
    float gritDb = -60;                     // 150 Hz-1.5 kHz against below 150 Hz over the body: distortion
    std::optional<float> rumbleDb;          // the low end between the kicks against the hit; only with a beat, and a rumble there
    std::optional<float> rumbleGritDb;      // the rumble's mids against its low end
    float width = 0;                        // side against mid over the hit, 0 mono
    std::vector<float> spectrum;            // eight bands, 30 Hz to 12 kHz, dB against the loudest
    std::string pattern;                    // sixteen steps of a bar, "x" where it plays; only with a bar grid
    int hits = 0;                           // how many hits it was measured from

    model::Json toJson() const;
    static KickSound fromJson (const model::Json&);

    /** In words: "body 48 Hz (G1), rings 140 ms, click -18 dB, grit -21 dB, rumble -9 dB". */
    std::string describe() const;
};

/** Where to measure: the hits (seconds; found in the low end if empty), and for kicks in a groove
    the beat and the first downbeat (seconds), which bring the rumble and the pattern. */
struct KickTiming
{
    std::vector<double> hits;
    std::optional<double> beat, downbeat;
};

/** Measures the kicks in stereo or mono audio: each hit on its own, then the median. Nothing
    without a hit in the low end. */
std::optional<KickSound> measureKick (const juce::AudioBuffer<float>&, double sampleRate, KickTiming = {});

/** How close a candidate is to a reference kick: score 0-100 (100 the same), each measurement's
    difference, and the biggest in words, most important first ("2 semitones higher"). A one-shot
    without rumble against a reference with one is noted, not counted. */
struct KickComparison
{
    float score = 0;
    model::Json differences;                // name -> the candidate minus the reference
    std::vector<std::string> notes;

    model::Json toJson() const;
};

KickComparison compareKicks (const KickSound& candidate, const KickSound& reference);

/** The note name of a frequency: "G1". */
std::string noteName (double hz);

} // namespace gk::listening
