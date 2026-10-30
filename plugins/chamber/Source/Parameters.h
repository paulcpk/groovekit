#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Chamber: an algorithmic reverb from small rooms to endless washes, played from an X/Y pad. */
namespace chamber
{

namespace id
{
    inline const juce::String size = "size", decay = "decay", predelay = "predelay", damping = "damping", lowCut = "lowCut", highCut = "highCut";
    inline const juce::String diffusion = "diffusion", modRate = "modRate", modDepth = "modDepth", early = "early", width = "width", duck = "duck";
    inline const juce::String freeze = "freeze", mix = "mix", padX = "padX", padY = "padY";
}

/** What the pad's axes can be given, in the order the padX and padY choices name them. */
inline const std::vector<juce::String> padParams { id::size, id::decay, id::predelay, id::damping, id::lowCut, id::highCut, id::diffusion, id::modDepth, id::early, id::width, id::duck, id::mix };

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace chamber
