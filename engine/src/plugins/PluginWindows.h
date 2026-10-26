#pragma once

#include "model/Project.h"
#include "model/Protocol.generated.h"

#include <tracktion_engine/tracktion_engine.h>

namespace gk
{

/** What the bar above a plugin's editor needs from the rest of the engine. */
struct PluginWindowHost
{
    /** "channel:<id>" or "effect:<insert>/<effect>"; empty for a plugin that isn't in the project. */
    std::function<std::string (tracktion::Plugin&)> findKey;
    std::function<const model::Project* ()> getProject;

    /** Runs commands as the UI's controls do: a gesture's changes share a group, so one undo reverts it. */
    std::function<void (const std::vector<model::Command>&, const std::string& label, const std::string& group)> execute;

    /** Asks the UI to make an automation clip for a parameter, starting at a value. */
    std::function<void (const model::AutomationTarget&, double value)> createAutomation;
};

/** Opens hosted plugins' own editors in native windows (Tracktion asks its UIBehaviour for them),
    with a bar on top: for an instrument, the channel's mixer insert, volume and pan; for an
    effect, its mix; for both, a button that automates the parameter last moved in the editor.

    Built-in effects have no editors; the UI shows their parameters itself.
*/
class PluginWindowBehaviour : public tracktion::UIBehaviour
{
public:
    std::unique_ptr<juce::Component> createPluginWindow (tracktion::PluginWindowState&) override;
    void recreatePluginWindowContentAsync (tracktion::Plugin&) override;

    /** Called on the message thread after the user closes a plugin's window. */
    static inline std::function<void (tracktion::Plugin&)> onWindowClosed;

    static inline PluginWindowHost host;
};

} // namespace gk
