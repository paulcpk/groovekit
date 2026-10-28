#pragma once

#include "BuiltinEffects.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk::effects
{

/** Base for GrooveKit's own effects: one automatable parameter per catalogue entry, with the
    catalogue's id, range and default, so applyParams and automation treat them like
    Tracktion's. Subclasses read the values in applyToBuffer. */
class EffectPlugin : public tracktion::Plugin
{
public:
    EffectPlugin (tracktion::PluginCreationInfo, model::BuiltinEffectKind);
    ~EffectPlugin() override;

    juce::String getName() const override               { return info.description; }
    juce::String getSelectableDescription() override    { return getName(); }
    bool takesAudioInput() override                     { return true; }
    bool takesMidiInput() override                      { return true; }
    int getNumOutputChannelsGivenInputs (int) override  { return 2; }

    /** Keyed by another track (a sidechain), its sound comes in on inputs 3 and 4. */
    void getChannelNames (juce::StringArray* ins, juce::StringArray* outs) override;
    void deinitialise() override {}

    /** What it measures for the UI (see `mixer.meter`): nothing unless the effect overrides it.
        `params` are the call's, such as `since` for a MeterHistory. The message thread. */
    virtual model::Json readMeter (const model::Json& params) const   { (void) params; return model::Json::object(); }

protected:
    const EffectInfo& info;

    /** The parameter's current value (automation included), by its index in the catalogue. */
    float value (size_t index) const    { return parameters[index]->getCurrentValue(); }

    /** The current value of a choice parameter, as an index into its choices. */
    int choice (size_t index) const     { return juce::roundToInt (value (index)); }

private:
    std::vector<tracktion::AutomatableParameter::Ptr> parameters;
    std::vector<std::unique_ptr<juce::CachedValue<float>>> values;
};

} // namespace gk::effects
