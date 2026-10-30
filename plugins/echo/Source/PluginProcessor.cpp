#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace echo
{

Processor::Processor()
    : PresetProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                       "Echo", createLayout(), getPresets(), false)
{
}

void Processor::prepareToPlay (double sampleRate, int)
{
    delay.prepare (sampleRate);
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() == 0)
        return;

    double tempo = 120;

    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto bpm = position->getBpm())
                tempo = *bpm > 20 ? *bpm : 120.0;

    Settings s;
    s.mode = getIndex (id::mode);
    const auto& divisions = gkp::dsp::divisions();
    const auto division = divisions[static_cast<size_t> (juce::jlimit (0, static_cast<int> (divisions.size()) - 1, getIndex (id::division)))];
    s.timeSeconds = getBool (id::sync) ? static_cast<float> (division.beats * 60.0 / tempo) : get (id::timeMs) * 0.001f;
    s.ratio = get (id::ratio);
    s.feedback = get (id::feedback);
    s.crossfeed = get (id::crossfeed);
    s.lowCut = get (id::lowCut);
    s.highCut = get (id::highCut);
    s.saturation = get (id::saturation);
    s.wow = get (id::wow);
    s.wowRate = get (id::wowRate);
    s.diffusion = get (id::diffusion);
    s.duck = get (id::duck);
    s.width = get (id::width);
    s.freeze = getBool (id::freeze);
    s.mix = get (id::mix);
    delay.setSettings (s);
    timeSeconds = s.timeSeconds;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float l = left[i], r = right[i];
        delay.process (l, r);
        left[i] = l;
        right[i] = r;
    }

    inputLevel = delay.inputLevel();
    writeScope (buffer);
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

} // namespace echo

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new echo::Processor();
}
