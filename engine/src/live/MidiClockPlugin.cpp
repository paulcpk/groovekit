#include "MidiClockPlugin.h"

namespace gk
{

const char* MidiClockPlugin::xmlTypeName = "groovekitMidiClock";

MidiClockPlugin::MidiClockPlugin (tracktion::PluginCreationInfo info)
    : Plugin (info)
{
}

MidiClockPlugin::~MidiClockPlugin()
{
    notifyListenersOfDeletion();
}

void MidiClockPlugin::setClock (std::shared_ptr<MidiClock> c)
{
    clock = std::move (c);
}

void MidiClockPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;

    // A copy, so the audio thread never reads a tempo map the Edit is rebuilding.
    auto copy = edit.tempoSequence.getInternalSequence();
    const juce::SpinLock::ScopedLockType sl (tempoLock);
    tempo = std::move (copy);
}

void MidiClockPlugin::applyToBuffer (const tracktion::PluginRenderContext& rc)
{
    if (clock == nullptr || rc.isRendering)
        return;

    const auto seconds = rc.bufferNumSamples / sampleRate;
    double startPpq = 0, endPpq = 0;

    {
        const juce::SpinLock::ScopedTryLockType sl (tempoLock);

        if (! sl.isLocked() || ! tempo)
            return;

        tracktion::tempo::Sequence::Position position (*tempo);
        position.set (rc.editTime.getStart());
        startPpq = position.getPPQTime();
        position.set (rc.editTime.getStart() + tracktion::TimeDuration::fromSeconds (seconds));
        endPpq = position.getPPQTime();
    }

    clock->addBlock (this, startPpq, endPpq, seconds, rc.isPlaying && ! rc.isScrubbing);
}

} // namespace gk
