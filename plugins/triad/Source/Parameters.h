#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace triad
{

/** Parameter ids. Oscillators are numbered 1-3: "osc2Shape". */
namespace id
{
    inline juce::String osc (int n, const char* what)   { return "osc" + juce::String (n) + what; }

    inline const juce::String unisonVoices = "unisonVoices", unisonDetune = "unisonDetune", unisonSpread = "unisonSpread";
    inline const juce::String filterType = "filterType", cutoff = "cutoff", resonance = "resonance", filterEnv = "filterEnv", keyTrack = "keyTrack";
    inline const juce::String ampAttack = "ampAttack", ampDecay = "ampDecay", ampSustain = "ampSustain", ampRelease = "ampRelease";
    inline const juce::String filterAttack = "filterAttack", filterDecay = "filterDecay", filterSustain = "filterSustain", filterRelease = "filterRelease";
    inline const juce::String lfoShape = "lfoShape", lfoRate = "lfoRate", lfoTarget = "lfoTarget", lfoDepth = "lfoDepth";
    inline const juce::String voiceMode = "voiceMode", glide = "glide", velocity = "velocity", bendRange = "bendRange", gain = "gain";
}

inline const juce::StringArray shapeNames { "Sine", "Triangle", "Square", "Saw", "Soft saw", "Pulse", "Noise" };
inline const juce::StringArray modNames { "Mix", "FM", "Ring" };
inline const juce::StringArray filterNames { "Off", "Low-pass 24", "Low-pass 12", "High-pass", "Band-pass" };
inline const juce::StringArray lfoShapeNames { "Sine", "Triangle", "Square", "Saw", "Random" };
inline const juce::StringArray lfoTargetNames { "Off", "Pitch", "Cutoff", "Volume", "Pan" };
inline const juce::StringArray voiceModeNames { "Poly", "Mono", "Legato" };

enum Shape { sine, triangle, square, saw, softSaw, pulse, noise };
enum Mod { mix, fm, ring };
enum FilterType { filterOff, lowpass24, lowpass12, highpass, bandpass };
enum LfoTarget { lfoOff, lfoPitch, lfoCutoff, lfoVolume, lfoPan };
enum VoiceMode { poly, mono, legato };

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace triad
