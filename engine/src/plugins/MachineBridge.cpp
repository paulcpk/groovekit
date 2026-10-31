#include "MachineBridge.h"

#include "live/LiveEngine.h"

#include "common/GrooveKitMachine.h"

#include <set>

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wshadow"
 #pragma clang diagnostic ignored "-Wextra"
#endif

#include <pluginterfaces/vst/ivstcomponent.h>

#if defined (__clang__)
 #pragma clang diagnostic pop
#endif

namespace gk
{

namespace te = tracktion;
using namespace model;

namespace
{
    constexpr int stepVelocity = 100, accentVelocity = 127, graceVelocity = 64;

    /** The machine's interface on a hosted plugin, with a reference held; nullptr if it has none. */
    gkmachine::IMachine* findMachine (te::Plugin* plugin)
    {
        auto external = dynamic_cast<te::ExternalPlugin*> (plugin);

        if (external == nullptr || external->getAudioPluginInstance() == nullptr)
            return nullptr;

        struct Visitor : juce::ExtensionsVisitor
        {
            void visitVST3Client (const VST3Client& client) override
            {
                if (auto* component = client.getIComponentPtr())
                {
                    void* found = nullptr;

                    if (component->queryInterface (gkmachine::IMachine::getIid(), &found) == Steinberg::kResultOk && found != nullptr)
                        machine = static_cast<gkmachine::IMachine*> (found);
                }
            }

            gkmachine::IMachine* machine = nullptr;
        };

        Visitor visitor;
        external->getAudioPluginInstance()->getExtensions (visitor);

        if (visitor.machine != nullptr && visitor.machine->getVersion() < 1)
        {
            visitor.machine->release();
            return nullptr;
        }

        return visitor.machine;
    }

    Ticks msToTicks (const Project& project, double ms)
    {
        return std::max<Ticks> (1, std::llround (ms * 0.001 * project.tempo / 60.0 * static_cast<double> (ticksPerQuarter)));
    }

    /** How far before its hit a grace note may sit: the longest flam, short of the step before. */
    Ticks graceWindow (const Project& project, Ticks stepTicks)
    {
        return std::clamp<Ticks> (msToTicks (project, MachineBridge::maxFlamMs), 1, stepTicks - 1);
    }

    /** The notes that make a hit of a key at a tick: the loudest note starting there (the hit),
        any other starting there as loud (doubled), and softer ones of the key just before it or
        with it (grace notes: a flam). */
    struct HitNotes
    {
        std::optional<size_t> main;
        std::vector<size_t> all;
        bool flam = false;
    };

    HitNotes findHit (const std::vector<Note>& notes, int key, Ticks at, Ticks window)
    {
        HitNotes hit;

        for (size_t i = 0; i < notes.size(); ++i)
            if (notes[i].key == key && notes[i].start == at && (! hit.main || notes[i].velocity > notes[*hit.main].velocity))
                hit.main = i;

        if (! hit.main)
            return hit;

        const auto loud = notes[*hit.main].velocity;

        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];

            if (n.key != key || n.start > at || n.start < at - window)
                continue;

            if (n.velocity < loud)
            {
                hit.flam = true;
                hit.all.push_back (i);
            }
            else if (n.start == at)
            {
                hit.all.push_back (i);
            }
        }

        return hit;
    }

    void removeIndices (std::vector<Note>& notes, std::vector<size_t> indices)
    {
        std::sort (indices.begin(), indices.end(), std::greater<>());
        indices.erase (std::unique (indices.begin(), indices.end()), indices.end());

        for (auto i : indices)
            notes.erase (notes.begin() + static_cast<std::ptrdiff_t> (i));
    }

    /** The keys with a hit (an on-grid note) at a tick. */
    std::vector<int> keysAt (const std::vector<Note>& notes, Ticks at)
    {
        std::vector<int> keys;

        for (auto& n : notes)
            if (n.start == at && std::find (keys.begin(), keys.end(), n.key) == keys.end())
                keys.push_back (n.key);

        return keys;
    }
}

//==============================================================================
/** One machine on one channel: GrooveKit as its host. */
class MachineBridge::Link : public gkmachine::IHost
{
public:
    Link (MachineBridge& b, Id c, gkmachine::IMachine* m) : bridge (b), channel (std::move (c)), machine (m) {}

    MachineBridge& bridge;
    const Id channel;
    gkmachine::IMachine* const machine;
    Ticks stepTicks = 240;
    int depth = 0;
    std::optional<std::string> group;
    std::pair<int, bool> shownPlayhead { -2, false };

    std::string channelName() const
    {
        auto c = findChannel (bridge.session.getProject(), channel);
        return c != nullptr && ! c->name.empty() ? c->name : channel;
    }

    /** A hit's notes: the hit and, with a flam, its grace note before it (at the pattern's start
        at the earliest). */
    void addHit (std::vector<Note>& notes, int key, Ticks at, int velocity, bool flam, float flamMs) const
    {
        notes.push_back ({ channel, at, stepTicks, juce::jlimit (0, 127, key), juce::jlimit (1, 127, velocity) });

        if (flam)
        {
            const auto grace = std::max<Ticks> (0, at - std::min (msToTicks (bridge.session.getProject(), flamMs), graceWindow (bridge.session.getProject(), stepTicks)));
            notes.push_back ({ channel, grace, std::max<Ticks> (1, at - grace), juce::jlimit (0, 127, key), graceVelocity });
        }
    }

    Ticks window() const     { return graceWindow (bridge.session.getProject(), stepTicks); }

    /** A pattern with a length of its own grows by whole pages to take a step past its end. */
    void lengthenFor (std::optional<TickLength>& length, int step) const
    {
        if (length && static_cast<Ticks> (step) * stepTicks >= *length)
            length = static_cast<TickLength> ((step / 16 + 1) * 16) * stepTicks;
    }

    //==============================================================================
    void PLUGIN_API setStepTicks (gkmachine::int32 ticks) override
    {
        stepTicks = std::clamp<Ticks> (ticks, 10, ticksPerQuarter * 4);
        bridge.triggerAsyncUpdate();
    }

    void PLUGIN_API beginEdit() override
    {
        if (depth++ == 0)
            group = bridge.newGroup();
    }

    void PLUGIN_API endEdit() override
    {
        if (depth > 0 && --depth == 0)
            group.reset();
    }

    void PLUGIN_API setStep (gkmachine::int32 key, gkmachine::int32 step, gkmachine::int32 on, gkmachine::int32 accent, gkmachine::int32 flam, float flamMs) override
    {
        if (step < 0)
            return;

        const auto label = on != 0 ? "Add a step to " + channelName() : "Remove a step from " + channelName();

        bridge.edit (*this, label, [&] (std::vector<Note>& notes, std::optional<TickLength>& length, bool)
        {
            const auto at = static_cast<Ticks> (step) * stepTicks;
            removeIndices (notes, findHit (notes, key, at, window()).all);

            if (on != 0)
            {
                addHit (notes, key, at, accent != 0 ? accentVelocity : stepVelocity, flam != 0, flamMs);
                lengthenFor (length, step);
            }
        });
    }

    void PLUGIN_API setAccent (gkmachine::int32 step, gkmachine::int32 on) override
    {
        bridge.edit (*this, (on != 0 ? "Accent a step on " : "Take an accent off ") + channelName(), [&] (std::vector<Note>& notes, std::optional<TickLength>&, bool)
        {
            const auto at = static_cast<Ticks> (step) * stepTicks;

            for (auto key : keysAt (notes, at))
            {
                const auto hit = findHit (notes, key, at, window());

                for (auto i : hit.all)
                    if (notes[i].start == at)
                        notes[i].velocity = on != 0 ? accentVelocity : stepVelocity;
            }
        });
    }

    void PLUGIN_API setFlam (gkmachine::int32 step, gkmachine::int32 on, float flamMs) override
    {
        bridge.edit (*this, (on != 0 ? "Flam a step on " : "Take a flam off ") + channelName(), [&] (std::vector<Note>& notes, std::optional<TickLength>&, bool)
        {
            const auto at = static_cast<Ticks> (step) * stepTicks;

            for (auto key : keysAt (notes, at))
            {
                const auto hit = findHit (notes, key, at, window());
                const auto velocity = notes[*hit.main].velocity;
                removeIndices (notes, hit.all);
                addHit (notes, key, at, velocity, on != 0, flamMs);
            }
        });
    }

    void PLUGIN_API setFlamTime (float flamMs) override
    {
        bridge.edit (*this, "Change the flams on " + channelName(), [&] (std::vector<Note>& notes, std::optional<TickLength>&, bool)
        {
            // Every flammed hit, found before any moves.
            std::vector<std::tuple<int, Ticks, int>> flams;

            for (auto& n : notes)
                if (n.start % stepTicks == 0)
                    if (auto hit = findHit (notes, n.key, n.start, window()); hit.flam && hit.main == static_cast<size_t> (&n - notes.data()))
                        flams.emplace_back (n.key, n.start, n.velocity);

            for (auto& [key, at, velocity] : flams)
            {
                removeIndices (notes, findHit (notes, key, at, window()).all);
                addHit (notes, key, at, velocity, true, flamMs);
            }
        });
    }

    void PLUGIN_API setLength (gkmachine::int32 steps) override
    {
        if (steps < 1)
            return;

        bridge.edit (*this, "Set the pattern's length", [&] (std::vector<Note>&, std::optional<TickLength>& length, bool)
        {
            length = static_cast<TickLength> (steps) * stepTicks;
        });
    }

    void PLUGIN_API clearRow (gkmachine::int32 key, gkmachine::int32 fromStep, gkmachine::int32 numSteps) override
    {
        bridge.edit (*this, "Clear steps on " + channelName(), [&] (std::vector<Note>& notes, std::optional<TickLength>&, bool)
        {
            clear (notes, key, fromStep, numSteps);
        });
    }

    void PLUGIN_API pasteRow (gkmachine::int32 key, gkmachine::int32 fromStep, gkmachine::int32 numSteps, const gkmachine::Hit* hits, gkmachine::int32 numHits, float flamMs) override
    {
        const std::vector<gkmachine::Hit> pasted (hits, hits + std::max (0, numHits));

        bridge.edit (*this, "Paste steps on " + channelName(), [&] (std::vector<Note>& notes, std::optional<TickLength>& length, bool)
        {
            clear (notes, key, fromStep, numSteps);

            for (auto& h : pasted)
            {
                if (h.step < fromStep || h.step >= fromStep + numSteps)
                    continue;

                addHit (notes, h.key, static_cast<Ticks> (h.step) * stepTicks, h.velocity, h.flam != 0, flamMs);
                lengthenFor (length, h.step);
            }
        });
    }

    void PLUGIN_API loadGroove (const char* name, const gkmachine::Hit* hits, gkmachine::int32 numHits, gkmachine::int32 lengthSteps, float flamMs) override
    {
        const std::vector<gkmachine::Hit> groove (hits, hits + std::max (0, numHits));
        const auto grooveName = juce::String::fromUTF8 (name != nullptr ? name : "").toStdString();

        if (lengthSteps < 1)
            return;

        bridge.edit (*this, "Load the groove " + grooveName + " on " + channelName(), [&] (std::vector<Note>& notes, std::optional<TickLength>& length, bool hasOtherNotes)
        {
            // Alone in its pattern the groove sets the length; beside other notes it repeats
            // through the length they have.
            const auto grooveTicks = static_cast<Ticks> (lengthSteps) * stepTicks;
            notes.clear();

            if (! hasOtherNotes)
                length = grooveTicks;

            // Without a length of its own, the pattern's length is what the other notes give it.
            const auto total = length ? *length : [&]
            {
                auto others = *bridge.findFocusedPattern();
                std::erase_if (others.notes, [&] (const Note& n) { return n.channel == channel; });
                return getPatternLength (others, bridge.session.getProject().timeSignature);
            }();

            for (Ticks from = 0; from < total; from += grooveTicks)
                for (auto& h : groove)
                    if (const auto at = from + static_cast<Ticks> (h.step) * stepTicks; h.step >= 0 && h.step < lengthSteps && at < total)
                        addHit (notes, h.key, at, h.velocity, h.flam != 0, flamMs);
        });
    }

    void PLUGIN_API setShuffle (float amount) override
    {
        const auto swing = std::round (juce::jlimit (0.0, 1.0, static_cast<double> (amount)) * 1000.0) / 1000.0;

        if (std::abs (swing - bridge.session.getProject().swing) > 1e-9)
            bridge.execute (*this, { SetSwing { swing } }, "Set the swing");
    }

    void PLUGIN_API play() override
    {
        // After this call: pattern mode rebuilds the Edit, and with it this machine.
        juce::MessageManager::callAsync ([&b = bridge, alive = std::weak_ptr<bool> (bridge.lifetime)]
        {
            if (alive.expired())
                return;

            try
            {
                if (auto* pattern = b.findFocusedPattern())
                {
                    b.live.setPlayPattern (pattern->id);
                    b.live.play();
                }
            }
            catch (const std::exception& e)
            {
                juce::Logger::writeToLog ("Machine: couldn't play: " + juce::String (e.what()));
            }
        });
    }

    void PLUGIN_API stop() override
    {
        juce::MessageManager::callAsync ([&b = bridge, alive = std::weak_ptr<bool> (bridge.lifetime)]
        {
            if (! alive.expired())
                b.live.stop();
        });
    }

    void PLUGIN_API detach() override
    {
        // This link is deleted here: nothing of it is touched after.
        bridge.forget (*this);
    }

private:
    void clear (std::vector<Note>& notes, int key, int fromStep, int numSteps) const
    {
        const auto from = static_cast<Ticks> (std::max (0, fromStep)) * stepTicks;
        const auto to = from + static_cast<Ticks> (std::max (0, numSteps)) * stepTicks;
        std::vector<size_t> gone;

        // The hits there with their grace notes (which may sit before the first step), then
        // whatever else of the key starts there.
        for (Ticks at = from; at < to; at += stepTicks)
        {
            const auto hit = findHit (notes, key, at, window());
            gone.insert (gone.end(), hit.all.begin(), hit.all.end());
        }

        for (size_t i = 0; i < notes.size(); ++i)
            if (notes[i].key == key && notes[i].start >= from && notes[i].start < to)
                gone.push_back (i);

        removeIndices (notes, gone);
    }
};

//==============================================================================
MachineBridge::MachineBridge (LiveEngine& l, Session& s) : live (l), session (s)
{
    listenerId = session.addListener ([this] (const Change&) { triggerAsyncUpdate(); });
    startTimerHz (30);
}

MachineBridge::~MachineBridge()
{
    *lifetime = false;
    lifetime.reset();
    stopTimer();
    session.removeListener (listenerId);
    disconnect();
}

void MachineBridge::connect()
{
    disconnect();

    for (auto& channel : session.getProject().channels)
    {
        if (! std::holds_alternative<PluginInstrument> (channel.instrument))
            continue;

        if (auto* machine = findMachine (live.findPlugin ("channel:" + channel.id)))
            attach (channel.id, machine);
    }
}

void MachineBridge::attach (const Id& channel, gkmachine::IMachine* machine)
{
    auto& link = *links.emplace_back (std::make_unique<Link> (*this, channel, machine));
    machine->setHost (&link);
    triggerAsyncUpdate();
}

void MachineBridge::forget (Link& link)
{
    std::erase_if (links, [&] (auto& l) { return l.get() == &link; });
}

gkmachine::IHost* MachineBridge::findHost (const Id& channel) const
{
    for (auto& link : links)
        if (link->channel == channel)
            return link.get();

    return nullptr;
}

void MachineBridge::disconnect()
{
    for (auto& link : links)
    {
        link->machine->setHost (nullptr);
        link->machine->release();
    }

    links.clear();
    cancelPendingUpdate();
}

void MachineBridge::setFocus (std::optional<Id> pattern)
{
    if (pattern == focus)
        return;

    focus = std::move (pattern);
    triggerAsyncUpdate();
}

const Pattern* MachineBridge::findFocusedPattern() const
{
    auto& project = session.getProject();

    if (focus)
        if (auto* pattern = findPattern (project, *focus))
            return pattern;

    return project.patterns.empty() ? nullptr : &project.patterns.front();
}

std::vector<Id> MachineBridge::getConnectedChannels() const
{
    std::vector<Id> channels;

    for (auto& link : links)
        channels.push_back (link->channel);

    return channels;
}

void MachineBridge::updateNow()
{
    cancelPendingUpdate();
    handleAsyncUpdate();
    timerCallback();
}

void MachineBridge::handleAsyncUpdate()
{
    for (auto& link : links)
        showPattern (*link);
}

void MachineBridge::timerCallback()
{
    for (auto& link : links)
        showPlayhead (*link);
}

void MachineBridge::showPattern (Link& link)
{
    auto& project = session.getProject();
    const auto* pattern = findFocusedPattern();
    const auto stepTicks = link.stepTicks;
    std::vector<gkmachine::Hit> hits;
    std::string name;
    Ticks length = ticksPerBar (project.timeSignature);

    if (pattern != nullptr)
    {
        name = pattern->name.empty() ? pattern->id : pattern->name;
        length = getPatternLength (*pattern, project.timeSignature);

        std::vector<Note> notes;

        for (auto& n : pattern->notes)
            if (n.channel == link.channel)
                notes.push_back (n);

        // Each key's hit on each step, once.
        std::set<std::pair<int, Ticks>> seen;
        const auto window = graceWindow (project, stepTicks);

        for (auto& n : notes)
        {
            if (n.start % stepTicks != 0 || ! seen.insert ({ n.key, n.start }).second)
                continue;

            const auto hit = findHit (notes, n.key, n.start, window);
            hits.push_back ({ n.key, static_cast<gkmachine::int32> (n.start / stepTicks), notes[*hit.main].velocity, hit.flam ? 1 : 0 });
        }
    }

    gkmachine::Pattern shown;
    shown.name = name.c_str();
    shown.exists = pattern != nullptr ? 1 : 0;
    shown.stepTicks = static_cast<gkmachine::int32> (stepTicks);
    shown.lengthSteps = static_cast<gkmachine::int32> ((length + stepTicks - 1) / stepTicks);
    shown.stepsPerBar = static_cast<gkmachine::int32> (std::max<Ticks> (1, ticksPerBar (project.timeSignature) / stepTicks));
    shown.swing = static_cast<float> (project.swing);
    shown.hits = hits.data();
    shown.numHits = static_cast<gkmachine::int32> (hits.size());
    link.machine->showPattern (shown);
}

void MachineBridge::showPlayhead (Link& link)
{
    const auto transport = live.getTransportState();
    const auto* pattern = findFocusedPattern();
    int step = -1;

    if (transport.playing && pattern != nullptr)
    {
        auto& project = session.getProject();
        const auto length = getPatternLength (*pattern, project.timeSignature);
        std::optional<Ticks> local;

        // In pattern mode positions are within the pattern; in the song, within a clip of it.
        if (transport.pattern)
        {
            if (*transport.pattern == pattern->id)
                local = transport.position;
        }
        else
        {
            for (auto& clip : project.playlist.clips)
                if (auto pc = std::get_if<PatternClip> (&clip); pc != nullptr && pc->pattern == pattern->id && ! pc->mute
                                                                && transport.position >= pc->start && transport.position < pc->start + pc->length)
                    local = transport.position - pc->start + pc->offset;
        }

        if (local && length > 0)
            step = static_cast<int> ((*local % length) / link.stepTicks);
    }

    const std::pair<int, bool> now { step, transport.playing };

    if (now != link.shownPlayhead)
    {
        link.shownPlayhead = now;
        link.machine->setPlayhead (step, transport.playing ? 1 : 0);
    }
}

void MachineBridge::edit (Link& link, const std::string& label, const NoteEdit& change)
{
    auto& project = session.getProject();
    const auto* pattern = findFocusedPattern();

    std::vector<Note> notes;
    std::optional<TickLength> length;
    bool hasOtherNotes = false;

    if (pattern != nullptr)
    {
        length = pattern->length;

        for (auto& n : pattern->notes)
        {
            if (n.channel == link.channel)
                notes.push_back (n);
            else
                hasOtherNotes = true;
        }
    }

    const auto before = notes;
    const auto lengthBefore = length;
    change (notes, length, hasOtherNotes);

    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return std::tie (a.start, a.key, a.velocity) < std::tie (b.start, b.key, b.velocity); });

    std::vector<Command> commands;

    if (pattern == nullptr)
    {
        // A first step makes the pattern.
        if (notes.empty() && ! length)
            return;

        Pattern made;
        int n = static_cast<int> (project.patterns.size()) + 1;

        while (findPattern (project, "pattern-" + std::to_string (n)) != nullptr)
            ++n;

        made.id = "pattern-" + std::to_string (n);
        made.name = "Pattern " + std::to_string (n);
        made.length = length;
        made.notes = notes;
        commands.push_back (AddPattern { made, {} });
        focus = made.id;
    }
    else
    {
        auto sortedBefore = before;
        std::sort (sortedBefore.begin(), sortedBefore.end(), [] (const Note& a, const Note& b) { return std::tie (a.start, a.key, a.velocity) < std::tie (b.start, b.key, b.velocity); });

        if (notes != sortedBefore)
            commands.push_back (SetNotes { pattern->id, link.channel, notes });

        if (length && length != lengthBefore)
            commands.push_back (UpdatePattern { pattern->id, Json { { "length", *length } } });
    }

    if (! commands.empty())
        execute (link, commands, label);
}

void MachineBridge::execute (Link& link, const std::vector<Command>& commands, const std::string& label)
{
    try
    {
        // As the UI's own controls and the bar above plugin windows do.
        session.execute (commands, { "ui", link.group, label });
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog ("Machine: " + juce::String (e.what()));
    }
}

std::string MachineBridge::newGroup()
{
    return "machine-" + std::to_string (juce::Time::currentTimeMillis()) + "-" + std::to_string (++groupCount);
}

} // namespace gk
