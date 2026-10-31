#pragma once

#include "Synth.h"
#include "MachineProcessor.h"

namespace groove9
{

const std::vector<gkp::Preset>& getPresets();

class Processor : public rhythm::MachineProcessor
{
public:
    Processor();

    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override        { return 3.0; }

    int noteForRow (int row) const override             { return Synth::noteForRow (row); }
    int rowForNote (int note) const override            { return Synth::rowForNote (note); }

    /** The keys' names, for hosts that show them (a drum editor's rows): each voice on its main
        key, as General MIDI names it. The other keys that play a voice go unnamed. */
    std::optional<juce::String> getNameForMidiNoteNumber (int note, int) override
    {
        static const std::map<int, const char*> names {
            { 36, "Bass Drum" }, { 37, "Rim Shot" }, { 38, "Snare" }, { 39, "Hand Clap" }, { 41, "Low Tom" }, { 42, "Closed Hat" }, { 45, "Mid Tom" },
            { 46, "Open Hat" }, { 48, "High Tom" }, { 49, "Crash" }, { 51, "Ride" }
        };

        if (auto it = names.find (note); it != names.end())
            return juce::String (it->second);

        return std::nullopt;
    }

private:
    Synth synth;

    void prepareVoices (double sampleRate) override;
    void renderVoices (juce::AudioBuffer<float>&, const juce::MidiBuffer&) override;
    Settings readSettings() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

} // namespace groove9
