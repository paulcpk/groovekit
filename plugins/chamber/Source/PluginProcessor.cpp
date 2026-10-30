#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace chamber
{

Processor::Processor()
    : PresetProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                       "Chamber", createLayout(), getPresets(), false)
{
}

void Processor::prepareToPlay (double sampleRate, int)
{
    reverb.prepare (sampleRate);
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() == 0)
        return;

    Settings s;
    s.size = get (id::size);
    s.decay = get (id::decay);
    s.predelayMs = get (id::predelay);
    s.damping = get (id::damping);
    s.lowCut = get (id::lowCut);
    s.highCut = get (id::highCut);
    s.diffusion = get (id::diffusion);
    s.modRate = get (id::modRate);
    s.modDepth = get (id::modDepth);
    s.early = get (id::early);
    s.width = get (id::width);
    s.duck = get (id::duck);
    s.freeze = getBool (id::freeze);
    s.mix = get (id::mix);
    reverb.setSettings (s);

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float l = left[i], r = right[i];
        reverb.process (l, r);
        left[i] = l;
        right[i] = r;
    }

    inputLevel = reverb.inputLevel();
    writeScope (buffer);
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

} // namespace chamber

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chamber::Processor();
}
