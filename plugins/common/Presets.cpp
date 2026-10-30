#include "Presets.h"

namespace gkp
{

PresetProcessor::PresetProcessor (const BusesProperties& buses, const juce::String& name, juce::AudioProcessorValueTreeState::ParameterLayout layout,
                                  const std::vector<Preset>& p, bool synth)
    : AudioProcessor (buses), state (*this, nullptr, juce::Identifier (name), std::move (layout)), pluginName (name), presets (p), isSynth (synth)
{
}

bool PresetProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto out = layout.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;

    return isSynth || layout.getMainInputChannelSet() == out;
}

const juce::String PresetProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? presets[static_cast<size_t> (index)].name : "";
}

void PresetProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    program = index;
    const auto& preset = presets[static_cast<size_t> (index)];

    for (auto* param : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            auto value = ranged->getDefaultValue();

            for (auto& [id, v] : preset.values)
                if (ranged->paramID == id)
                    value = ranged->convertTo0to1 (v);

            ranged->setValueNotifyingHost (value);
        }
    }
}

void PresetProcessor::getStateInformation (juce::MemoryBlock& data)
{
    auto tree = state.copyState();
    tree.setProperty ("program", program, nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, data);
}

void PresetProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        if (tree.hasType (state.state.getType()))
        {
            program = tree.getProperty ("program", 0);
            state.replaceState (tree);
        }
    }
}

void PresetProcessor::writeScope (const juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() == 0)
        return;

    auto position = scopePosition.load (std::memory_order_relaxed);
    const auto* left = buffer.getReadPointer (0);
    const auto* right = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);

    for (int i = 0; i < buffer.getNumSamples(); ++i, position = (position + 1) % scopeSize)
        scope[static_cast<size_t> (position)] = 0.5f * (left[i] + right[i]);

    scopePosition.store (position, std::memory_order_release);
}

void PresetProcessor::readScope (float* destination, int numSamples) const
{
    numSamples = std::min (numSamples, scopeSize);
    const auto end = scopePosition.load (std::memory_order_acquire);

    for (int i = 0; i < numSamples; ++i)
        destination[i] = scope[static_cast<size_t> ((end - numSamples + i + scopeSize) % scopeSize)];
}

void PresetProcessor::mergeKeyboard (juce::MidiBuffer& midi, int numSamples)
{
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
}

} // namespace gkp
