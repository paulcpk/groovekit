#pragma once

#include "PluginProcessor.h"
#include "EffectEditor.h"

namespace chamber
{

/** The pad with the tail drawn behind it: the early reflections, then the decay falling away. */
class Editor : public gkp::PadEffectEditor
{
public:
    explicit Editor (Processor&);

protected:
    void drawPad (juce::Graphics&, juce::Rectangle<float>) override;
    void drawStatus (juce::Graphics&, juce::Rectangle<int>) override;

private:
    Processor& processor;
};

} // namespace chamber
