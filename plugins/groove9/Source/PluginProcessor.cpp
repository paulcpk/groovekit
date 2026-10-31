#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace groove9
{

Processor::Processor()
    : MachineProcessor ("Groove-9", createLayout(), getPresets(), getRows(), numVoices, true, getPatterns())
{
}

void Processor::prepareVoices (double sampleRate)
{
    synth.prepare (sampleRate);
}

Settings Processor::readSettings() const
{
    Settings s;

    for (int r = 0; r < numVoices; ++r)
    {
        const auto code = getRows()[static_cast<size_t> (r)].code.toRawUTF8();
        s.level[static_cast<size_t> (r)] = get (id::voice (code, "Level"));
        s.pan[static_cast<size_t> (r)] = get (id::voice (code, "Pan"));

        if (r == lt || r == mt || r == ht)
        {
            s.tomTune[static_cast<size_t> (r - lt)] = get (id::voice (code, "Tune"));
            s.tomDecay[static_cast<size_t> (r - lt)] = get (id::voice (code, "Decay"));
        }
    }

    s.bdTune = get (id::bdTune);
    s.bdAttack = get (id::bdAttack);
    s.bdDecay = get (id::bdDecay);
    s.sdTune = get (id::sdTune);
    s.sdTone = get (id::sdTone);
    s.sdSnappy = get (id::sdSnappy);
    s.chDecay = get (id::chDecay);
    s.ohDecay = get (id::ohDecay);
    s.crTune = get (id::crTune);
    s.rdTune = get (id::rdTune);
    s.accent = get (id::accent);
    s.drive = get (id::drive);
    s.gain = juce::Decibels::decibelsToGain (get (id::gain));
    return s;
}

void Processor::renderVoices (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    synth.render (buffer, midi, readSettings());
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

} // namespace groove9

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new groove9::Processor();
}
