#pragma once

#include "AudioChannelPlugin.h"
#include "Processing.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk::sampler
{

/** What a layer works into its sample before it plays: its processing and transient shaping. */
Processing toProcessing (const model::SamplerLayer&);

/** Resolves a model sampler into a playable config, loading every sample. Throws on missing files. */
std::shared_ptr<const Config> buildConfig (const model::SamplerInstrument&, const std::filesystem::path& projectDir);

/** The built-in GrooveKit sampler as a Tracktion plugin.
    The instrument definition is kept in the plugin's state as JSON, so Tracktion can recreate it.
*/
class SamplerPlugin : public tracktion::Plugin
{
public:
    SamplerPlugin (tracktion::PluginCreationInfo);
    ~SamplerPlugin() override;

    static const char* getPluginName()                  { return "GrooveKit Sampler"; }
    static const char* xmlTypeName;

    /** Throws if a sample can't be loaded. */
    void setInstrument (const model::SamplerInstrument&, const std::filesystem::path& projectDir);

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Sampler"; }
    juce::String getSelectableDescription() override    { return getName(); }
    bool isSynth() override                             { return true; }
    bool takesMidiInput() override                      { return true; }
    bool takesAudioInput() override                     { return false; }
    bool producesAudioWhenNoAudioInput() override       { return true; }
    double getTailLength() const override;

    int getNumOutputChannelsGivenInputs (int) override  { return 2; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    FilterControls filterControls { *this };

private:
    SamplerEngine samplerEngine;
    std::vector<SamplerEngine::MidiEvent> events;
    double tailSeconds = 0;

    void restoreFromState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerPlugin)
};

} // namespace gk::sampler
