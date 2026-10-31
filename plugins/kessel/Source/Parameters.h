#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Kessel: a kick machine for hard kicks: a click, a body whose pitch dives to a tuned tail,
    distortion with a boost in the mids before it (the "crunch" of hardstyle and hardcore kicks),
    a clean sub under it, a rumble (the kick through a dark, driven reverb that ducks under the
    next kick) and simple compression and clipping. One kick at a time; a note's length doesn't
    matter, every kick plays out. */
namespace kessel
{

namespace id
{
    inline const juce::String clickType = "clickType", clickLevel = "clickLevel", clickTone = "clickTone", clickDecay = "clickDecay";
    inline const juce::String bodyShape = "bodyShape", tune = "tune", followKeys = "followKeys", hold = "hold", decay = "decay", sub = "sub";
    inline const juce::String pitchStart = "pitchStart", pitchTime = "pitchTime", pitchCurve = "pitchCurve", glide = "glide";
    inline const juce::String driveType = "driveType", drive = "drive", crunch = "crunch", crunchFreq = "crunchFreq", tone = "tone";
    inline const juce::String rumble = "rumble", rumbleSize = "rumbleSize", rumbleTone = "rumbleTone", rumbleDrive = "rumbleDrive", rumbleDuck = "rumbleDuck";
    inline const juce::String compress = "compress", clip = "clip", lowCut = "lowCut", velocity = "velocity", gain = "gain";
}

inline const juce::StringArray clickNames { "Noise", "Tok", "Snap", "Zap" };
inline const juce::StringArray shapeNames { "Sine", "Triangle", "Soft saw", "Square" };
inline const juce::StringArray driveNames { "Warm", "Hard", "Fold", "Fuzz", "Crush" };

enum Click { clickNoise, clickTok, clickSnap, clickZap };
enum Shape { shapeSine, shapeTriangle, shapeSoftSaw, shapeSquare };
enum Drive { driveWarm, driveHard, driveFold, driveFuzz, driveCrush };

/** The tail's note when keys aren't followed, and the key that plays it when they are (C4, where
    GrooveKit puts drum notes). */
constexpr int rootKey = 60;

/** A MIDI note as its name: 29 -> "F1". */
juce::String noteName (float note);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace kessel
