#include "MachineProcessor.h"

namespace rhythm
{

namespace
{
    constexpr juce::uint8 stepVelocity = 100, accentVelocity = 127, graceVelocity = 64;

    bool isSequencerParameter (const juce::String& paramId)
    {
        return paramId == id::seq || paramId == id::pattern || paramId == id::lastStep || paramId == id::scale
            || paramId == id::shuffle || paramId == id::flam || paramId == id::editRow;
    }
}

MachineProcessor::MachineProcessor (const juce::String& name, juce::AudioProcessorValueTreeState::ParameterLayout layout, const std::vector<gkp::Preset>& presets,
                                    std::vector<Row> r, int voiceRows, bool flam, const std::vector<FactoryPattern>& factory)
    : PresetProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true), name, std::move (layout), presets, true),
      rows (std::move (r)), numVoiceRows (voiceRows), accentRow (voiceRows), flamRow (flam ? voiceRows + 1 : -1), patterns (rows)
{
    patterns.load (factory);
    pending.reserve (64);
    steps.reserve (64);
}

void MachineProcessor::prepareToPlay (double rate, int blockSize)
{
    sampleRate = rate;
    sequencer.reset();
    pending.clear();
    sequenced.ensureSize (static_cast<size_t> (std::max (256, blockSize)));
    prepareVoices (rate);
}

void MachineProcessor::setCurrentProgram (int index)
{
    // A preset is a sound: the sequencer keeps playing what it played.
    std::vector<std::pair<juce::RangedAudioParameter*, float>> kept;

    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (isSequencerParameter (ranged->paramID))
                kept.emplace_back (ranged, ranged->getValue());

    PresetProcessor::setCurrentProgram (index);

    for (auto& [param, value] : kept)
        param->setValueNotifyingHost (value);
}

void MachineProcessor::runSequencer (juce::MidiBuffer& midi, int numSamples)
{
    bool playing = false;
    double ppq = 0, bpm = 0;

    if (auto* head = getPlayHead())
    {
        if (auto position = head->getPosition())
        {
            playing = position->getIsPlaying();
            bpm = position->getBpm().orFallback (0.0);
            ppq = position->getPpqPosition().orFallback (0.0);
        }
    }

    hostTempo = bpm;
    hostPlaying = playing;

    if (! getBool (id::seq) || ! playing)
    {
        sequencer.run (false, 0, 0, numSamples, sampleRate, {}, steps);
        pending.clear();
        return;
    }

    Sequencer::Settings settings;
    settings.lastStep = getIndex (id::lastStep);
    settings.scale = getIndex (id::scale);
    settings.shuffle = get (id::shuffle);
    sequencer.run (true, ppq, bpm, numSamples, sampleRate, settings, steps);

    const auto pattern = juce::jlimit (0, numPatterns - 1, getIndex (id::pattern));
    const auto flamSamples = flamRow >= 0 ? juce::roundToInt (get (id::flam) * 0.001 * sampleRate) : 0;
    sequenced.clear();

    // What was left over from the last block (the second hit of a flam).
    for (auto it = pending.begin(); it != pending.end();)
    {
        if (it->samples < numSamples)
        {
            sequenced.addEvent (juce::MidiMessage::noteOn (10, it->note, it->velocity), it->samples);
            it = pending.erase (it);
        }
        else
        {
            it->samples -= numSamples;
            ++it;
        }
    }

    for (auto& s : steps)
    {
        const bool accent = patterns.get (pattern, accentRow, s.step);
        const bool flam = flamRow >= 0 && patterns.get (pattern, flamRow, s.step);

        for (int row = 0; row < numVoiceRows; ++row)
        {
            if (! patterns.get (pattern, row, s.step))
                continue;

            const auto note = noteForRow (row);
            const auto velocity = accent ? accentVelocity : stepVelocity;

            // A flam: a softer grace note, then the hit itself a moment later.
            if (flam && flamSamples > 0)
            {
                sequenced.addEvent (juce::MidiMessage::noteOn (10, note, graceVelocity), s.sample);
                const auto at = s.sample + flamSamples;

                if (at < numSamples)
                    sequenced.addEvent (juce::MidiMessage::noteOn (10, note, velocity), at);
                else
                    pending.push_back ({ at - numSamples, note, velocity });
            }
            else
            {
                sequenced.addEvent (juce::MidiMessage::noteOn (10, note, velocity), s.sample);
            }
        }
    }

    if (! sequenced.isEmpty())
        midi.addEvents (sequenced, 0, numSamples, 0);
}

void MachineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (buffer.getNumChannels() == 0)
        return;

    mergeKeyboard (midi, buffer.getNumSamples());
    runSequencer (midi, buffer.getNumSamples());

    for (auto event : midi)
    {
        const auto m = event.getMessage();

        if (m.isNoteOn())
        {
            const auto row = rowForNote (m.getNoteNumber());

            if (juce::isPositiveAndBelow (row, maxRows))
                ++hits[static_cast<size_t> (row)];
        }
    }

    renderVoices (buffer, midi);

    if (buffer.getNumChannels() == 1)
        buffer.applyGain (juce::MathConstants<float>::sqrt2 * 0.5f);

    writeScope (buffer);
}

void MachineProcessor::getStateInformation (juce::MemoryBlock& data)
{
    // The parameters as every GrooveKit plugin saves them, with the patterns inside.
    juce::MemoryBlock base;
    PresetProcessor::getStateInformation (base);

    if (auto xml = getXmlFromBinary (base.getData(), static_cast<int> (base.getSize())))
    {
        xml->addChildElement (patterns.toTree().createXml().release());
        copyXmlToBinary (*xml, data);
    }
}

void MachineProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (auto* saved = xml->getChildByName (PatternStore::treeType.toString()))
        {
            patterns.fromTree (juce::ValueTree::fromXml (*saved));
            xml->removeChildElement (saved, true);
        }

        juce::MemoryBlock rest;
        copyXmlToBinary (*xml, rest);
        PresetProcessor::setStateInformation (rest.getData(), static_cast<int> (rest.getSize()));
    }
}

} // namespace rhythm
