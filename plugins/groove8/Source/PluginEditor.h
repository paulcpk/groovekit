#pragma once

#include "PluginProcessor.h"
#include "MachineEditor.h"

namespace groove8
{

/** The panel: the original's layout in GrooveKit's own colours, a midnight-blue panel with
    bone keys and glacier-teal lights. */
class Editor : public rhythm::MachineEditor
{
public:
    explicit Editor (Processor&);
};

} // namespace groove8
