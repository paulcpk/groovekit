#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace gk
{

/** Opens hosted plugins' own editors in native windows (Tracktion asks its UIBehaviour for them).

    Built-in effects have no editors; the UI shows their parameters itself.
*/
class PluginWindowBehaviour : public tracktion::UIBehaviour
{
public:
    std::unique_ptr<juce::Component> createPluginWindow (tracktion::PluginWindowState&) override;
    void recreatePluginWindowContentAsync (tracktion::Plugin&) override;

    /** Called on the message thread after the user closes a plugin's window. */
    static inline std::function<void (tracktion::Plugin&)> onWindowClosed;
};

} // namespace gk
