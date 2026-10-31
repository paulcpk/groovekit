#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace kessel
{

Processor::Processor()
    : PresetProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true), "Kessel", createLayout(), getPresets(), true)
{
}

void Processor::prepareToPlay (double sampleRate, int blockSize)
{
    rate = sampleRate;
    kick.prepare (sampleRate, blockSize);
    mono.assign (static_cast<size_t> (std::max (blockSize, 32)), 0);
    setLatencySamples (juce::roundToInt (kick.getLatency()));
}

Settings Processor::readSettings() const
{
    Settings s;
    s.clickType = getIndex (id::clickType);
    s.clickLevel = get (id::clickLevel);
    s.clickTone = get (id::clickTone);
    s.clickDecay = get (id::clickDecay);
    s.bodyShape = getIndex (id::bodyShape);
    s.tune = get (id::tune);
    s.followKeys = getBool (id::followKeys);
    s.hold = get (id::hold);
    s.decay = get (id::decay);
    s.sub = get (id::sub);
    s.pitchStart = get (id::pitchStart);
    s.pitchTime = get (id::pitchTime);
    s.pitchCurve = get (id::pitchCurve);
    s.glide = get (id::glide);
    s.driveType = getIndex (id::driveType);
    s.drive = get (id::drive);
    s.crunch = get (id::crunch);
    s.crunchFreq = get (id::crunchFreq);
    s.tone = get (id::tone);
    s.rumble = get (id::rumble);
    s.rumbleSize = get (id::rumbleSize);
    s.rumbleTone = get (id::rumbleTone);
    s.rumbleDrive = get (id::rumbleDrive);
    s.rumbleDuck = get (id::rumbleDuck);
    s.compress = get (id::compress);
    s.clip = get (id::clip);
    s.lowCut = get (id::lowCut);
    s.velocity = get (id::velocity);
    s.gain = get (id::gain);
    return s;
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const auto numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() == 0 || numSamples == 0)
        return;

    if (static_cast<int> (mono.size()) < numSamples)
        mono.resize (static_cast<size_t> (numSamples));

    mergeKeyboard (midi, numSamples);
    const auto settings = readSettings();
    int done = 0;
    auto hitAt = -1;

    // Kicks start on their sample: render up to each note, start it, go on.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (! message.isNoteOn())
            continue;

        const auto at = juce::jlimit (done, numSamples, metadata.samplePosition);
        kick.render (mono.data() + done, at - done, settings);
        done = at;
        kick.trigger (message.getNoteNumber(), message.getFloatVelocity());
        lastKey = message.getNoteNumber();
        hitAt = at;
    }

    kick.render (mono.data() + done, numSamples - done, settings);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.copyFrom (channel, 0, mono.data(), numSamples);

    writeScope (buffer);

    // What the editor shows: the peak falls back about 20 dB a second.
    const auto blockPeak = buffer.getMagnitude (0, 0, numSamples);
    const auto fallen = peak.load() * std::pow (0.1f, static_cast<float> (numSamples / rate));
    peak = std::max (blockPeak, fallen);
    reduction = kick.getGainReduction();
    sinceHit = hitAt >= 0 ? (numSamples - hitAt) / rate : sinceHit.load() + numSamples / rate;
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

} // namespace kessel

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kessel::Processor();
}
