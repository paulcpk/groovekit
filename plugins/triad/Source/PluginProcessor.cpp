#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace triad
{

const std::vector<Preset>& getPresets()
{
    static const std::vector<Preset> presets {
        { "Init", {} },
        { "Supersaw lead", { { "osc1Shape", saw }, { "osc2Shape", saw }, { "osc2Fine", 0 }, { "osc3Shape", saw }, { "osc3Coarse", 12 }, { "osc3Level", 0.3f },
                             { "unisonVoices", 7 }, { "unisonDetune", 35 }, { "unisonSpread", 0.9f }, { "cutoff", 6000 }, { "resonance", 0.1f }, { "filterEnv", 0.2f },
                             { "ampAttack", 0.005f }, { "ampDecay", 0.3f }, { "ampSustain", 0.9f }, { "ampRelease", 0.35f } } },
        { "Sub bass", { { "osc1Shape", sine }, { "osc2Shape", triangle }, { "osc2Fine", 0 }, { "osc2Level", 0.25f }, { "osc3Level", 0 },
                        { "cutoff", 400 }, { "filterEnv", 0 }, { "voiceMode", mono }, { "glide", 0.03f },
                        { "ampAttack", 0.002f }, { "ampDecay", 0.2f }, { "ampSustain", 1 }, { "ampRelease", 0.1f } } },
        { "Acid line", { { "osc1Shape", saw }, { "osc2Level", 0 }, { "osc3Level", 0 }, { "filterType", lowpass24 }, { "cutoff", 300 }, { "resonance", 0.8f },
                         { "filterEnv", 0.7f }, { "filterDecay", 0.25f }, { "filterSustain", 0 }, { "keyTrack", 0.3f }, { "voiceMode", legato }, { "glide", 0.06f },
                         { "ampSustain", 0.9f }, { "ampRelease", 0.08f } } },
        { "Detroit chord stab", { { "osc1Shape", saw }, { "osc2Shape", square }, { "osc2Fine", 8 }, { "osc2Level", 0.7f }, { "osc3Shape", saw }, { "osc3Coarse", 12 },
                                  { "osc3Level", 0.25f }, { "filterType", lowpass12 }, { "cutoff", 900 }, { "resonance", 0.35f }, { "filterEnv", 0.55f },
                                  { "filterDecay", 0.22f }, { "filterSustain", 0 }, { "ampAttack", 0.001f }, { "ampDecay", 0.35f }, { "ampSustain", 0 }, { "ampRelease", 0.3f } } },
        { "Pluck", { { "osc1Shape", saw }, { "osc2Shape", square }, { "osc2Coarse", 12 }, { "osc2Fine", 0 }, { "osc2Level", 0.4f }, { "osc3Level", 0 },
                     { "cutoff", 500 }, { "resonance", 0.3f }, { "filterEnv", 0.8f }, { "filterDecay", 0.18f }, { "filterSustain", 0 },
                     { "ampDecay", 0.3f }, { "ampSustain", 0 }, { "ampRelease", 0.25f } } },
        { "Square bass", { { "osc1Shape", square }, { "osc1Coarse", -12 }, { "osc2Shape", saw }, { "osc2Coarse", -12 }, { "osc2Fine", 5 }, { "osc2Level", 0.5f },
                           { "osc3Level", 0 }, { "cutoff", 700 }, { "resonance", 0.25f }, { "filterEnv", 0.4f }, { "filterDecay", 0.15f }, { "filterSustain", 0.1f },
                           { "voiceMode", mono }, { "ampDecay", 0.2f }, { "ampSustain", 0.7f }, { "ampRelease", 0.08f } } },
        { "Warm pad", { { "osc1Shape", saw }, { "osc2Shape", saw }, { "osc2Fine", 6 }, { "osc2Detune", 12 }, { "osc3Shape", triangle }, { "osc3Coarse", 12 },
                        { "osc3Level", 0.3f }, { "unisonVoices", 3 }, { "unisonDetune", 20 }, { "unisonSpread", 0.8f },
                        { "filterType", lowpass12 }, { "cutoff", 1500 }, { "resonance", 0.1f }, { "filterEnv", 0.2f }, { "filterAttack", 0.8f },
                        { "filterDecay", 2 }, { "filterSustain", 0.5f }, { "ampAttack", 0.8f }, { "ampDecay", 1 }, { "ampSustain", 0.8f }, { "ampRelease", 2 },
                        { "lfoTarget", lfoCutoff }, { "lfoRate", 0.3f }, { "lfoDepth", 0.15f } } },
        { "Dark drone", { { "osc1Shape", saw }, { "osc1Coarse", -24 }, { "osc2Shape", saw }, { "osc2Coarse", -12 }, { "osc2Fine", -9 }, { "osc3Shape", noise },
                          { "osc3Level", 0.05f }, { "cutoff", 180 }, { "resonance", 0.5f }, { "filterEnv", 0 },
                          { "lfoTarget", lfoCutoff }, { "lfoRate", 0.1f }, { "lfoDepth", 0.25f }, { "ampAttack", 1.5f }, { "ampSustain", 1 }, { "ampRelease", 3 } } },
        { "Rave hoover", { { "osc1Shape", pulse }, { "osc2Shape", saw }, { "osc2Fine", 15 }, { "osc2Detune", 30 }, { "osc3Shape", saw }, { "osc3Coarse", -12 },
                           { "osc3Level", 0.5f }, { "unisonVoices", 5 }, { "unisonDetune", 50 }, { "unisonSpread", 1 }, { "cutoff", 5000 }, { "filterEnv", -0.2f },
                           { "voiceMode", legato }, { "glide", 0.15f }, { "ampSustain", 1 }, { "ampRelease", 0.3f } } },
        { "FM bell", { { "osc1Shape", sine }, { "osc2Shape", sine }, { "osc2Coarse", 19 }, { "osc2Fine", 0 }, { "osc2Mod", fm }, { "osc2Level", 0.6f },
                       { "osc3Level", 0 }, { "filterType", filterOff }, { "ampAttack", 0.001f }, { "ampDecay", 2 }, { "ampSustain", 0 }, { "ampRelease", 1.5f } } },
        { "Ring metal", { { "osc1Shape", square }, { "osc2Shape", sine }, { "osc2Coarse", 5 }, { "osc2Fine", 0 }, { "osc2Mod", ring }, { "osc2Level", 1 },
                          { "osc3Level", 0 }, { "cutoff", 6000 }, { "filterEnv", 0 }, { "ampDecay", 0.5f }, { "ampSustain", 0 }, { "ampRelease", 0.4f } } },
        { "Organ", { { "osc1Shape", sine }, { "osc2Shape", sine }, { "osc2Coarse", 12 }, { "osc2Fine", 0 }, { "osc2Level", 0.6f }, { "osc3Shape", sine },
                     { "osc3Coarse", 19 }, { "osc3Level", 0.35f }, { "filterType", filterOff }, { "ampAttack", 0.005f }, { "ampDecay", 0.1f },
                     { "ampSustain", 1 }, { "ampRelease", 0.08f } } },
        { "Vibrato lead", { { "osc1Shape", triangle }, { "osc2Shape", square }, { "osc2Coarse", 12 }, { "osc2Fine", 0 }, { "osc2Level", 0.3f }, { "osc3Level", 0 },
                            { "filterType", lowpass12 }, { "cutoff", 3000 }, { "filterEnv", 0 }, { "lfoTarget", lfoPitch }, { "lfoRate", 5.5f },
                            { "lfoDepth", 0.25f }, { "voiceMode", legato }, { "glide", 0.05f } } },
        { "Noise sweep", { { "osc1Shape", noise }, { "osc2Level", 0 }, { "osc3Level", 0 }, { "filterType", bandpass }, { "cutoff", 800 }, { "resonance", 0.7f },
                           { "filterEnv", 0 }, { "lfoTarget", lfoCutoff }, { "lfoRate", 0.15f }, { "lfoDepth", 0.6f }, { "ampAttack", 0.5f },
                           { "ampSustain", 1 }, { "ampRelease", 1.5f } } },
    };

    return presets;
}

//==============================================================================
Processor::Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "Triad", createLayout())
{
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto out = layout.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void Processor::prepareToPlay (double sampleRate, int)
{
    synth.prepare (sampleRate);
}

Settings Processor::readSettings() const
{
    Settings s;

    for (int n = 1; n <= 3; ++n)
    {
        auto& o = s.osc[n - 1];
        o.shape = getIndex (id::osc (n, "Shape"));
        o.coarse = get (id::osc (n, "Coarse"));
        o.fine = get (id::osc (n, "Fine"));
        o.level = get (id::osc (n, "Level"));
        o.pan = get (id::osc (n, "Pan"));
        o.phase = get (id::osc (n, "Phase"));
        o.detune = get (id::osc (n, "Detune"));
        o.mod = n > 1 ? getIndex (id::osc (n, "Mod")) : mix;
    }

    s.unisonVoices = getIndex (id::unisonVoices);
    s.unisonDetune = get (id::unisonDetune);
    s.unisonSpread = get (id::unisonSpread);
    s.filterType = getIndex (id::filterType);
    s.cutoff = get (id::cutoff);
    s.resonance = get (id::resonance);
    s.filterEnv = get (id::filterEnv);
    s.keyTrack = get (id::keyTrack);
    s.amp = { get (id::ampAttack), get (id::ampDecay), get (id::ampSustain), get (id::ampRelease) };
    s.filter = { get (id::filterAttack), get (id::filterDecay), get (id::filterSustain), get (id::filterRelease) };
    s.lfoShape = getIndex (id::lfoShape);
    s.lfoRate = get (id::lfoRate);
    s.lfoTarget = getIndex (id::lfoTarget);
    s.lfoDepth = get (id::lfoDepth);
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

    synth.render (buffer, midi, readSettings());

    if (buffer.getNumChannels() == 1)
        buffer.applyGain (juce::MathConstants<float>::sqrt2 * 0.5f);

    auto position = scopePosition.load (std::memory_order_relaxed);

    for (int i = 0; i < buffer.getNumSamples(); ++i, position = (position + 1) % scopeSize)
        scope[static_cast<size_t> (position)] = buffer.getSample (0, i);

    scopePosition.store (position, std::memory_order_release);
    activeVoices = synth.countActiveVoices();
}

void Processor::readScope (float* destination, int numSamples) const
{
    numSamples = std::min (numSamples, scopeSize);
    const auto end = scopePosition.load (std::memory_order_acquire);

    for (int i = 0; i < numSamples; ++i)
        destination[i] = scope[static_cast<size_t> ((end - numSamples + i + scopeSize) % scopeSize)];
}

//==============================================================================
int Processor::getNumPrograms()
{
    return static_cast<int> (getPresets().size());
}

const juce::String Processor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? getPresets()[static_cast<size_t> (index)].name : "";
}

void Processor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    program = index;
    const auto& preset = getPresets()[static_cast<size_t> (index)];

    // Every parameter: the preset's value, or its default, so nothing of the last sound stays.
    for (auto* p : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            auto value = ranged->getDefaultValue();

            for (auto& [id, v] : preset.values)
                if (ranged->paramID == id)
                    value = ranged->convertTo0to1 (v);

            ranged->setValueNotifyingHost (value);
        }
    }
}

void Processor::getStateInformation (juce::MemoryBlock& data)
{
    auto tree = state.copyState();
    tree.setProperty ("program", program, nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, data);
}

void Processor::setStateInformation (const void* data, int size)
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

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

} // namespace triad

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new triad::Processor();
}
