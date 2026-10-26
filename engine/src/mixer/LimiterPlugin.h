#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <deque>

namespace gk::mixer
{

/** Lookahead brickwall limiter for mastering.

    Input gain drives the signal into the ceiling; peaks above it are caught by a gain
    envelope that starts falling one lookahead period before them, so no sample exceeds
    the ceiling. Channels are linked. The lookahead is reported as latency.
*/
class LimiterPlugin : public tracktion::Plugin
{
public:
    LimiterPlugin (tracktion::PluginCreationInfo);
    ~LimiterPlugin() override;

    static const char* getPluginName()                  { return "GrooveKit Limiter"; }
    static const char* xmlTypeName;

    static constexpr double lookaheadSeconds = 0.005;

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Limiter"; }
    juce::String getSelectableDescription() override    { return getName(); }
    bool takesAudioInput() override                     { return true; }
    bool takesMidiInput() override                      { return false; }
    double getLatencySeconds() override                 { return lookaheadSeconds; }
    double getTailLength() const override               { return lookaheadSeconds; }

    int getNumOutputChannelsGivenInputs (int) override  { return 2; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    juce::CachedValue<float> inputGainValue, ceilingValue, releaseValue;
    tracktion::AutomatableParameter::Ptr inputGainParam, ceilingParam, releaseParam;

private:
    int lookahead = 240;
    std::vector<float> delayL, delayR, delayGain;
    int writeIndex = 0;
    int64_t sampleCount = 0;
    std::deque<std::pair<int64_t, float>> window;   // (sample index, required gain), increasing gains
    float envelope = 1.0f;
    double releaseCoefficient = 0;
    float lastReleaseMs = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LimiterPlugin)
};

} // namespace gk::mixer
