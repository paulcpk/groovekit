#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostLink.h"

namespace groove3
{

Processor::Processor()
    : PresetProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true), "Groove-3", createLayout(), getPresets(), true)
{
    events.reserve (512);
    held.reserve (128);
    lineKeys.reserve (128);
    ownNotes.reserve (maxSteps);
    playingNotes.reserve (maxSteps);
    ownLine = fromText (factoryLines().front().steps);
    ownLine.resize (maxSteps);
    state.addParameterListener (id::length, this);
    updateNotes();
}

Processor::~Processor()
{
    state.removeParameterListener (id::length, this);
    cancelPendingUpdate();
}

void Processor::prepareToPlay (double sampleRate, int blockSize)
{
    rate = sampleRate;
    voice.prepare (sampleRate, std::max (32, blockSize));
    mono.assign (static_cast<size_t> (std::max (32, blockSize)), 0);
    held.clear();
    lineKeys.clear();
    nextTick = -1;
    setLatencySamples (juce::roundToInt (voice.getLatency()));
}

Settings Processor::readSettings() const
{
    Settings s;
    s.tuning = get (id::tuning);
    s.waveform = getIndex (id::waveform);
    s.cutoff = get (id::cutoff);
    s.resonance = get (id::resonance);
    s.envMod = get (id::envMod);
    s.decayMs = get (id::decay);
    s.accent = get (id::accent);
    s.volumeDb = get (id::volume);
    s.overdrive = get (id::overdrive);
    s.accentDecayMs = get (id::accentDecay);
    s.ampDecayMs = get (id::ampDecay);
    s.softAttackMs = get (id::softAttack);
    s.slideMs = get (id::slideTime);
    s.tracking = get (id::tracking);
    s.filterFm = get (id::filterFm);
    s.sub = get (id::sub);
    s.subOctave = getIndex (id::subOctave);
    s.sweepSpeed = getIndex (id::sweepSpeed);
    s.resonanceRange = getIndex (id::resonanceRange);
    s.muffler = getIndex (id::muffler);
    s.distortion = get (id::distortion);
    return s;
}

void Processor::setCurrentProgram (int index)
{
    // A preset is a sound: the step section keeps playing what it played.
    const auto run = state.getParameter (id::run)->getValue();
    const auto length = state.getParameter (id::length)->getValue();
    PresetProcessor::setCurrentProgram (index);
    state.getParameter (id::run)->setValueNotifyingHost (run);
    state.getParameter (id::length)->setValueNotifyingHost (length);
}

void Processor::setOwnLine (Line line)
{
    line.resize (maxSteps);
    ownLine = std::move (line);
    updateNotes();
}

void Processor::parameterChanged (const juce::String&, float)
{
    // From any thread (automation comes on the audio thread).
    triggerAsyncUpdate();
}

void Processor::handleAsyncUpdate()
{
    updateNotes();
}

void Processor::updateNotes()
{
    const auto length = juce::jlimit (1, maxSteps, getIndex (id::length));
    const Line playing (ownLine.begin(), ownLine.begin() + length);
    auto notes = notesOf (playing, 0, length);

    const juce::SpinLock::ScopedLockType lock (notesLock);
    ownNotes = std::move (notes);
    ownTicks = length * stepTicks;
    notesChanged = true;
}

void Processor::runLine (int numSamples)
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

    const auto stopLine = [&]
    {
        for (auto key : lineKeys)
            events.push_back ({ 0, key, 0, false, true });

        nextTick = -1;
        ownStep = -1;
    };

    // Inside GrooveKit the notes come from its patterns.
    if (! getBool (id::run) || ! playing || bpm <= 0 || link.linked || rhythm::HostLink::isInsideGrooveKit())
    {
        stopLine();
        return;
    }

    {
        const juce::SpinLock::ScopedTryLockType lock (notesLock);

        if (lock.isLocked() && notesChanged)
        {
            playingNotes.assign (ownNotes.begin(), ownNotes.end());
            playingTicks = ownTicks;
            notesChanged = false;
        }
    }

    const auto ticksPerSample = bpm / 60.0 * 960.0 / rate;
    const auto from = ppq * 960.0, to = from + numSamples * ticksPerSample;
    const auto length = static_cast<double> (playingTicks);

    // Moved (a loop or a jump): what was on stops first.
    if (nextTick >= 0 && std::abs (from - nextTick) > ticksPerSample * 2)
        stopLine();

    const auto schedule = [&] (double tick, int key, int velocity, bool on)
    {
        for (auto at = tick + std::ceil ((from - tick) / length) * length; at < to; at += length)
            events.push_back ({ juce::jlimit (0, numSamples - 1, static_cast<int> ((at - from) / ticksPerSample)), key, velocity, on, true });
    };

    for (auto& n : playingNotes)
    {
        schedule (n.start + n.length, n.key, 0, false);
        schedule (n.start, n.key, n.velocity, true);
    }

    ownStep = static_cast<int> (std::fmod (std::max (0.0, from), length) / stepTicks);
    nextTick = to;
}

void Processor::play (const Event& e)
{
    if (e.fromLine)
    {
        std::erase (lineKeys, e.key);

        if (e.on)
            lineKeys.push_back (e.key);
    }

    if (e.key < 0)
    {
        held.clear();
        voice.noteOff();
        soundingKey = -1;
        return;
    }

    if (e.on)
    {
        // Played while another is held: a slide.
        std::erase_if (held, [&] (const Held& h) { return h.key == e.key; });
        const bool legato = ! held.empty();
        const bool accent = e.velocity >= accentVelocity;
        held.push_back ({ e.key, accent });
        voice.noteOn (e.key, accent, legato);
        soundingKey = e.key;
        return;
    }

    const auto it = std::find_if (held.begin(), held.end(), [&] (const Held& h) { return h.key == e.key; });

    if (it == held.end())
        return;

    const bool wasSounding = std::next (it) == held.end();
    held.erase (it);

    if (held.empty())
    {
        voice.noteOff();
        soundingKey = -1;
    }
    else if (wasSounding)
    {
        // Back to the key still held, sliding.
        voice.noteOn (held.back().key, held.back().accent, true);
        soundingKey = held.back().key;
    }
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
    events.clear();

    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        const auto at = juce::jlimit (0, numSamples - 1, metadata.samplePosition);

        if (m.isNoteOn())
            events.push_back ({ at, m.getNoteNumber(), m.getVelocity(), true, false });
        else if (m.isNoteOff())
            events.push_back ({ at, m.getNoteNumber(), 0, false, false });
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            events.push_back ({ at, -1, 0, false, false });
    }

    runLine (numSamples);

    // In time; at the same moment notes end before others start, so only an overlap slides.
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sample != b.sample ? a.sample < b.sample : (! a.on && b.on); });

    const auto settings = readSettings();
    int done = 0;

    for (auto& e : events)
    {
        voice.render (mono.data() + done, e.sample - done, settings);
        done = e.sample;
        play (e);
    }

    voice.render (mono.data() + done, numSamples - done, settings);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.copyFrom (channel, 0, mono.data(), numSamples);

    writeScope (buffer);
    cutoffHz = voice.isSounding() ? voice.getCutoffHz() : 0.0f;
    envelope = voice.getEnvelope();
    sweep = voice.getSweep();
}

void Processor::getStateInformation (juce::MemoryBlock& data)
{
    // The parameters as every GrooveKit plugin saves them, with the line inside.
    juce::MemoryBlock base;
    PresetProcessor::getStateInformation (base);

    if (auto xml = getXmlFromBinary (base.getData(), static_cast<int> (base.getSize())))
    {
        xml->createNewChildElement ("Line")->setAttribute ("steps", toText (ownLine));
        copyXmlToBinary (*xml, data);
    }
}

void Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (auto* saved = xml->getChildByName ("Line"))
        {
            setOwnLine (fromText (saved->getStringAttribute ("steps")));
            xml->removeChildElement (saved, true);
        }

        juce::MemoryBlock rest;
        copyXmlToBinary (*xml, rest);
        PresetProcessor::setStateInformation (rest.getData(), static_cast<int> (rest.getSize()));
        updateNotes();
    }
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

} // namespace groove3

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new groove3::Processor();
}
