#include "MidiClock.h"

#if JUCE_MAC
 #include <CoreMIDI/CoreMIDI.h>
 #include <mach/mach_time.h>
#endif

namespace gk
{

namespace
{
    constexpr int clocksPerQuarter = 24;
    constexpr int clocksPerSixteenth = clocksPerQuarter / 4;

    MidiClockEvent event (double seconds, juce::uint8 status, int data1 = -1, int data2 = -1)
    {
        MidiClockEvent e { seconds, { status, 0, 0 }, 1 };

        if (data1 >= 0) e.bytes[e.size++] = static_cast<juce::uint8> (data1);
        if (data2 >= 0) e.bytes[e.size++] = static_cast<juce::uint8> (data2);

        return e;
    }

   #if JUCE_MAC
    MIDIPortRef getPort()
    {
        static const auto port = []
        {
            MIDIClientRef client = 0;
            MIDIPortRef p = 0;
            MIDIClientCreate (CFSTR ("GrooveKit Clock"), nullptr, nullptr, &client);
            MIDIOutputPortCreate (client, CFSTR ("Clock"), &p);
            return p;
        }();

        return port;
    }

    MIDITimeStamp toHostTime (juce::uint64 nanos)
    {
        static const auto timebase = []
        {
            mach_timebase_info_data_t t;
            mach_timebase_info (&t);
            return t;
        }();

        return static_cast<MIDITimeStamp> (static_cast<unsigned __int128> (nanos) * timebase.denom / timebase.numer);
    }

    juce::uint64 hostTimeNowNs()
    {
        return clock_gettime_nsec_np (CLOCK_UPTIME_RAW);
    }
   #else
    juce::uint64 hostTimeNowNs()
    {
        return static_cast<juce::uint64> (juce::Time::getMillisecondCounterHiRes() * 1.0e6);
    }
   #endif
}

//==============================================================================
void MidiClockGenerator::process (const MidiClockBlock& b, const std::function<void (const MidiClockEvent&)>& emit)
{
    if (! b.playing || b.endPpq <= b.startPpq)
    {
        if (running)
            emit (event (b.offsetSeconds, 0xfc));

        running = false;
        return;
    }

    const auto start = b.startPpq * clocksPerQuarter;
    const auto end = b.endPpq * clocksPerQuarter;

    if (running && std::abs (b.startPpq - lastEndPpq) * clocksPerQuarter > 0.5)
    {
        emit (event (b.offsetSeconds, 0xfc));
        running = false;
    }

    if (! running)
    {
        running = syncing = true;
        nextTick = static_cast<juce::int64> (std::ceil (start - 1.0e-6));
    }

    lastEndPpq = b.endPpq;

    for (; static_cast<double> (nextTick) < end; ++nextTick)
    {
        const auto at = b.offsetSeconds + std::max (0.0, (static_cast<double> (nextTick) - start) / (end - start)) * b.seconds;

        if (syncing)
        {
            if (nextTick < 0 || nextTick % clocksPerSixteenth != 0)
                continue;

            const auto sixteenths = static_cast<int> (std::min<juce::int64> (nextTick / clocksPerSixteenth, 0x3fff));
            emit (event (at, 0xf2, sixteenths & 0x7f, sixteenths >> 7));
            emit (event (at, nextTick == 0 || b.recording ? 0xfa : 0xfb));
            syncing = false;
        }

        emit (event (at, 0xf8));
    }
}

//==============================================================================
struct MidiClock::Destination
{
    std::string name;
   #if JUCE_MAC
    MIDIEndpointRef endpoint = 0;
   #else
    std::unique_ptr<juce::MidiOutput> output;
   #endif
};

MidiClock::MidiClock() = default;

MidiClock::~MidiClock()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    destinations.clear();
}

bool MidiClock::setOutput (const std::string& name, bool enabled)
{
    const auto devices = juce::MidiOutput::getAvailableDevices();
    const auto index = std::find_if (devices.begin(), devices.end(), [&] (auto& d) { return d.name.toStdString() == name; }) - devices.begin();

    if (index >= devices.size())
        return false;

    std::unique_ptr<Destination> destination;

    if (enabled && ! isSendingTo (name))
    {
        destination = std::make_unique<Destination>();
        destination->name = name;

       #if JUCE_MAC
        // JUCE lists outputs in CoreMIDI's order of destinations.
        if (static_cast<int> (MIDIGetNumberOfDestinations()) != devices.size())
            return false;

        destination->endpoint = MIDIGetDestination (static_cast<ItemCount> (index));
        getPort();
       #else
        destination->output = juce::MidiOutput::openDevice (devices[static_cast<int> (index)].identifier);

        if (destination->output == nullptr)
            return false;
       #endif
    }

    const juce::SpinLock::ScopedLockType sl (lock);

    if (destination != nullptr)
        destinations.push_back (std::move (destination));
    else if (! enabled)
        std::erase_if (destinations, [&] (auto& d) { return d->name == name; });

    return true;
}

bool MidiClock::isSendingTo (const std::string& name) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return std::any_of (destinations.begin(), destinations.end(), [&] (auto& d) { return d->name == name; });
}

void MidiClock::setSource (const void* s)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    source = s;
}

void MidiClock::setRecording (bool r)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    recording = r;
}

void MidiClock::stopNow()
{
    const juce::SpinLock::ScopedLockType sl (lock);

    if (! generator.isRunning())
        return;

    generator.reset();
    const auto stop = event (0, 0xfc);
    send (&stop, 1, hostTimeNowNs());
}

void MidiClock::addBlock (const void* s, double startPpq, double endPpq, double seconds, bool playing)
{
    const juce::SpinLock::ScopedLockType sl (lock);

    if (s != source)
        return;

    const MidiClockBlock block { startPpq, endPpq, seconds, cycleSeconds, playing, recording };
    cycleSeconds += seconds;

    generator.process (block, [this] (const MidiClockEvent& e)
    {
        if (numPending < pending.size())
            pending[numPending++] = e;
    });
}

void MidiClock::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                  int numSamples, const juce::AudioIODeviceCallbackContext& context)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const auto cycleStart = context.hostTimeNs != nullptr ? *context.hostTimeNs : hostTimeNowNs();
    const juce::SpinLock::ScopedLockType sl (lock);

    send (pending.data(), numPending, cycleStart);
    numPending = 0;
    cycleSeconds = 0;
}

void MidiClock::audioDeviceStopped()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    numPending = 0;
    cycleSeconds = 0;
}

void MidiClock::send (const MidiClockEvent* events, size_t num, juce::uint64 cycleHostTimeNs)
{
    if (num == 0)
        return;

    auto dueAt = [&] (const MidiClockEvent& e) { return cycleHostTimeNs + static_cast<juce::uint64> (std::llround (e.seconds * 1.0e9)); };

    if (onSend)
        for (size_t i = 0; i < num; ++i)
            onSend (events[i].toMessage(), dueAt (events[i]));

    for (auto& d : destinations)
    {
       #if JUCE_MAC
        // Timestamped, so CoreMIDI sends each message when it's due rather than when it was handed over.
        alignas (MIDIEventList) juce::uint8 storage[1024];
        auto list = reinterpret_cast<MIDIEventList*> (storage);
        auto packet = MIDIEventListInit (list, kMIDIProtocol_1_0);

        for (size_t i = 0; i < num; ++i)
        {
            const auto& e = events[i];
            // A universal MIDI packet for a system message: type 1, group 0, then the MIDI 1.0 bytes.
            const UInt32 word = (0x1u << 28) | (UInt32 (e.bytes[0]) << 16)
                              | (e.size > 1 ? UInt32 (e.bytes[1]) << 8 : 0) | (e.size > 2 ? UInt32 (e.bytes[2]) : 0);

            packet = MIDIEventListAdd (list, sizeof (storage), packet, toHostTime (dueAt (e)), 1, &word);

            if (packet == nullptr)
            {
                MIDISendEventList (getPort(), d->endpoint, list);
                packet = MIDIEventListInit (list, kMIDIProtocol_1_0);
                packet = MIDIEventListAdd (list, sizeof (storage), packet, toHostTime (dueAt (e)), 1, &word);
            }
        }

        MIDISendEventList (getPort(), d->endpoint, list);
       #else
        for (size_t i = 0; i < num; ++i)
            d->output->sendMessageNow (events[i].toMessage());
       #endif
    }
}

} // namespace gk
