#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace gk::mixer
{

class EffectMixBlend;

/** Sits before an effect in an insert and keeps what goes into it: the dry signal its
    EffectMixBlend mixes back in. */
class EffectMixTap : public tracktion::Plugin
{
public:
    EffectMixTap (tracktion::PluginCreationInfo);
    ~EffectMixTap() override;

    static const char* getPluginName()                  { return "GrooveKit Effect Mix Tap"; }
    static const char* xmlTypeName;

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Dry"; }
    juce::String getSelectableDescription() override    { return getName(); }
    bool takesAudioInput() override                     { return true; }
    bool takesMidiInput() override                      { return true; }

    int getNumOutputChannelsGivenInputs (int) override  { return 2; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    friend class EffectMixBlend;

    /** Long enough for the latency of any effect (about 2.7 s at 48 kHz) plus a block. */
    static constexpr int ringSize = 1 << 17;

    juce::AudioBuffer<float> ring { 2, ringSize };
    int writePosition = 0;
    int blockStart = 0;   // where this block's input starts in the ring

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectMixTap)
};

/** Sits after an effect and mixes its output with the dry signal kept by the tap before it,
    delayed by the effect's latency so the two line up. Works the same for built-in and hosted
    effects. Also keeps the last few thousand samples of what it outputs, for the UI's scopes. */
class EffectMixBlend : public tracktion::Plugin
{
public:
    EffectMixBlend (tracktion::PluginCreationInfo);
    ~EffectMixBlend() override;

    static const char* getPluginName()                  { return "GrooveKit Effect Mix"; }
    static const char* xmlTypeName;

    /** Pairs it with its tap and the effect between them. */
    void setup (EffectMixTap&, tracktion::Plugin& effect);

    /** 0 is the dry signal only, 1 only the effect's output. */
    void setMix (float);

    /** The latest output, mono, oldest first (up to scopeSize samples). */
    std::vector<float> readScope (int numSamples) const;
    static constexpr int scopeSize = 8192;

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Mix"; }
    juce::String getSelectableDescription() override    { return getName(); }
    bool takesAudioInput() override                     { return true; }
    bool takesMidiInput() override                      { return true; }

    int getNumOutputChannelsGivenInputs (int) override  { return 2; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    juce::CachedValue<float> mixValue;
    tracktion::AutomatableParameter::Ptr mixParam;

private:
    EffectMixTap* tap = nullptr;
    tracktion::Plugin::Ptr effect;
    juce::SmoothedValue<float> mix;

    std::vector<float> scope = std::vector<float> (scopeSize);
    std::atomic<int> scopePosition { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectMixBlend)
};

} // namespace gk::mixer
