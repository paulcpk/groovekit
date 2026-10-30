#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace sextet
{

Processor::Processor()
    : PresetProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true), "Sextet", createLayout(), getPresets(), true)
{
}

void Processor::prepareToPlay (double sampleRate, int)
{
    synth.prepare (sampleRate);
}

Settings Processor::readSettings() const
{
    Settings s;

    for (int n = 1; n <= numOscs; ++n)
    {
        auto& o = s.oscs[static_cast<size_t> (n - 1)];
        o.on = getBool (id::osc (n, "On"));
        o.morph = get (id::osc (n, "Morph"));
        o.warp = get (id::osc (n, "Warp"));
        o.coarse = get (id::osc (n, "Coarse"));
        o.fine = get (id::osc (n, "Fine"));
        o.level = get (id::osc (n, "Level"));
        o.pan = get (id::osc (n, "Pan"));
        o.unison = getIndex (id::osc (n, "Unison"));
        o.detune = get (id::osc (n, "Detune"));
        o.modSource = getIndex (id::osc (n, "ModSource"));
        o.fm = get (id::osc (n, "Fm"));
        o.ring = get (id::osc (n, "Ring"));
        o.sync = getBool (id::osc (n, "Sync"));
        o.route = getIndex (id::osc (n, "Route"));
    }

    for (int k = 1; k <= 2; ++k)
    {
        auto& f = s.filters[static_cast<size_t> (k - 1)];
        f.type = getIndex (id::filter (k, "Type"));
        f.cutoff = get (id::filter (k, "Cutoff"));
        f.resonance = get (id::filter (k, "Res"));
        f.drive = get (id::filter (k, "Drive"));
        f.env = get (id::filter (k, "Env"));
        f.key = get (id::filter (k, "Key"));
    }

    s.filterRouting = getIndex (id::filterRouting);
    s.filterBalance = get (id::filterBalance);

    auto envelope = [&] (const char* which)
    {
        return Settings::Env { get (id::env (which, "Attack")), get (id::env (which, "Decay")), get (id::env (which, "Sustain")),
                               get (id::env (which, "Release")), get (id::env (which, "Curve")) };
    };

    s.amp = envelope ("amp");
    s.env2 = envelope ("env2");
    s.env3 = envelope ("env3");

    for (int k = 1; k <= numLfos; ++k)
    {
        auto& l = s.lfos[static_cast<size_t> (k - 1)];
        l.shape = getIndex (id::lfo (k, "Shape"));
        l.rate = get (id::lfo (k, "Rate"));
        l.sync = getIndex (id::lfo (k, "Sync"));
    }

    for (int m = 1; m <= numMods; ++m)
    {
        auto& slot = s.mods[static_cast<size_t> (m - 1)];
        slot.source = getIndex (id::mod (m, "Source"));
        slot.target = getIndex (id::mod (m, "Target"));
        slot.amount = get (id::mod (m, "Amount"));
    }

    s.driveType = getIndex (id::driveType);
    s.drive = get (id::drive);
    s.chorusRate = get (id::chorusRate);
    s.chorusDepth = get (id::chorusDepth);
    s.chorusMix = get (id::chorusMix);
    s.delaySync = getIndex (id::delaySync);
    s.delayFeedback = get (id::delayFeedback);
    s.delayMix = get (id::delayMix);
    s.delayTone = get (id::delayTone);
    s.delayPingPong = getBool (id::delayPingPong);
    s.reverbSize = get (id::reverbSize);
    s.reverbDamp = get (id::reverbDamp);
    s.reverbMix = get (id::reverbMix);
    s.voiceMode = getIndex (id::voiceMode);
    s.glide = get (id::glide);
    s.velocity = get (id::velocity);
    s.bendRange = get (id::bendRange);
    s.gain = juce::Decibels::decibelsToGain (get (id::gain));
    s.modWheel = modWheel;
    s.aftertouch = aftertouch;

    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto bpm = position->getBpm())
                s.tempo = *bpm > 20 ? *bpm : 120.0;

    return s;
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (buffer.getNumChannels() == 0)
        return;

    mergeKeyboard (midi, buffer.getNumSamples());

    // The wheel and the pressure stay as last set, across blocks.
    for (auto event : midi)
    {
        const auto m = event.getMessage();

        if (m.isController() && m.getControllerNumber() == 1)
            modWheel = m.getControllerValue() / 127.0f;
        else if (m.isChannelPressure())
            aftertouch = m.getChannelPressureValue() / 127.0f;
    }

    synth.render (buffer, midi, readSettings());

    if (buffer.getNumChannels() == 1)
        buffer.applyGain (juce::MathConstants<float>::sqrt2 * 0.5f);

    writeScope (buffer);
    activeVoices = synth.countActiveVoices();
    lfoValues = synth.lfoValues;
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

} // namespace sextet

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sextet::Processor();
}
