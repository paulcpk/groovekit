#pragma once

#include "session/Session.h"

#include <juce_events/juce_events.h>

namespace gkmachine { class IHost; class IMachine; }

namespace gk
{

class LiveEngine;

/** Connects the rhythm machines on the song's channels (GrooveKit's Groove-8 and Groove-9, or any
    plugin offering gkmachine::IMachine, see plugins/common/GrooveKitMachine.h) to the song, so
    their step keys edit the GrooveKit pattern the user is working on rather than patterns of
    their own. Lives on the message thread, with the live engine.

    A machine is shown the focused pattern (the one selected in the UI: setFocus; else the first)
    with its channel's notes counted in the machine's steps, again whenever the song changes, and
    where that pattern plays. What the machine asks for becomes an ordinary change to the song: a
    hit is a note one step long at velocity 100 (127 accented) on the key that plays the voice,
    a flam a grace note at velocity 64 the flam time before it. Edits the machine groups (a drag
    across the keys, a knob turned) undo as one.

    The live Edit's plugins are made again on every rebuild: the engine calls disconnect() before
    the old ones go and connect() once the new ones are there.
*/
class MachineBridge : private juce::AsyncUpdater,
                      private juce::Timer
{
public:
    MachineBridge (LiveEngine&, Session&);
    ~MachineBridge() override;

    /** Finds the machines among the live Edit's instruments and connects them. */
    void connect();

    /** Lets go of every machine; they're never called again. */
    void disconnect();

    /** Connects a machine on a channel, holding the reference it comes with (connect() does this
        for the live Edit's plugins; tests stand one in). */
    void attach (const model::Id& channel, gkmachine::IMachine*);

    /** What a channel's machine calls, or nullptr if none is connected there. */
    gkmachine::IHost* findHost (const model::Id& channel) const;

    /** The pattern the user is working on (selected in the UI), or none. */
    void setFocus (std::optional<model::Id>);
    const std::optional<model::Id>& getFocus() const     { return focus; }

    /** The pattern machines show and edit: the focused one if it exists, else the first; nullptr
        when the song has none (a step set then makes one). */
    const model::Pattern* findFocusedPattern() const;

    /** The channels whose machines are connected. */
    std::vector<model::Id> getConnectedChannels() const;

    /** Shows every machine the pattern and the playhead now, rather than on the next message. */
    void updateNow();

    /** Grace notes for flams sit at most this long before their hit (more than the longest flam). */
    static constexpr double maxFlamMs = 50;

private:
    class Link;

    LiveEngine& live;
    Session& session;
    std::vector<std::unique_ptr<Link>> links;
    std::optional<model::Id> focus;
    int listenerId = 0;
    int groupCount = 0;
    std::shared_ptr<bool> lifetime = std::make_shared<bool> (true);  // for messages that may outlive this

    void handleAsyncUpdate() override;
    void timerCallback() override;
    void showPattern (Link&);
    void showPlayhead (Link&);

    /** Drops a machine that went without being let go (deleting its link). */
    void forget (Link&);

    /** Changes a channel's notes (and the pattern's length) in the focused pattern as one change
        to the song, making the pattern if there's none. */
    using NoteEdit = std::function<void (std::vector<model::Note>&, std::optional<model::TickLength>& length, bool hasOtherNotes)>;
    void edit (Link&, const std::string& label, const NoteEdit&);
    void execute (Link&, const std::vector<model::Command>&, const std::string& label);
    std::string newGroup();

    friend class Link;
    JUCE_DECLARE_NON_COPYABLE (MachineBridge)
};

} // namespace gk
