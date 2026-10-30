#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Sextet: six oscillators that morph between shapes and can modulate, ring and sync each
    other, two filters, three envelopes, three LFOs, a modulation matrix and effects, for pads,
    long chords, trance sounds and raw basslines. */
namespace sextet
{

constexpr int numOscs = 6, numLfos = 3, numMods = 10;

namespace id
{
    inline juce::String osc (int n, const char* what)     { return "osc" + juce::String (n) + what; }
    inline juce::String filter (int k, const char* what)  { return "filter" + juce::String (k) + what; }
    inline juce::String lfo (int k, const char* what)     { return "lfo" + juce::String (k) + what; }
    inline juce::String mod (int m, const char* what)     { return "mod" + juce::String (m) + what; }
    inline juce::String env (const char* which, const char* what) { return juce::String (which) + what; }

    inline const juce::String filterRouting = "filterRouting", filterBalance = "filterBalance";
    inline const juce::String driveType = "driveType", drive = "drive", chorusRate = "chorusRate", chorusDepth = "chorusDepth", chorusMix = "chorusMix";
    inline const juce::String delaySync = "delaySync", delayFeedback = "delayFeedback", delayMix = "delayMix", delayTone = "delayTone", delayPingPong = "delayPingPong";
    inline const juce::String reverbSize = "reverbSize", reverbDamp = "reverbDamp", reverbMix = "reverbMix";
    inline const juce::String voiceMode = "voiceMode", glide = "glide", velocity = "velocity", bendRange = "bendRange", gain = "gain";
}

inline const juce::StringArray morphNames { "Sine", "Triangle", "Saw", "Square", "Pulse" };
inline const juce::StringArray modSourceNames { "Off", "Osc 1", "Osc 2", "Osc 3", "Osc 4", "Osc 5", "Osc 6" };
inline const juce::StringArray routeNames { "Filter 1", "Filter 2", "Both", "Direct" };
inline const juce::StringArray filterTypeNames { "Off", "Low-pass 12", "Low-pass 24", "High-pass 12", "High-pass 24", "Band-pass", "Notch" };
inline const juce::StringArray routingNames { "Parallel", "Series" };
inline const juce::StringArray lfoShapeNames { "Sine", "Triangle", "Saw", "Square", "Sample & hold", "Drift" };
inline const juce::StringArray driveNames { "Soft", "Hard", "Fold" };
inline const juce::StringArray voiceModeNames { "Poly", "Mono", "Legato" };

/** Where modulation comes from, and what it moves. */
inline const juce::StringArray sourceNames { "Off", "LFO 1", "LFO 2", "LFO 3", "Env 2", "Env 3", "Amp env", "Velocity", "Mod wheel", "Aftertouch", "Key", "Random" };
inline const juce::StringArray targetNames {
    "Off", "Pitch", "Pitch 1", "Pitch 2", "Pitch 3", "Pitch 4", "Pitch 5", "Pitch 6",
    "Level 1", "Level 2", "Level 3", "Level 4", "Level 5", "Level 6",
    "Morph", "Warp", "FM", "Ring", "Pan", "Filter 1 cutoff", "Filter 1 res", "Filter 2 cutoff", "Filter 2 res",
    "LFO 1 rate", "LFO 2 rate", "LFO 3 rate", "Drive", "Detune",
};

enum Route { routeFilter1, routeFilter2, routeBoth, routeDirect };
enum FilterType { filterOff, lowpass12, lowpass24, highpass12, highpass24, bandpass, notch };
enum Source { sourceOff, sourceLfo1, sourceLfo2, sourceLfo3, sourceEnv2, sourceEnv3, sourceAmp, sourceVelocity, sourceWheel, sourceAftertouch, sourceKey, sourceRandom };
enum Target { targetOff, targetPitch, targetPitch1, targetPitch2, targetPitch3, targetPitch4, targetPitch5, targetPitch6,
              targetLevel1, targetLevel2, targetLevel3, targetLevel4, targetLevel5, targetLevel6,
              targetMorph, targetWarp, targetFm, targetRing, targetPan, targetF1Cutoff, targetF1Res, targetF2Cutoff, targetF2Res,
              targetLfo1Rate, targetLfo2Rate, targetLfo3Rate, targetDrive, targetDetune };
enum VoiceMode { poly, mono, legato };

/** The LFO sync choices: free, then the tempo divisions. */
juce::StringArray lfoSyncNames();

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace sextet
