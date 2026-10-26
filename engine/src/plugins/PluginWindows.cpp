#include "PluginWindows.h"

namespace gk
{

namespace te = tracktion;

namespace
{
    /** The plugin's own editor, or JUCE's generic one if it has none. */
    struct EditorContent : te::Plugin::EditorComponent
    {
        explicit EditorContent (te::ExternalPlugin& p) : plugin (p)
        {
            JUCE_AUTORELEASEPOOL
            {
                if (auto instance = plugin.getAudioPluginInstance())
                {
                    editor.reset (instance->createEditorIfNeeded());

                    if (editor == nullptr)
                        editor = std::make_unique<juce::GenericAudioProcessorEditor> (*instance);

                    addAndMakeVisible (*editor);
                }
            }

            fitEditor();
        }

        bool allowWindowResizing() override { return false; }

        juce::ComponentBoundsConstrainer* getBoundsConstrainer() override
        {
            return editor != nullptr ? editor->getConstrainer() : nullptr;
        }

        void resized() override
        {
            if (editor != nullptr)
                editor->setBounds (getLocalBounds());
        }

        void childBoundsChanged (juce::Component* c) override
        {
            if (c == editor.get())
            {
                plugin.edit.pluginChanged (plugin);
                fitEditor();
            }
        }

        void fitEditor()
        {
            setSize (std::max (8, editor != nullptr ? editor->getWidth() : 0), std::max (8, editor != nullptr ? editor->getHeight() : 0));
        }

        te::ExternalPlugin& plugin;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
    };

    class PluginWindow : public juce::DocumentWindow
    {
    public:
        explicit PluginWindow (te::Plugin& p)
            : DocumentWindow (p.getName(), juce::Colours::black, DocumentWindow::closeButton, true), plugin (p)
        {
            getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
            setUsingNativeTitleBar (true);
            recreateEditor();
            setBoundsConstrained (getLocalBounds() + plugin.windowState->choosePositionForPluginWindow());
            trackBounds = true;
        }

        ~PluginWindow() override
        {
            trackBounds = false;
            plugin.edit.flushPluginStateIfNeeded (plugin);
            setEditor (nullptr);
        }

        void recreateEditor()
        {
            setEditor (nullptr);
            setEditor (plugin.createEditor());
        }

        bool hasEditor() const { return editor != nullptr; }

    private:
        te::Plugin& plugin;
        std::unique_ptr<te::Plugin::EditorComponent> editor;
        bool trackBounds = false;

        void setEditor (std::unique_ptr<te::Plugin::EditorComponent> e)
        {
            JUCE_AUTORELEASEPOOL
            {
                setConstrainer (nullptr);
                editor = std::move (e);

                if (editor != nullptr)
                    setContentNonOwned (editor.get(), true);

                setResizable (false, false);
            }
        }

        void moved() override
        {
            if (trackBounds)
                plugin.windowState->lastWindowBounds = getBounds();
        }

        void closeButtonPressed() override
        {
            auto& p = plugin;
            p.windowState->closeWindowExplicitly();

            // After the window is gone, so the handler can rebuild the Edit.
            juce::MessageManager::callAsync ([ref = te::Plugin::Ptr (&p)]
            {
                if (PluginWindowBehaviour::onWindowClosed)
                    PluginWindowBehaviour::onWindowClosed (*ref);
            });
        }

        float getDesktopScaleFactor() const override { return 1.0f; }
    };
}

std::unique_ptr<juce::Component> PluginWindowBehaviour::createPluginWindow (te::PluginWindowState& state)
{
    auto windowState = dynamic_cast<te::Plugin::WindowState*> (&state);

    if (windowState == nullptr)
        return {};

    auto& plugin = windowState->plugin;

    if (auto external = dynamic_cast<te::ExternalPlugin*> (&plugin); external == nullptr || external->getAudioPluginInstance() == nullptr)
        return {};

    auto window = std::make_unique<PluginWindow> (plugin);

    if (! window->hasEditor())
        return {};

    // The engine is a background process; bring the window to the front of the desktop.
    juce::Process::makeForegroundProcess();
    window->setVisible (true);
    window->toFront (true);
    return window;
}

void PluginWindowBehaviour::recreatePluginWindowContentAsync (te::Plugin& plugin)
{
    if (auto window = dynamic_cast<PluginWindow*> (plugin.windowState->pluginWindow.get()))
    {
        juce::Component::SafePointer<PluginWindow> safe (window);
        juce::Timer::callAfterDelay (50, [safe]
        {
            if (safe != nullptr)
                safe->recreateEditor();
        });
        return;
    }

    UIBehaviour::recreatePluginWindowContentAsync (plugin);
}

} // namespace gk
