#include "BuiltinEffects.h"

#include "EqPlugin.h"
#include "mixer/LimiterPlugin.h"

#include <deque>

namespace gk::effects
{

using model::BuiltinEffectKind;

namespace
{
    float dbToGain (float db)         { return juce::Decibels::decibelsToGain (db); }
    float inverse (float ratio)       { return 1.0f / ratio; }
    float scaleWet (float level)      { return level / 3.0f; }  // Tracktion's reverb scales wet by 3 and dry by 2
    float scaleDry (float level)      { return level / 2.0f; }

    /** Keeps generated parameter names alive for the catalogue's const char* ids. */
    const char* keep (std::string s)
    {
        static std::deque<std::string> names;
        return names.emplace_back (std::move (s)).c_str();
    }

    EffectInfo eqInfo()
    {
        EffectInfo info { BuiltinEffectKind::eq, EqPlugin::xmlTypeName,
                          "Seven-band parametric EQ. Each band is off, lowCut or highCut (24 dB/octave, Q shapes the knee), "
                          "lowShelf or highShelf, peak or notch. Bands default to shelves at 80 Hz and 12 kHz with peaks between, all flat.",
                          {} };

        for (int b = 0; b < EqPlugin::numBands; ++b)
        {
            const auto n = "band" + std::to_string (b + 1);
            const auto& d = EqPlugin::bandDefaults[b];
            const auto id = [&] (const char* what) { return keep (n + what); };

            info.params.push_back ({ id ("Type"), "", 0, 6, static_cast<double> (d.type), keep ("Band " + std::to_string (b + 1) + " type: an index into the choices."),
                                     id ("Type"), nullptr, nullptr, EqPlugin::bandTypeNames() });
            info.params.push_back ({ id ("Freq"), "Hz", 20, 20000, d.freq, "Frequency: the centre of a peak or notch, the corner of a shelf or cut.", id ("Freq") });
            info.params.push_back ({ id ("GainDb"), "dB", -24, 24, 0, "Gain of a peak or shelf; cuts and notches ignore it.", id ("GainDb") });
            info.params.push_back ({ id ("Q"), "", 0.1, 18, d.q, "Width of a peak or notch (higher is narrower); slope of a shelf; resonance of a cut.", id ("Q") });
        }

        return info;
    }

    std::vector<EffectInfo> buildCatalogue()
    {
        return {
            eqInfo(),

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
        {
            params[p.id] = { { "unit", p.unit }, { "min", p.min }, { "max", p.max }, { "default", p.defaultValue },
                             { "automatable", p.isAutomatable() }, { "description", p.description } };

            if (! p.choices.empty())
                params[p.id]["choices"] = p.choices;
        }

        effects[model::toString (e.kind)] = { { "description", e.description }, { "params", params } };

        if (e.defaultMix < 1.0)
            effects[model::toString (e.kind)]["defaultMix"] = e.defaultMix;
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
