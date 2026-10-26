#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace gk::mixer
{

/** Volume in dB and balance pan for channels and inserts.

    Pan uses a balance law: centre leaves both channels at unity, hard left keeps the
    left channel at unity and silences the right. Changes are smoothed over 20 ms;
    the first block after initialise starts at the target values.
*/
class FaderPlugin : public tracktion::Plugin
{
public:
    FaderPlugin (tracktion::PluginCreationInfo);
    ~FaderPlugin() override;

    static const char* getPluginName()                  { return "GrooveKit Fader"; }
    static const char* xmlTypeName;

    static constexpr float minDb = -100.0f;
    static constexpr float maxDb = 24.0f;

    void setVolumeDb (float);
    void setPan (float);

    /** The highest output sample levels (linear) since the last call, for meters. */
    std::pair<float, float> takePeaks();

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Fader"; }
    juce::String getSelectableDescription() override    { return getName(); }
    bool takesAudioInput() override                     { return true; }
    bool takesMidiInput() override                      { return true; }

    int getNumOutputChannelsGivenInputs (int) override  { return 2; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

    juce::CachedValue<float> volumeValue, panValue;
    tracktion::AutomatableParameter::Ptr volumeParam, panParam;

private:
    juce::SmoothedValue<float> leftGain, rightGain;
    std::atomic<float> peakLeft { 0.0f }, peakRight { 0.0f };

    std::pair<float, float> getTargetGains() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaderPlugin)
};

} // namespace gk::mixer
