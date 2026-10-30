#pragma once

#include "PluginProcessor.h"
#include "EffectEditor.h"

namespace echo
{

/** The pad with the repeats drawn behind it: each one a bar where it falls, as tall as it's loud. */
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

} // namespace echo
