#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Echo: a stereo delay with tape and dub in it, played from an X/Y pad. */
namespace echo
{

namespace id
{
    inline const juce::String mode = "mode", sync = "sync", division = "division", timeMs = "timeMs", ratio = "ratio", feedback = "feedback", crossfeed = "crossfeed";
    inline const juce::String lowCut = "lowCut", highCut = "highCut", saturation = "saturation", wow = "wow", wowRate = "wowRate", diffusion = "diffusion";
    inline const juce::String duck = "duck", width = "width", freeze = "freeze", mix = "mix", padX = "padX", padY = "padY";
}

inline const juce::StringArray modeNames { "Stereo", "Ping-pong", "Dual" };
enum Mode { stereo, pingPong, dual };

/** What the pad's axes can be given, in the order the padX and padY choices name them. */
inline const std::vector<juce::String> padParams { id::feedback, id::highCut, id::timeMs, id::ratio, id::crossfeed, id::lowCut, id::saturation, id::wow, id::diffusion, id::duck, id::width, id::mix };

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace echo
