#pragma once

#include "model/Project.h"

#include <tracktion_engine/tracktion_engine.h>

#include <filesystem>
#include <map>

namespace gk::sampler
{

/** How a multi-sampler's layers sound together. Each layer is first matched in loudness (the
    loudest hit, or a loop's level over its length), so a mix of 1 is as loud for every layer;
    then the layers are summed as they play (hits at their offsets, loops looping at their speeds)
    and trimmed so the sum is no louder than the loudest layer alone, nor than -1 dBFS. Worked out from the samples
    themselves and remembered, since the same stack is mixed again on every change. */
struct StackMix
{
    struct Layer
    {
        double loudnessDb = -100;   // the sample's, as measured
        double matchDb = 0;         // what brings it to the loudest layer's loudness
        double gainDb = -100;       // match and mix together (-100 when it's silent: muted, or not soloed)
    };

    std::map<model::Id, Layer> layers;
    double trimDb = 0;              // on the summed layers
    double peak = 0;                // the summed layers' peak after the trim (linear), for the drive
};

/** A loop layer's playback: how much faster than the file it plays, and how long one time
    round lasts in the song (seconds). */
struct LoopTiming
{
    double rate = 1;
    double cycle = 0;
};

/** Loops: each layer with a sourceTempo plays at the song's tempo (times its speed); one without
    lasts as long as the master (divided by its speed). Layers without a sample are left out. */
std::map<model::Id, LoopTiming> loopTimings (const model::StackInstrument&, const std::filesystem::path& projectDir, double songTempo);

StackMix mixStack (const model::StackInstrument&, const std::filesystem::path& projectDir, double songTempo);

/** The gain (dB) a layer's mix comes to at equal loudness: for automation of "mix:<layer>". */
double stackLayerDb (const StackMix::Layer&, double mix);

/** Offsets (ms, none below 0) that line every hit layer's attack up with the latest one's. */
std::map<model::Id, double> alignStack (const model::StackInstrument&, const std::filesystem::path& projectDir);

//==============================================================================
/** A multi-sampler's output: the trim that keeps the summed layers as loud as the loudest alone,
    drive (saturation that keeps the peak where it was) and a soft ceiling just under full scale. */
class StackOutputPlugin : public tracktion::Plugin
{
public:
    StackOutputPlugin (tracktion::PluginCreationInfo);
    ~StackOutputPlugin() override;

    static const char* getPluginName()                  { return "GrooveKit Multi-sampler Output"; }
    static const char* xmlTypeName;

    void setMix (double trimDb, double peak, double drive);

    juce::String getName() const override               { return getPluginName(); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "Stack"; }
    juce::String getSelectableDescription() override    { return getName(); }
    bool takesAudioInput() override                     { return true; }
    bool takesMidiInput() override                      { return true; }

    int getNumOutputChannelsGivenInputs (int) override  { return 2; }
    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;

private:
    std::atomic<float> trim { 1.0f }, peak { 1.0f }, drive { 0.0f };
    juce::SmoothedValue<float> trimGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StackOutputPlugin)
};

} // namespace gk::sampler
