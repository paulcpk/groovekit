#include "PluginWindows.h"

#include "WindowOrder.h"

namespace gk
{

namespace te = tracktion;

namespace
{
    namespace colours
    {
        const juce::Colour panel (0xff18181b), raised (0xff202024), control (0xff29292e), line (0xff2c2c31),
                           fg (0xffd6d6db), dim (0xff8d8d96), faint (0xff5b5b64), accent (0xfff2a33a);
    }

    /** The bar above a plugin's editor: where an instrument's channel plays and how loud, or how
        much of an effect is heard, and automating the parameter last moved in the editor. Changes
        go through the project like the UI's, so they're undoable and the UI follows them. */
    class PluginBar : public juce::Component,
                      private juce::Timer,
                      private te::AutomatableParameter::Listener
    {
    public:
        static constexpr int height = 34;

        explicit PluginBar (te::Plugin& p)
        {
            auto& host = PluginWindowBehaviour::host;
            const auto key = host.findKey ? host.findKey (p) : std::string();
            isChannel = key.starts_with ("channel:");

            if (isChannel)
            {
                channel = key.substr (8);
            }
            else if (key.starts_with ("effect:"))
            {
                const auto slash = key.find ('/');
                insert = key.substr (7, slash - 7);
                effect = key.substr (slash + 1);
            }

            const bool known = isChannel || ! effect.empty();

            for (auto* c : { static_cast<juce::Component*> (&insertBox), static_cast<juce::Component*> (&volume), static_cast<juce::Component*> (&pan) })
                if (isChannel)
                    addAndMakeVisible (c);

            if (! isChannel && known)
                addAndMakeVisible (mix);

            addAndMakeVisible (automate);
            automate.setEnabled (false);
            automate.setButtonText ("Move a control to automate it");

            style (insertBox);
            insertBox.setTooltip ("Mixer insert the channel plays into");
            insertBox.onChange = [this] { insertChosen(); };

            setUpSlider (volume, -60, 12, 0, " dB", "volumeDb", "Channel volume");
            setUpSlider (pan, -1, 1, 0, "", "pan", "Pan channel");
            setUpSlider (mix, 0, 100, 100, " %", "mix", "Effect mix");

            automate.setColour (juce::TextButton::buttonColourId, colours::control);
            automate.setColour (juce::TextButton::textColourOffId, colours::fg);
            automate.onClick = [this] { automateLast(); };

            for (int i = 0; i < p.getNumAutomatableParameters(); ++i)
                if (auto param = p.getAutomatableParameter (i))
                {
                    param->addListener (this);
                    params.push_back (param);
                }

            refresh();
            startTimerHz (15);
        }

        ~PluginBar() override
        {
            for (auto& param : params)
                param->removeListener (this);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (colours::raised);
            g.setColour (colours::line);
            g.fillRect (0, getHeight() - 1, getWidth(), 1);
            g.setColour (colours::faint);
            g.setFont (11.0f);

            for (auto& [label, x] : labels)
                g.drawText (label, x, 0, 60, getHeight(), juce::Justification::centredLeft);
        }

        void resized() override
        {
            labels.clear();
            auto area = getLocalBounds().reduced (8, 5);

            auto labelled = [&] (juce::Component& c, const char* label, int width)
            {
                const auto labelWidth = juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (11.0f), label) + 6;
                labels.push_back ({ label, area.getX() });
                area.removeFromLeft (labelWidth);
                c.setBounds (area.removeFromLeft (width));
                area.removeFromLeft (12);
            };

            if (isChannel)
            {
                labelled (insertBox, "Insert", 130);
                labelled (volume, "Vol", 110);
                labelled (pan, "Pan", 80);
            }
            else if (mix.isVisible())
            {
                labelled (mix, "Mix", 120);
            }

            automate.setBounds (area.removeFromRight (std::min (area.getWidth(), 230)));
        }

    private:
        bool isChannel = false;
        std::string channel, insert, effect;
        juce::ComboBox insertBox;
        juce::Slider volume, pan, mix;
        juce::TextButton automate;
        std::vector<std::pair<juce::String, int>> labels;
        std::vector<std::string> insertIds;

        std::vector<te::AutomatableParameter::Ptr> params;
        std::atomic<te::AutomatableParameter*> lastMoved { nullptr };
        te::AutomatableParameter* shownLast = nullptr;

        /** The gesture in progress: changes while a slider is held share an undo group, and are
            sent at most every 60 ms. */
        struct Gesture { std::string group; juce::uint32 lastSent = 0; std::optional<model::Command> pending; std::string label; };
        std::optional<Gesture> gesture;

        static void style (juce::ComboBox& box)
        {
            box.setColour (juce::ComboBox::backgroundColourId, colours::control);
            box.setColour (juce::ComboBox::textColourId, colours::fg);
            box.setColour (juce::ComboBox::outlineColourId, colours::line);
            box.setColour (juce::ComboBox::arrowColourId, colours::dim);
        }

        void setUpSlider (juce::Slider& slider, double min, double max, double reset, const char* suffix, std::string field, std::string label)
        {
            slider.setSliderStyle (juce::Slider::LinearBar);
            slider.setRange (min, max, max - min > 50 ? 0.1 : 0.01);
            slider.setDoubleClickReturnValue (true, reset);
            slider.setTextValueSuffix (suffix);
            slider.setColour (juce::Slider::trackColourId, colours::accent.withAlpha (0.35f));
            slider.setColour (juce::Slider::backgroundColourId, colours::control);
            slider.setColour (juce::Slider::textBoxTextColourId, colours::fg);
            slider.setColour (juce::Slider::textBoxOutlineColourId, colours::line);

            slider.onDragStart = [this, label] { gesture = Gesture { juce::Uuid().toString().toStdString(), 0, {}, label }; };
            slider.onDragEnd = [this] { flush(); gesture.reset(); };
            slider.onValueChange = [this, &slider, field, label]
            {
                const auto value = field == "mix" ? slider.getValue() / 100.0 : slider.getValue();
                model::Command command = isChannel ? model::Command (model::UpdateChannel { .channel = channel, .changes = { { field, value } } })
                                                   : model::Command (model::UpdateEffect { .insert = insert, .effect = effect, .changes = { { field, value } } });

                if (! gesture)
                {
                    send ({ command }, label, juce::Uuid().toString().toStdString());
                    return;
                }

                gesture->pending = command;

                if (juce::Time::getMillisecondCounter() - gesture->lastSent >= 60)
                    flush();
            };
        }

        void send (const std::vector<model::Command>& commands, const std::string& label, const std::string& group)
        {
            if (PluginWindowBehaviour::host.execute)
                PluginWindowBehaviour::host.execute (commands, label, group);
        }

        void flush()
        {
            if (! gesture || ! gesture->pending)
                return;

            send ({ *gesture->pending }, gesture->label, gesture->group);
            gesture->pending.reset();
            gesture->lastSent = juce::Time::getMillisecondCounter();
        }

        const model::Project* project() const
        {
            return PluginWindowBehaviour::host.getProject ? PluginWindowBehaviour::host.getProject() : nullptr;
        }

        /** Shows the project's values, unless a slider is being dragged. */
        void refresh()
        {
            auto p = project();

            if (p == nullptr || gesture)
                return;

            if (auto c = isChannel ? model::findChannel (*p, channel) : nullptr)
            {
                std::vector<std::string> ids;
                juce::StringArray names;

                for (size_t i = 0; i < p->mixer.inserts.size(); ++i)
                {
                    auto& ins = p->mixer.inserts[i];
                    ids.push_back (ins.id);
                    const auto name = ! ins.name.empty() ? ins.name : i == 0 ? std::string ("Master") : ins.id;
                    names.add (i == 0 ? juce::String (name) : juce::String (i) + " - " + juce::String (name));
                }

                if (ids != insertIds || names.size() != insertBox.getNumItems())
                {
                    insertIds = ids;
                    insertBox.clear (juce::dontSendNotification);
                    insertBox.addItemList (names, 1);
                    insertBox.addSeparator();
                    insertBox.addItem ("New insert", 10000);
                }

                const auto at = std::find (ids.begin(), ids.end(), c->insert);
                insertBox.setSelectedId (at == ids.end() ? 0 : static_cast<int> (at - ids.begin()) + 1, juce::dontSendNotification);
                volume.setValue (c->volumeDb, juce::dontSendNotification);
                pan.setValue (c->pan, juce::dontSendNotification);
            }
            else if (auto ins = ! effect.empty() ? model::findInsert (*p, insert) : nullptr)
            {
                for (auto& e : ins->effects)
                    if (model::getEffectId (e) == effect)
                        mix.setValue (std::visit ([] (auto& x) { return x.mix; }, e) * 100.0, juce::dontSendNotification);
            }
        }

        void insertChosen()
        {
            auto p = project();
            const auto id = insertBox.getSelectedId();

            if (p == nullptr || id == 0)
                return;

            const auto group = juce::Uuid().toString().toStdString();

            if (id != 10000)
            {
                if (static_cast<size_t> (id - 1) < insertIds.size())
                    send ({ model::UpdateChannel { .channel = channel, .changes = { { "insert", insertIds[static_cast<size_t> (id - 1)] } } } },
                          "Route channel", group);
                return;
            }

            // A new insert named after the channel.
            auto c = model::findChannel (*p, channel);
            auto newId = channel;

            for (int n = 2; model::findInsert (*p, newId) != nullptr; ++n)
                newId = channel.substr (0, 60) + "-" + std::to_string (n);

            model::Insert created;
            created.id = newId;
            created.name = c != nullptr && ! c->name.empty() ? c->name : channel;
            send ({ model::AddInsert { .insert = created }, model::UpdateChannel { .channel = channel, .changes = { { "insert", newId } } } },
                  "Route to a new insert", group);
        }

        void automateLast()
        {
            auto param = lastMoved.load();

            if (param == nullptr || ! PluginWindowBehaviour::host.createAutomation)
                return;

            model::AutomationTarget target;
            target.param = param->paramID.toStdString();

            if (isChannel)
            {
                target.scope = model::AutomationTargetScope::channel;
                target.channel = channel;
            }
            else
            {
                target.scope = model::AutomationTargetScope::effect;
                target.insert = insert;
                target.effect = effect;
            }

            PluginWindowBehaviour::host.createAutomation (target, std::round (param->getCurrentNormalisedValue() * 1000.0) / 1000.0);
        }

        void timerCallback() override
        {
            flush();
            refresh();

            if (auto last = lastMoved.load(); last != shownLast && (isChannel || ! effect.empty()))
            {
                shownLast = last;
                automate.setEnabled (true);
                automate.setButtonText ("Automate " + last->getParameterName());
                automate.setTooltip ("Make an automation clip for " + last->getParameterName() + ", the control last moved in the plugin");
            }
        }

        // Called by the plugin when its own controls move it (not by automation).
        void parameterChanged (te::AutomatableParameter& param, float) override        { lastMoved = &param; }
        void parameterChangeGestureBegin (te::AutomatableParameter& param) override     { lastMoved = &param; }
        void curveHasChanged (te::AutomatableParameter&) override {}
    };

    /** The bar above the plugin's editor. */
    struct Frame : juce::Component
    {
        Frame (te::Plugin& p, std::unique_ptr<te::Plugin::EditorComponent> e) : bar (p), editor (std::move (e))
        {
            addAndMakeVisible (bar);

            if (editor != nullptr)
                addAndMakeVisible (*editor);

            fit();
        }

        void fit()
        {
            const auto w = editor != nullptr ? editor->getWidth() : 0;
            const auto h = editor != nullptr ? editor->getHeight() : 0;
            setSize (std::max (w, 420), h + PluginBar::height);
        }

        void resized() override
        {
            bar.setBounds (0, 0, getWidth(), PluginBar::height);

            if (editor != nullptr)
                editor->setTopLeftPosition ((getWidth() - editor->getWidth()) / 2, PluginBar::height);
        }

        void childBoundsChanged (juce::Component* c) override
        {
            if (c == editor.get())
                fit();
        }

        void paint (juce::Graphics& g) override     { g.fillAll (colours::panel); }

        PluginBar bar;
        std::unique_ptr<te::Plugin::EditorComponent> editor;
    };

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
            releaseKeys (true);
            trackBounds = false;
            plugin.edit.flushPluginStateIfNeeded (plugin);
            setEditor (nullptr);
        }

        void recreateEditor()
        {
            setEditor (nullptr);
            setEditor (plugin.createEditor());
        }

        bool hasEditor() const { return frame != nullptr && frame->editor != nullptr; }

        // Keys nothing in the window used (a text field keeps its own) go to the typing keyboard,
        // which plays the selected channel as it does in the UI's window.
        bool keyPressed (const juce::KeyPress& key) override
        {
            const auto mods = key.getModifiers();
            const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

            if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown() || c <= ' ' || c > '~' || ! PluginWindowBehaviour::host.typedKey)
                return false;

            const auto code = key.getKeyCode();

            if (std::find_if (held.begin(), held.end(), [code] (auto& h) { return h.first == code; }) == held.end())
            {
                held.push_back ({ code, std::string (1, static_cast<char> (c)) });
                PluginWindowBehaviour::host.typedKey (held.back().second, true);
            }

            return true;
        }

        bool keyStateChanged (bool isKeyDown) override
        {
            if (! isKeyDown)
                releaseKeys (false);

            return false;
        }

        void activeWindowStatusChanged() override
        {
            DocumentWindow::activeWindowStatusChanged();

            if (! isActiveWindow())
                releaseKeys (true);
        }

    private:
        te::Plugin& plugin;
        std::unique_ptr<Frame> frame;
        bool trackBounds = false;
        std::vector<std::pair<int, std::string>> held;     // key codes typed here, and their characters

        void releaseKeys (bool all)
        {
            for (auto it = held.begin(); it != held.end();)
            {
                if (all || ! juce::KeyPress::isKeyCurrentlyDown (it->first))
                {
                    if (PluginWindowBehaviour::host.typedKey)
                        PluginWindowBehaviour::host.typedKey (it->second, false);

                    it = held.erase (it);
                }
                else
                {
                    ++it;
                }
            }
        }

        void setEditor (std::unique_ptr<te::Plugin::EditorComponent> e)
        {
            JUCE_AUTORELEASEPOOL
            {
                setConstrainer (nullptr);
                clearContentComponent();
                frame.reset();

                if (e != nullptr)
                {
                    frame = std::make_unique<Frame> (plugin, std::move (e));
                    setContentNonOwned (frame.get(), true);
                }

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

    bringToFront (*window);
    return window;
}

void PluginWindowBehaviour::bringToFront (juce::Component& window)
{
    window.setVisible (true);
    window.toFront (false);

    if (auto* peer = window.getPeer())
        orderFrontRegardless (peer->getNativeHandle());
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
