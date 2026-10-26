#pragma once

#include "MidiClock.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk
{

/** Passes the playhead of a live Edit to the MidiClock, block by block. Leaves audio untouched.

    It sits on a track of its own, so mutes and solos in the project don't stop the clock.
*/
class MidiClockPlugin : public tracktion::Plugin
{
public:
    MidiClockPlugin (tracktion::PluginCreationInfo);
    ~MidiClockPlugin() override;

    static const char* getPluginName()                  { return "GrooveKit MIDI Clock"; }
    static const char* xmlTypeName;

    /** Call before the Edit plays. */
    void setClock (std::shared_ptr<MidiClock>);

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Clock"; }
    juce::String getSelectableDescription() override    { return getName(); }
    bool takesAudioInput() override                     { return true; }
    bool takesMidiInput() override                      { return true; }
    bool producesAudioWhenNoAudioInput() override       { return true; } // keeps it processed with nothing on its track

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    std::shared_ptr<MidiClock> clock;
    juce::SpinLock tempoLock;
    std::optional<tracktion::tempo::Sequence> tempo;
    double sampleRate = 48000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiClockPlugin)
};

} // namespace gk
