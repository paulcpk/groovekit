#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace quartet
{

Processor::Processor()
    : PresetProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true), "Quartet", createLayout(), getPresets(), true)
{
}

void Processor::prepareToPlay (double sampleRate, int)
{
    synth.prepare (sampleRate);
}

Settings Processor::readSettings() const
{
    Settings s;
    s.algorithm = getIndex (id::algorithm);
    s.feedback = get (id::feedback);
    s.fmLevel = get (id::fmLevel);
    s.vaLevel = get (id::vaLevel);

    for (int n = 1; n <= 4; ++n)
    {
        auto& o = s.ops[static_cast<size_t> (n - 1)];
        o.wave = getIndex (id::op (n, "Wave"));
        o.ratio = get (id::op (n, "Ratio"));
        o.fine = get (id::op (n, "Fine"));
        o.level = get (id::op (n, "Level"));
        o.attack = get (id::op (n, "Attack"));
        o.decay = get (id::op (n, "Decay"));
        o.sustain = get (id::op (n, "Sustain"));
        o.release = get (id::op (n, "Release"));
        o.velocity = get (id::op (n, "Velocity"));
        o.keyScale = get (id::op (n, "KeyScale"));
    }

    s.vaShape = getIndex (id::vaShape);
    s.vaCoarse = get (id::vaCoarse);
    s.vaFine = get (id::vaFine);
    s.vaWidth = get (id::vaWidth);
    s.vaSub = get (id::vaSub);
    s.vaUnison = getIndex (id::vaUnison);
    s.vaDetune = get (id::vaDetune);
    s.chord = getIndex (id::chord);
    s.chordSpread = get (id::chordSpread);
    s.chordDetune = get (id::chordDetune);
    s.filterType = getIndex (id::filterType);
    s.cutoff = get (id::cutoff);
    s.resonance = get (id::resonance);
    s.filterEnv = get (id::filterEnv);
    s.keyTrack = get (id::keyTrack);
    s.filterAttack = get (id::filterAttack);
    s.filterDecay = get (id::filterDecay);
    s.filterSustain = get (id::filterSustain);
    s.filterRelease = get (id::filterRelease);
    s.ampAttack = get (id::ampAttack);
    s.ampDecay = get (id::ampDecay);
    s.ampSustain = get (id::ampSustain);
    s.ampRelease = get (id::ampRelease);
    s.lfoShape = getIndex (id::lfoShape);
    s.lfoRate = get (id::lfoRate);
    s.lfoTarget = getIndex (id::lfoTarget);
    s.lfoDepth = get (id::lfoDepth);
    s.chorusMode = getIndex (id::chorusMode);
    s.chorusDepth = get (id::chorusDepth);
    s.drive = get (id::drive);
    s.tone = get (id::tone);
    s.voiceMode = getIndex (id::voiceMode);
    s.glide = get (id::glide);
    s.velocity = get (id::velocity);
    s.bendRange = get (id::bendRange);
    s.gain = juce::Decibels::decibelsToGain (get (id::gain));
    return s;
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (buffer.getNumChannels() == 0)
        return;

    mergeKeyboard (midi, buffer.getNumSamples());
    synth.render (buffer, midi, readSettings());

    if (buffer.getNumChannels() == 1)
        buffer.applyGain (juce::MathConstants<float>::sqrt2 * 0.5f);

    writeScope (buffer);
    activeVoices = synth.countActiveVoices();
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

} // namespace quartet

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new quartet::Processor();
}
