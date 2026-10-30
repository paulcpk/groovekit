#pragma once

#include "Handoff.h"

#include "SamplerEngine.h"
#include "model/Project.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk::sampler
{

/** The filter's cutoff, resonance and envelope amount as automatable parameters ("filterCutoff",
    "filterResonance", "filterEnvelopeAmount"): the model's values unless automation moves them. */
class FilterControls
{
public:
    explicit FilterControls (tracktion::Plugin&);
    ~FilterControls();

    /** Takes the model's values. */
    void set (const Filter&);

    /** The filter with the parameters' current values. */
    Filter apply (Filter) const;

    tracktion::AutomatableParameter::Ptr cutoff, resonance, envelopeAmount;

private:
    juce::CachedValue<float> values[3];
};

/** A sampler channel's first layer's gain, pan and envelope and the channel's filter, applied
    to the audio clips on its track.

    Each clip is shaped like a note held for the clip's length: the volume envelope's attack
    starts where the clip starts, and its release ends where the clip ends; the filter's
    cutoff follows its own envelope the same way. Everything is computed from the timeline
    position, so it's the same wherever playback starts, and in renders.
*/
class AudioChannelPlugin : public tracktion::Plugin
{
public:
    AudioChannelPlugin (tracktion::PluginCreationInfo);
    ~AudioChannelPlugin() override;

    static const char* getPluginName()                  { return "GrooveKit Audio Clips"; }
    static const char* xmlTypeName;

    struct Clip { double start, end; };   // seconds on the timeline

    /** Takes new settings (safe while playing). Clips must be sorted by start. */
    void setSettings (const model::SamplerInstrument&, std::vector<Clip>);

    /** New gain, pan, envelope and filter settings for the same clips. */
    void updateInstrument (const model::SamplerInstrument&);

    FilterControls filterControls { *this };


    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Audio"; }
    juce::String getSelectableDescription() override    { return getName(); }
    bool takesAudioInput() override                     { return true; }
    bool takesMidiInput() override                      { return false; }

    int getNumOutputChannelsGivenInputs (int) override  { return 2; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    struct Settings
    {
        float gainL = 1, gainR = 1;
        EnvelopeShape envelope;
        Filter filter;
        std::vector<Clip> clips;
    };

    Handoff<Settings> handoff;
    const Settings* settings = nullptr;   // audio thread
    float filterState[2][4] {};
    FilterStages filterStages;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioChannelPlugin)
};

/** The sampler filter's and modulation's settings from the model. */
Filter toFilter (const model::SamplerFilter&);
EnvelopeShape toEnvelope (const model::Envelope&);
Modulation toModulation (const std::optional<model::SamplerModulation>&);

} // namespace gk::sampler
