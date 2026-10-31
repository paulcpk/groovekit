#pragma once

#include "GrooveKitMachine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace rhythm
{

/** The pattern GrooveKit shows a machine: its own copy of what came through showPattern. */
struct HostPattern
{
    bool exists = false;
    juce::String name;
    int stepTicks = 240, lengthSteps = 16, stepsPerBar = 16;
    float swing = 0;
    std::vector<gkmachine::Hit> hits;
};

/** A machine's side of the link with GrooveKit (see GrooveKitMachine.h): what GrooveKit shows it,
    and the host to send edits to while connected. Message thread only, except linked. */
class HostLink : public gkmachine::IMachine, private juce::AsyncUpdater, private juce::AudioProcessorValueTreeState::Listener
{
public:
    /** Follows the Scale parameter, to tell GrooveKit the machine's step length. */
    explicit HostLink (juce::AudioProcessorValueTreeState&);
    ~HostLink() override;

    /** Whether the machine runs inside GrooveKit's engine: it then never plays patterns of its
        own, connected or not (an export renders a copy the engine doesn't connect). */
    static bool isInsideGrooveKit();

    /** The step length in ticks (960 per quarter) for a Scale choice. */
    static int ticksForScale (int scale);

    bool isLinked() const                       { return host != nullptr; }
    gkmachine::IHost* getHost() const           { return host; }
    const HostPattern& getPattern() const       { return pattern; }
    int getPlayStep() const                     { return playStep; }
    bool isHostPlaying() const                  { return hostPlaying; }

    /** Goes up whenever anything above changes, so the editor knows to repaint. */
    uint32_t getChangeCount() const             { return changes; }

    /** For the audio thread: whether GrooveKit is connected. */
    std::atomic<bool> linked { false };

    //==============================================================================
    Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID, void**) override;
    Steinberg::uint32 PLUGIN_API addRef() override      { return 1; }
    Steinberg::uint32 PLUGIN_API release() override     { return 1; }

    gkmachine::int32 PLUGIN_API getVersion() override   { return gkmachine::version; }
    void PLUGIN_API setHost (gkmachine::IHost*) override;
    void PLUGIN_API showPattern (const gkmachine::Pattern&) override;
    void PLUGIN_API setPlayhead (gkmachine::int32 step, gkmachine::int32 playing) override;
    void PLUGIN_API showNotes (const gkmachine::Note*, gkmachine::int32) override    {}    // the hits say it all

    /** Answers GrooveKit's query on the machine's VST3 component. */
    struct Extensions : public juce::VST3ClientExtensions
    {
        explicit Extensions (HostLink& l) : link (l) {}
        int32_t queryIAudioProcessor (const Steinberg::TUID, void** obj) override;
        HostLink& link;
    };

private:
    juce::AudioProcessorValueTreeState& state;
    gkmachine::IHost* host = nullptr;
    HostPattern pattern;
    int playStep = -1;
    bool hostPlaying = false;
    uint32_t changes = 0;

    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    void sendStepTicks();

    JUCE_DECLARE_NON_COPYABLE (HostLink)
};

} // namespace rhythm
