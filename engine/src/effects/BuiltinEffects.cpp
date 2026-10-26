#include "BuiltinEffects.h"

#include "mixer/LimiterPlugin.h"

namespace gk::effects
{

using model::BuiltinEffectKind;

namespace
{
    float dbToGain (float db)         { return juce::Decibels::decibelsToGain (db); }
    float inverse (float ratio)       { return 1.0f / ratio; }
    float scaleWet (float level)      { return level / 3.0f; }  // Tracktion's reverb scales wet by 3 and dry by 2
    float scaleDry (float level)      { return level / 2.0f; }

    std::vector<EffectInfo> buildCatalogue()
    {
        return {
            { BuiltinEffectKind::eq, tracktion::EqualiserPlugin::xmlTypeName,
              "Four-band EQ: low shelf, two peaking bands, high shelf.",
              {
                  // Tracktion calls the shelves "Low-pass" and "High-pass".
                  { "lowFreq", "Hz", 20, 20000, 80, "Low shelf frequency.", "Low-pass freq" },
                  { "lowGainDb", "dB", -20, 20, 0, "Low shelf gain.", "Low-pass gain" },
                  { "lowQ", "", 0.1, 4, 0.5, "Low shelf slope.", "Low-pass Q" },
                  { "mid1Freq", "Hz", 20, 20000, 3000, "First peak frequency.", "Mid freq 1" },
                  { "mid1GainDb", "dB", -20, 20, 0, "First peak gain.", "Mid gain 1" },
                  { "mid1Q", "", 0.1, 4, 0.5, "First peak width; higher is narrower.", "Mid Q 1" },
                  { "mid2Freq", "Hz", 20, 20000, 5000, "Second peak frequency.", "Mid freq 2" },
                  { "mid2GainDb", "dB", -20, 20, 0, "Second peak gain.", "Mid gain 2" },
                  { "mid2Q", "", 0.1, 4, 0.5, "Second peak width; higher is narrower.", "Mid Q 2" },
                  { "highFreq", "Hz", 20, 20000, 17000, "High shelf frequency.", "High-pass freq" },
                  { "highGainDb", "dB", -20, 20, 0, "High shelf gain.", "High-pass gain" },
                  { "highQ", "", 0.1, 4, 0.5, "High shelf slope.", "High-pass Q" },
              } },

            { BuiltinEffectKind::compressor, tracktion::CompressorPlugin::xmlTypeName,
              "Feed-forward compressor.",
              {
                  { "thresholdDb", "dB", -40, 0, -6, "Level above which gain is reduced.", "threshold", nullptr, dbToGain },
                  { "ratio", ":1", 1, 20, 2, "Compression ratio.", "ratio", nullptr, inverse },
                  { "attackMs", "ms", 0.3, 200, 100, "Attack time.", "attack" },
                  { "releaseMs", "ms", 10, 300, 100, "Release time.", "release" },
                  { "outputDb", "dB", -10, 24, 0, "Make-up gain.", "output gain" },
              } },

            { BuiltinEffectKind::limiter, mixer::LimiterPlugin::xmlTypeName,
              "Lookahead brickwall limiter (5 ms latency): no sample exceeds the ceiling. Put it last on master.",
              {
                  { "inputGainDb", "dB", -36, 36, 0, "Gain before limiting; raises loudness.", "inputGain" },
                  { "ceilingDb", "dB", -12, 0, -1, "Maximum output level.", "ceiling" },
                  { "releaseMs", "ms", 1, 1000, 100, "How fast gain recovers after a peak.", "release" },
              } },

            { BuiltinEffectKind::reverb, tracktion::ReverbPlugin::xmlTypeName,
              "Algorithmic room reverb.",
              {
                  { "roomSize", "", 0, 1, 0.3, "Room size.", "room size" },
                  { "damping", "", 0, 1, 0.5, "High-frequency damping.", "damping" },
                  { "wet", "gain", 0, 3, 1, "Reverb level (linear gain).", "wet level", nullptr, scaleWet },
                  { "dry", "gain", 0, 2, 1, "Direct signal level (linear gain).", "dry level", nullptr, scaleDry },
                  { "width", "", 0, 1, 1, "Stereo width.", "width" },
              } },

            { BuiltinEffectKind::delay, tracktion::DelayPlugin::xmlTypeName,
              "Mono feedback delay.",
              {
                  { "timeMs", "ms", 1, 4000, 150, "Delay time. Not automatable.", nullptr, "length" },
                  { "feedbackDb", "dB", -30, 0, -6, "Feedback level.", "feedback" },
                  { "mix", "", 0, 1, 0.3, "Wet proportion.", "mix proportion" },
              } },

            { BuiltinEffectKind::phaser, tracktion::PhaserPlugin::xmlTypeName,
              "Phaser. Parameters are not automatable.",
              {
                  { "depth", "", 0, 10, 5, "Sweep depth.", nullptr, "depth" },
                  { "rate", "Hz", 0, 10, 0.4, "Sweep rate.", nullptr, "rate" },
                  { "feedback", "", 0, 0.99, 0.7, "Feedback amount.", nullptr, "feedback" },
              } },

            { BuiltinEffectKind::chorus, tracktion::ChorusPlugin::xmlTypeName,
              "Chorus. Parameters are not automatable.",
              {
                  { "depthMs", "ms", 0.1, 20, 3, "Modulation depth.", nullptr, "depthMs" },
                  { "speedHz", "Hz", 0.1, 10, 1, "Modulation rate.", nullptr, "speedHz" },
                  { "width", "", 0, 1, 0.5, "Stereo width.", nullptr, "width" },
                  { "mix", "", 0, 1, 0.5, "Wet proportion.", nullptr, "mixProportion" },
              } },

            { BuiltinEffectKind::lowpass, tracktion::LowPassPlugin::xmlTypeName,
              "Low-pass filter.",
              {
                  { "frequency", "Hz", 10, 22000, 4000, "Cutoff frequency.", "frequency" },
              } },

            { BuiltinEffectKind::highpass, tracktion::LowPassPlugin::xmlTypeName,
              "High-pass filter.",
              {
                  { "frequency", "Hz", 10, 22000, 4000, "Cutoff frequency.", "frequency" },
              } },
        };
    }

    std::string listParams (const EffectInfo& info, bool automatableOnly)
    {
        std::string s;

        for (auto& p : info.params)
            if (! automatableOnly || p.isAutomatable())
                s += (s.empty() ? "" : ", ") + std::string (p.id);

        return s.empty() ? "none" : s;
    }
}

const ParamInfo* EffectInfo::findParam (const std::string& id) const
{
    for (auto& p : params)
        if (id == p.id)
            return &p;

    return nullptr;
}

const std::vector<EffectInfo>& getEffectCatalogue()
{
    static const auto catalogue = buildCatalogue();
    return catalogue;
}

const EffectInfo& getEffectInfo (BuiltinEffectKind kind)
{
    for (auto& e : getEffectCatalogue())
        if (e.kind == kind)
            return e;

    throw std::logic_error ("Effect missing from catalogue");
}

model::Json catalogueToJson()
{
    auto effects = model::Json::object();

    for (auto& e : getEffectCatalogue())
    {
        auto params = model::Json::object();

        for (auto& p : e.params)
            params[p.id] = { { "unit", p.unit }, { "min", p.min }, { "max", p.max }, { "default", p.defaultValue },
                             { "automatable", p.isAutomatable() }, { "description", p.description } };

        effects[model::toString (e.kind)] = { { "description", e.description }, { "params", params } };
    }

    return effects;
}

tracktion::Plugin::Ptr createEffect (tracktion::Edit& edit, const model::BuiltinEffect& effect)
{
    auto& info = getEffectInfo (effect.kind);
    auto plugin = edit.getPluginCache().createNewPlugin (info.tracktionType, {});

    if (plugin == nullptr)
        throw std::runtime_error (std::string ("Couldn't create effect ") + model::toString (effect.kind));

    if (effect.kind == BuiltinEffectKind::lowpass || effect.kind == BuiltinEffectKind::highpass)
        plugin->state.setProperty ("mode", effect.kind == BuiltinEffectKind::highpass ? "highpass" : "lowpass", nullptr);

    applyParams (*plugin, effect);
    return plugin;
}

void applyParams (tracktion::Plugin& plugin, const model::BuiltinEffect& effect)
{
    auto& info = getEffectInfo (effect.kind);

    for (auto& p : info.params)
    {
        auto it = effect.params.find (p.id);
        const auto value = it != effect.params.end() ? it->second : p.defaultValue;

        if (value < p.min || value > p.max)
            throw std::runtime_error ("Effect \"" + effect.id + "\": " + p.id + " must be between "
                                      + juce::String (p.min).toStdString() + " and " + juce::String (p.max).toStdString());

        const auto native = p.toNative != nullptr ? p.toNative (static_cast<float> (value)) : static_cast<float> (value);

        if (p.tracktionParam != nullptr)
        {
            auto param = plugin.getAutomatableParameterByID (p.tracktionParam);

            if (param == nullptr)
                throw std::logic_error (std::string ("Tracktion parameter missing: ") + p.tracktionParam);

            // With notification, so the attached state values and the plugin's filters update too.
            param->setParameter (native, juce::sendNotification);
        }
        else
        {
            plugin.state.setProperty (p.stateProperty, native, nullptr);
        }
    }

    for (auto& [id, value] : effect.params)
        if (info.findParam (id) == nullptr)
            throw std::runtime_error ("Effect \"" + effect.id + "\": " + model::toString (effect.kind)
                                      + " has no parameter \"" + id + "\" (has: " + listParams (info, false) + ")");

    plugin.setEnabled (! effect.bypass);
}

std::pair<tracktion::AutomatableParameter::Ptr, const ParamInfo*>
    findAutomatableParam (tracktion::Plugin& plugin, BuiltinEffectKind kind, const std::string& paramId)
{
    auto& info = getEffectInfo (kind);
    auto p = info.findParam (paramId);

    if (p == nullptr || ! p->isAutomatable())
        throw std::runtime_error (std::string (model::toString (kind)) + " has no automatable parameter \"" + paramId
                                  + "\" (has: " + listParams (info, true) + ")");

    return { plugin.getAutomatableParameterByID (p->tracktionParam), p };
}

} // namespace gk::effects
