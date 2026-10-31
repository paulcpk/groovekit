#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace groove8
{

Processor::Processor()
    : MachineProcessor ("Groove-8", createLayout(), getPresets(), getRows(), numVoices, false, getPatterns())
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
            s.tuning[static_cast<size_t> (r - lt)] = get (id::voice (code, "Tuning"));
            s.alternate[static_cast<size_t> (r)] = getIndex (id::voice (code, "Switch")) == 1;
        }
        else if (r == rs || r == cp)
        {
            s.alternate[static_cast<size_t> (r)] = getIndex (id::voice (code, "Switch")) == 1;
        }
    }

    s.bdTone = get (id::bdTone);
    s.bdDecay = get (id::bdDecay);
    s.bdPitch = get (id::bdPitch);
    s.sdTone = get (id::sdTone);
    s.sdSnappy = get (id::sdSnappy);
    s.cyTone = get (id::cyTone);
    s.cyDecay = get (id::cyDecay);
    s.ohDecay = get (id::ohDecay);
    s.accent = get (id::accent);
    s.drive = get (id::drive);
    s.gain = juce::Decibels::decibelsToGain (get (id::gain));
    return s;
}

void Processor::renderVoices (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
{
    synth.render (buffer, midi, readSettings());
}

int Processor::noteForRow (int row) const
{
    return Synth::noteForSound (Synth::soundForRow (row, readSettings()));
}

int Processor::rowForNote (int note) const
{
    return Synth::rowForSound (Synth::soundForNote (note));
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

} // namespace groove8

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new groove8::Processor();
}
