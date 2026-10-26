#include "EffectPlugin.h"

namespace gk::effects
{

EffectPlugin::EffectPlugin (tracktion::PluginCreationInfo creation, model::BuiltinEffectKind kind)
    : Plugin (creation), info (getEffectInfo (kind))
{
    for (auto& p : info.params)
    {
        auto& v = *values.emplace_back (std::make_unique<juce::CachedValue<float>>());
        v.referTo (state, p.id, getUndoManager(), static_cast<float> (p.defaultValue));

        auto param = addParam (p.id, p.id, { static_cast<float> (p.min), static_cast<float> (p.max) },
                               [unit = juce::String (p.unit)] (float x) { return juce::String (x, 2) + " " + unit; },
                               [] (const juce::String& s) { return s.getFloatValue(); });
        param->attachToCurrentValue (v);
        parameters.push_back (param);
    }
}

EffectPlugin::~EffectPlugin()
{
    notifyListenersOfDeletion();

    for (auto& p : parameters)
        p->detachFromCurrentValue();
}

} // namespace gk::effects
