#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>

namespace gkp
{

/** A factory sound: parameter ids to values in their own units (the rest take defaults). Named
    "Category: Name", so the preset list groups them. */
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

/** What every GrooveKit plugin has: parameters in a value tree, presets as programs (a preset
    sets every parameter, so nothing of the last sound stays), state with the program in it, a
    scope of the latest output for the editor, and a keyboard state the editor plays into. */
class PresetProcessor : public juce::AudioProcessor
{
public:
    PresetProcessor (const BusesProperties&, const juce::String& name, juce::AudioProcessorValueTreeState::ParameterLayout,
                     const std::vector<Preset>& presets, bool synth);

    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboardState;

    const juce::String getName() const override         { return pluginName; }
    bool acceptsMidi() const override                   { return isSynth; }
    bool producesMidi() const override                  { return false; }
    bool hasEditor() const override                     { return true; }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    int getNumPrograms() override                       { return static_cast<int> (presets.size()); }
    int getCurrentProgram() override                    { return program; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** The latest output, mono, oldest first, for the editor's scope. */
    static constexpr int scopeSize = 2048;
    void readScope (float* destination, int numSamples) const;

    std::atomic<int> activeVoices { 0 };

protected:
    float get (const juce::String& id) const       { return state.getRawParameterValue (id)->load(); }
    int getIndex (const juce::String& id) const    { return juce::roundToInt (get (id)); }
    bool getBool (const juce::String& id) const    { return get (id) >= 0.5f; }

    /** Keeps the block for the scope; call at the end of processBlock. */
    void writeScope (const juce::AudioBuffer<float>&);

    /** The editor's keyboard notes, merged into the block's MIDI. */
    void mergeKeyboard (juce::MidiBuffer&, int numSamples);

private:
    const juce::String pluginName;
    const std::vector<Preset>& presets;
    const bool isSynth;
    int program = 0;
    std::array<float, scopeSize> scope {};
    std::atomic<int> scopePosition { 0 };
};

} // namespace gkp
