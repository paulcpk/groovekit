#pragma once

#include "PluginProcessor.h"
#include "MachineEditor.h"

namespace groove9
{

/** The panel: the original's layout in GrooveKit's own colours, dark anodised metal with one
    acid-lime accent. */
class Editor : public rhythm::MachineEditor
{
public:
    explicit Editor (Processor&);
};

} // namespace groove9
