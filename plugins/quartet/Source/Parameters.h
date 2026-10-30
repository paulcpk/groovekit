#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

/** Quartet: four-operator FM with a chord memory and an analogue layer, for house chords and
    techno stabs, in the shape of a classic digital synth. */
namespace quartet
{

namespace id
{
    inline juce::String op (int n, const char* what)    { return "op" + juce::String (n) + what; }

    inline const juce::String algorithm = "algorithm", feedback = "feedback", fmLevel = "fmLevel", vaLevel = "vaLevel";
    inline const juce::String vaShape = "vaShape", vaCoarse = "vaCoarse", vaFine = "vaFine", vaWidth = "vaWidth", vaSub = "vaSub", vaUnison = "vaUnison", vaDetune = "vaDetune";
    inline const juce::String chord = "chord", chordSpread = "chordSpread", chordDetune = "chordDetune";
    inline const juce::String filterType = "filterType", cutoff = "cutoff", resonance = "resonance", filterEnv = "filterEnv", keyTrack = "keyTrack";
    inline const juce::String filterAttack = "filterAttack", filterDecay = "filterDecay", filterSustain = "filterSustain", filterRelease = "filterRelease";
    inline const juce::String ampAttack = "ampAttack", ampDecay = "ampDecay", ampSustain = "ampSustain", ampRelease = "ampRelease";
    inline const juce::String lfoShape = "lfoShape", lfoRate = "lfoRate", lfoTarget = "lfoTarget", lfoDepth = "lfoDepth";
    inline const juce::String chorusMode = "chorusMode", chorusDepth = "chorusDepth", drive = "drive", tone = "tone";
    inline const juce::String voiceMode = "voiceMode", glide = "glide", velocity = "velocity", bendRange = "bendRange", gain = "gain";
}

/** The operators' waves: a sine and seven bent ones, as the digital synths of the time had. */
inline const juce::StringArray waveNames { "Sine", "Half", "Double", "Soft", "Bright", "Hollow", "Fold", "Grit" };
inline const juce::StringArray waveShort { "Sin", "Hlf", "Dbl", "Sft", "Brt", "Hol", "Fld", "Grt" };
inline const juce::StringArray vaShapeNames { "Saw", "Square", "Pulse", "Triangle" };
inline const juce::StringArray chordNames { "Off", "Octave", "Fifth", "Major", "Minor", "Sus 2", "Sus 4", "Major 7", "Minor 7", "Dominant 7", "Minor 9", "House", "Fourths" };
inline const juce::StringArray filterNames { "Off", "Low-pass 24", "Low-pass 12", "High-pass", "Band-pass" };
inline const juce::StringArray lfoShapeNames { "Sine", "Triangle", "Saw", "Square", "Random" };
inline const juce::StringArray lfoTargetNames { "Off", "Vibrato", "Cutoff", "Tremolo", "Pulse width", "FM depth" };
inline const juce::StringArray chorusNames { "Off", "I", "II", "I+II" };
inline const juce::StringArray voiceModeNames { "Poly", "Mono", "Legato" };

enum Wave { sine, half, dbl, soft, bright, hollow, fold, grit };
enum VaShape { vaSaw, vaSquare, vaPulse, vaTriangle };
enum Chord { chordOff, chordOctave, chordFifth, chordMajor, chordMinor, chordSus2, chordSus4, chordMajor7, chordMinor7, chordDominant7, chordMinor9, chordHouse, chordFourths };
enum FilterType { filterOff, lowpass24, lowpass12, highpass, bandpass };
enum LfoTarget { lfoOff, lfoVibrato, lfoCutoff, lfoTremolo, lfoWidth, lfoFm };
enum VoiceMode { poly, mono, legato };

/** An algorithm: which operators modulate each one (bits 0-3 for operators 1-4), and which are
    heard (carriers). Modulators always have higher numbers than what they modulate, so the
    operators are rendered from 4 down to 1. */
struct Algorithm
{
    const char* name;
    std::array<int, 4> modulators;      // for op 1..4: a bitmask of ops feeding it
    int carriers;                       // bitmask
};

inline const std::array<Algorithm, 8> algorithms {{
    { "4>3>2>1",        { 0b0010, 0b0100, 0b1000, 0 }, 0b0001 },
    { "4>3>1, 2>1",     { 0b0110, 0,      0b1000, 0 }, 0b0001 },
    { "4>1, 3>2>1",     { 0b1010, 0b0100, 0,      0 }, 0b0001 },
    { "4>3>1 | 2",      { 0b0100, 0,      0b1000, 0 }, 0b0011 },
    { "4>3 | 2>1",      { 0b0010, 0,      0b1000, 0 }, 0b0101 },
    { "4>1,2,3",        { 0b1000, 0b1000, 0b1000, 0 }, 0b0111 },
    { "4>3 | 1 | 2",    { 0,      0,      0b1000, 0 }, 0b0111 },
    { "1 | 2 | 3 | 4",  { 0,      0,      0,      0 }, 0b1111 },
}};

inline juce::StringArray algorithmNames()
{
    juce::StringArray names;
    for (auto& a : algorithms) names.add (a.name);
    return names;
}

/** The notes of a chord memory, in semitones from the key played. */
std::vector<int> chordIntervals (int chord);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace quartet
