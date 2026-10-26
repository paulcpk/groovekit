// MIDI clock: what GrooveKit sends, and (hidden from the default run, as they need a real audio
// device or the Digitakt) how hardware receives it: gk_tests "[hardware]" or "[digitakt]".

#include "TestHelpers.h"

#include "analysis/Analysis.h"
#include "live/LiveEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <tracktion_engine/tracktion_engine.h>

using namespace gk;
using model::Json;
namespace te = tracktion;

namespace
{
    /** A virtual MIDI destination that notes when each message is due. CoreMIDI hands messages
        to a virtual destination straight away with their timestamps (scheduling them is up to the
        receiver, as a hardware driver does), so this reads the timestamps. */
    struct ClockProbe
    {
        struct Event
        {
            double ms;
            juce::MidiMessage message;
        };

        MIDIClientRef client = 0;
        MIDIEndpointRef endpoint = 0;
        std::mutex lock;
        std::vector<Event> events;

        ClockProbe()
        {
            REQUIRE (MIDIClientCreate (CFSTR ("GrooveKit Clock Probe"), nullptr, nullptr, &client) == noErr);
            REQUIRE (MIDIDestinationCreateWithProtocol (client, CFSTR ("GrooveKit Clock Probe"), kMIDIProtocol_1_0, &endpoint,
                                                        ^(const MIDIEventList* list, void*) { receive (list); }) == noErr);
        }

        ~ClockProbe()
        {
            MIDIEndpointDispose (endpoint);
            MIDIClientDispose (client);
        }

        void receive (const MIDIEventList* list)
        {
            mach_timebase_info_data_t timebase;
            mach_timebase_info (&timebase);
            const std::scoped_lock sl (lock);
            auto packet = &list->packet[0];

            for (UInt32 i = 0; i < list->numPackets; ++i, packet = MIDIEventPacketNext (packet))
            {
                const auto due = packet->timeStamp != 0 ? packet->timeStamp : mach_absolute_time();
                const auto ms = static_cast<double> (due) * timebase.numer / timebase.denom / 1.0e6;

                for (UInt32 w = 0; w < packet->wordCount; ++w)
                {
                    const auto word = packet->words[w];
                    const auto status = static_cast<juce::uint8> ((word >> 16) & 0xff);
                    const juce::uint8 bytes[] { status, static_cast<juce::uint8> ((word >> 8) & 0x7f), static_cast<juce::uint8> (word & 0x7f) };
                    events.push_back ({ ms, juce::MidiMessage (bytes, status == 0xf2 ? 3 : 1) });
                }
            }
        }

        std::vector<Event> take()
        {
            const std::scoped_lock sl (lock);
            auto result = std::exchange (events, {});
            std::sort (result.begin(), result.end(), [] (auto& a, auto& b) { return a.ms < b.ms; });
            return result;
        }
    };

    void pump (double seconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (static_cast<int> (seconds * 1000));
    }

    struct ClockStats
    {
        int starts = 0, continues = 0, stops = 0, positions = 0, clocks = 0;
        double meanMs = 0, minMs = 0, maxMs = 0, jitterMs = 0; // tick intervals; jitter: standard deviation
        double longestGapMs = 0;
    };

    ClockStats analyse (const std::vector<ClockProbe::Event>& events)
    {
        ClockStats s;
        std::vector<double> intervals;
        std::optional<double> lastClock;

        for (auto& e : events)
        {
            const auto& m = e.message;

            if (m.isMidiStart())                s.starts++;
            else if (m.isMidiContinue())        s.continues++;
            else if (m.isMidiStop())            s.stops++;
            else if (m.isSongPositionPointer()) s.positions++;
            else if (m.isMidiClock())
            {
                s.clocks++;

                if (lastClock)
                    intervals.push_back (e.ms - *lastClock);

                lastClock = e.ms;
            }
        }

        if (intervals.empty())
            return s;

        // Skip the first beat: a receiver locks on to the tempo from there.
        const auto settled = std::vector<double> (intervals.begin() + std::min<size_t> (24, intervals.size() - 1), intervals.end());
        double sum = 0, sq = 0;

        for (auto i : settled)
            sum += i;

        s.meanMs = sum / static_cast<double> (settled.size());

        for (auto i : settled)
            sq += (i - s.meanMs) * (i - s.meanMs);

        s.jitterMs = std::sqrt (sq / static_cast<double> (settled.size()));
        s.minMs = *std::min_element (settled.begin(), settled.end());
        s.maxMs = *std::max_element (settled.begin(), settled.end());
        s.longestGapMs = *std::max_element (intervals.begin(), intervals.end());
        return s;
    }

    void print (const char* what, const std::vector<ClockProbe::Event>& events, const ClockStats& s)
    {
        std::cout << what << ": " << s.starts << " start, " << s.continues << " continue, " << s.positions << " position, "
                  << s.stops << " stop, " << s.clocks << " clocks; interval mean " << s.meanMs << " ms, min " << s.minMs
                  << ", max " << s.maxMs << ", jitter " << s.jitterMs << ", longest gap " << s.longestGapMs << "\n";

        if (events.empty())
            return;

        const auto t0 = events.front().ms;

        for (size_t i = 0; i < std::min<size_t> (events.size(), 40); ++i)
        {
            const auto& m = events[i].message;
            std::cout << "  " << (events[i].ms - t0) << " ms  "
                      << (m.isMidiClock() ? "clock" : m.isMidiStart() ? "START" : m.isMidiContinue() ? "CONTINUE"
                          : m.isMidiStop() ? "STOP" : m.isSongPositionPointer() ? "SPP " + juce::String (m.getSongPositionPointerMidiBeat())
                          : m.getDescription()) << "\n";
        }
    }
}

namespace
{
    struct Sent
    {
        double seconds;
        juce::MidiMessage message;
    };

    /** Feeds the generator blocks of 512 samples at 48 kHz, playing at 120 BPM from a position. */
    std::vector<Sent> generate (MidiClockGenerator& generator, double fromPpq, int blocks, bool recording = false, double startSeconds = 0)
    {
        constexpr double blockSeconds = 512.0 / 48000.0;
        constexpr double ppqPerSecond = 2.0;
        std::vector<Sent> sent;

        for (int i = 0; i < blocks; ++i)
        {
            const auto at = startSeconds + i * blockSeconds;
            const auto ppq = fromPpq + i * blockSeconds * ppqPerSecond;
            generator.process ({ ppq, ppq + blockSeconds * ppqPerSecond, blockSeconds, 0, true, recording },
                               [&] (const MidiClockEvent& e) { sent.push_back ({ at + e.seconds, e.toMessage() }); });
        }

        return sent;
    }

    std::vector<double> clockTimes (const std::vector<Sent>& sent)
    {
        std::vector<double> times;

        for (auto& s : sent)
            if (s.message.isMidiClock())
                times.push_back (s.seconds);

        return times;
    }
}

TEST_CASE ("MIDI clock starts at the top and ticks evenly")
{
    MidiClockGenerator generator;
    const auto sent = generate (generator, 0, 187); // just under two seconds

    REQUIRE (sent.size() > 3);
    CHECK (sent[0].message.isSongPositionPointer());
    CHECK (sent[0].message.getSongPositionPointerMidiBeat() == 0);
    CHECK (sent[1].message.isMidiStart());
    CHECK (sent[1].seconds == 0);

    const auto times = clockTimes (sent);
    REQUIRE (times.size() == 96);   // 24 a beat, 2 beats a second

    for (size_t i = 0; i < times.size(); ++i)
        CHECK (times[i] == Catch::Approx (i * 0.5 / 24).margin (1.0e-9));

    // Starting once is enough: no more starts, continues or positions.
    CHECK (std::count_if (sent.begin(), sent.end(), [] (auto& s) { return ! s.message.isMidiClock(); }) == 2);
}

TEST_CASE ("MIDI clock continues from a song position on the next 16th")
{
    MidiClockGenerator generator;
    const auto sent = generate (generator, 1.1, 20); // a 16th is 0.25 quarter notes

    REQUIRE (sent.size() > 3);
    CHECK (sent[0].message.isSongPositionPointer());
    CHECK (sent[0].message.getSongPositionPointerMidiBeat() == 5);
    CHECK (sent[1].message.isMidiContinue());
    CHECK (sent[1].seconds == Catch::Approx ((1.25 - 1.1) * 0.5).margin (1.0e-9));
    CHECK (sent[2].message.isMidiClock());

    SECTION ("but starts when recording, so hardware plays its pattern from the top")
    {
        MidiClockGenerator recordingGenerator;
        const auto recorded = generate (recordingGenerator, 4, 20, true);

        REQUIRE (recorded.size() > 2);
        CHECK (recorded[0].message.getSongPositionPointerMidiBeat() == 16);
        CHECK (recorded[1].message.isMidiStart());
        CHECK (recorded[1].seconds == 0);
    }
}

TEST_CASE ("MIDI clock stops, and stops and starts again after a jump")
{
    MidiClockGenerator generator;
    generate (generator, 0, 10);

    std::vector<Sent> sent;
    auto collect = [&] (const MidiClockEvent& e) { sent.push_back ({ e.seconds, e.toMessage() }); };

    // Jumping back to the top while playing.
    generator.process ({ 0, 0.0213, 0.0107, 0, true, false }, collect);
    REQUIRE (sent.size() >= 3);
    CHECK (sent[0].message.isMidiStop());
    CHECK (sent[1].message.isSongPositionPointer());
    CHECK (sent[2].message.isMidiStart());

    sent.clear();
    generator.process ({ 0.0213, 0.0213, 0.0107, 0, false, false }, collect);
    generator.process ({ 0.0213, 0.0213, 0.0107, 0, false, false }, collect);
    REQUIRE (sent.size() == 1);
    CHECK (sent[0].message.isMidiStop());
    CHECK_FALSE (generator.isRunning());
}

TEST_CASE ("MIDI clock is steady from the first beat", "[.][hardware]")
{
    auto& engine = test::getEngine();
    ClockProbe probe;

    engine.getDeviceManager().rescanMidiDeviceList();
    pump (0.5);
    engine.getDeviceManager().dispatchPendingUpdates();

    auto dir = test::tempDir() / "clock";
    std::filesystem::remove_all (dir);
    auto session = Session::open (dir);

    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "clock-recording.json");
    live.openDefaultAudioDevice();

    auto device = engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    REQUIRE (device != nullptr);
    std::cout << "Audio device: " << device->getName() << ", " << device->getCurrentBufferSizeSamples() << " samples at "
              << device->getCurrentSampleRate() << " Hz\n";

    live.setMidiClock ("GrooveKit Clock Probe", true);
    pump (0.5);
    probe.take();

    // 120 BPM: 24 clocks a beat, 20.83 ms apart.
    SECTION ("Playing")
    {
        live.setPosition (0);
        live.play();
        pump (4);
        live.stop();
        pump (0.3);

        auto events = probe.take();
        auto s = analyse (events);
        print ("play", events, s);

        CHECK (s.starts == 1);
        CHECK (s.continues == 0);
        CHECK (s.jitterMs < 1.5);
        CHECK (s.longestGapMs < 25);
    }

    SECTION ("Recording")
    {
        auto inputs = live.getInputNames();
        REQUIRE_FALSE (inputs.empty());

        live.setPosition (0);
        live.startRecording ({ { inputs[0], "master", 0 } }, { .addClips = false });
        pump (4);
        live.stopRecording();
        pump (0.3);

        auto events = probe.take();
        auto s = analyse (events);
        print ("record", events, s);

        CHECK (s.starts == 1);
        CHECK (s.continues == 0);
        CHECK (s.jitterMs < 1.5);
        CHECK (s.longestGapMs < 25);
    }

    live.setMidiClock ("GrooveKit Clock Probe", false);
}

// Plays the Digitakt II from GrooveKit's clock and records its track 1 (a kick or any clean hits on
// 16ths): the hits should sit evenly on the grid from the very first one. gk_tests "[digitakt]"
TEST_CASE ("The Digitakt follows the clock evenly", "[.][digitakt]")
{
    auto& engine = test::getEngine();
    auto dir = test::tempDir() / "digitakt";
    std::filesystem::remove_all (dir);
    auto session = Session::open (dir);

    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "digitakt-recording.json");
    live.openDefaultAudioDevice();

    auto& dm = engine.getDeviceManager().deviceManager;
    auto setup = dm.getAudioDeviceSetup();
    setup.inputDeviceName = setup.outputDeviceName = "Digitakt II";
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = true;
    REQUIRE (dm.setAudioDeviceSetup (setup, true).isEmpty());
    live.pairStereoInputs();
    engine.getDeviceManager().rescanMidiDeviceList();
    pump (0.5);
    engine.getDeviceManager().dispatchPendingUpdates();
    live.setMidiClock ("Elektron Digitakt II", true);
    pump (2); // device changes restart Tracktion's transports, which would cut the take

    const int bars = 8;
    live.setPosition (0);
    live.startRecording ({ { "Track 1", "master", 0 } }, { .length = bars * 3840, .normalizeLufs = {} });

    for (auto start = juce::Time::getMillisecondCounter(); live.isRecording() && juce::Time::getMillisecondCounter() - start < 30000;)
        pump (0.02);

    REQUIRE_FALSE (live.isRecording());

    auto& clips = session->getProject().playlist.clips;
    REQUIRE (clips.size() == 1);
    const auto take = std::get<model::AudioClip> (clips.front());
    const auto& sample = model::getClipLayer (*model::findChannel (session->getProject(), take.channel))->sample;
    const auto audio = analysis::loadAudio (*session->getProjectDir() / sample);
    const auto hits = analysis::detectOnsets (audio, -40.0f, 0.05);
    REQUIRE (hits.size() >= 8);

    // Where each hit lands on the song's timeline, and how far from the nearest 16th.
    const auto step = 0.125;
    const auto clipStart = static_cast<double> (take.start) / 960.0 * 0.5 - take.offset; // 120 BPM; offset is in seconds
    std::vector<double> offsets;

    for (auto t : hits)
    {
        const auto when = clipStart + t;
        offsets.push_back ((when - std::round (when / step) * step) * 1000.0);
        std::cout << "hit at " << when << " s, " << offsets.back() << " ms off the grid\n";
    }

    const auto [lo, hi] = std::minmax_element (offsets.begin(), offsets.end());
    std::cout << "spread " << (*hi - *lo) << " ms over " << hits.size() << " hits\n";

    CHECK (clipStart + hits.front() < 0.05);    // the downbeat of bar 1 is there
    CHECK (*hi - *lo < 3.0);                    // no drift or wobble
}
