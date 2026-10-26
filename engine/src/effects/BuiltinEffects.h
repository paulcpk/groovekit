#pragma once

#include "model/Project.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk::effects
{

/** One parameter of a built-in effect, in GrooveKit's own names and units. */
struct ParamInfo
{
    const char* id;
    const char* unit;
    double min, max, defaultValue;
    const char* description;

    /** Where the value goes: a Tracktion automatable parameter, or a plain state property. */
    const char* tracktionParam = nullptr;
    const char* stateProperty = nullptr;

    /** Converts from GrooveKit units to the Tracktion parameter's units. */
    float (*toNative) (float) = nullptr;

    bool isAutomatable() const { return tracktionParam != nullptr; }
};

struct EffectInfo
{
    model::BuiltinEffectKind kind;
    const char* tracktionType;
    const char* description;
    std::vector<ParamInfo> params;

    const ParamInfo* findParam (const std::string& id) const;
};

const std::vector<EffectInfo>& getEffectCatalogue();
const EffectInfo& getEffectInfo (model::BuiltinEffectKind);

/** The catalogue as JSON, for the CLI and agent tools. */
model::Json catalogueToJson();

/** Creates and configures the Tracktion plugin for an effect. Throws on unknown or out-of-range parameters. */
tracktion::Plugin::Ptr createEffect (tracktion::Edit&, const model::BuiltinEffect&);

/** Sets an existing effect plugin's parameters and bypass from the model. Throws like createEffect. */
void applyParams (tracktion::Plugin&, const model::BuiltinEffect&);

/** The automatable parameter behind a GrooveKit parameter id. Throws if it doesn't exist or can't be automated. */
std::pair<tracktion::AutomatableParameter::Ptr, const ParamInfo*>
    findAutomatableParam (tracktion::Plugin&, model::BuiltinEffectKind, const std::string& paramId);

} // namespace gk::effects
