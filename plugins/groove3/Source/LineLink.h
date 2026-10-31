#pragma once

#include "GrooveKitMachine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace groove3
{

/** Groove-3's side of the link with GrooveKit (see GrooveKitMachine.h): the pattern and the notes
    GrooveKit shows it, and the host its step section's edits go to while connected. It counts in
    sixteenths. Message thread only, except linked. */
class LineLink : public gkmachine::IMachine
{
public:
    LineLink() = default;
    ~LineLink();

    /** The pattern shown: its name, whether there is one, its length and a bar in sixteenths. */
    struct Shown
    {
        bool exists = false;
        juce::String name;
        int lengthSteps = 16, stepsPerBar = 16;
    };

    bool isLinked() const                               { return host != nullptr; }
    gkmachine::IHost* getHost() const                   { return host; }
    const Shown& getPattern() const                     { return pattern; }
    const std::vector<gkmachine::Note>& getNotes() const { return notes; }
    int getPlayStep() const                             { return playStep; }
    bool isHostPlaying() const                          { return hostPlaying; }

    /** Goes up whenever anything above changes, so the editor knows to repaint. */
    uint32_t getChangeCount() const                     { return changes; }

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
    void PLUGIN_API showNotes (const gkmachine::Note*, gkmachine::int32) override;

    /** Answers GrooveKit's query on the plugin's VST3 component. */
    struct Extensions : public juce::VST3ClientExtensions
    {
        explicit Extensions (LineLink& l) : link (l) {}
        int32_t queryIAudioProcessor (const Steinberg::TUID, void** obj) override;
        LineLink& link;
    };

private:
    gkmachine::IHost* host = nullptr;
    Shown pattern;
    std::vector<gkmachine::Note> notes;
    int playStep = -1;
    bool hostPlaying = false;
    uint32_t changes = 0;

    JUCE_DECLARE_NON_COPYABLE (LineLink)
};

} // namespace groove3
