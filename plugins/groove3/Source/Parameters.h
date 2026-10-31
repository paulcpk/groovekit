#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Groove-3: a bass line synthesizer modelled on the circuits of the classic of 1981 (a saw or a
    square shaped from it, a four-pole diode ladder filter, an envelope for the filter and one for
    the volume, accents whose filter sweep piles up when they come fast, and slides between held
    notes), with the controls its best-known modifications added: decays of their own for accented
    notes and for the volume, a soft attack, a longer slide, the filter following the keys and
    modulated by the output, overdrive into the filter, a soft clipper after the volume, a sub
    oscillator and a resonance that can sing on its own. One note at a time: a note played while
    another is held slides to it without starting the envelopes again, and a velocity of 110 or
    more is an accent. */
namespace groove3
{

namespace id
{
    // The original's six knobs, its waveform switch and volume.
    inline const juce::String tuning = "tuning", waveform = "waveform", cutoff = "cutoff", resonance = "resonance", envMod = "envMod", decay = "decay", accent = "accent", volume = "volume";

    // The modifications.
    inline const juce::String overdrive = "overdrive", accentDecay = "accentDecay", ampDecay = "ampDecay", softAttack = "softAttack", slideTime = "slideTime";
    inline const juce::String tracking = "tracking", filterFm = "filterFm", sub = "sub", subOctave = "subOctave";
    inline const juce::String sweepSpeed = "sweepSpeed", resonanceRange = "resonanceRange", muffler = "muffler", distortion = "distortion";

    // The step section's own (left alone by presets): whether it plays with the host (outside
    // GrooveKit), and its line's length in steps.
    inline const juce::String run = "run", length = "length";
}

inline const juce::StringArray waveformNames { "Saw", "Square" };
inline const juce::StringArray subOctaveNames { "-1 oct", "-2 oct" };
inline const juce::StringArray sweepSpeedNames { "Fast", "Normal", "Slow" };
inline const juce::StringArray resonanceRangeNames { "Normal", "High" };
inline const juce::StringArray mufflerNames { "Off", "Soft", "Hard" };

enum Waveform { saw, square };
enum SweepSpeed { sweepFast, sweepNormal, sweepSlow };
enum Muffler { mufflerOff, mufflerSoft, mufflerHard };

/** The longest volume decay; the knob turned fully up holds the note at full volume instead. */
constexpr float ampDecayHold = 4000;

/** Velocities from this up play accented. */
constexpr int accentVelocity = 110;

/** The longest line the step section plays on its own, in sixteenths. */
constexpr int maxSteps = 64;

/** A MIDI note as its name: 36 -> "C2". */
juce::String noteName (int note);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace groove3
