#pragma once

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wshadow"
 #pragma clang diagnostic ignored "-Wextra"
 #pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#endif

#include <pluginterfaces/base/funknown.h>

#if defined (__clang__)
 #pragma clang diagnostic pop
#endif

/** How GrooveKit and the rhythm machines it hosts talk, when a machine runs inside GrooveKit's
    engine: the machine's step keys then edit the GrooveKit pattern its channel plays, rather than
    patterns of its own.

    The engine asks a hosted VST3's component for IMachine (by its id); a machine answers it. The
    engine then hands it an IHost, and shows it the pattern the user is working on and where it
    plays. Everything is called on the message thread, both ways. Only plain C types cross: the
    two sides are separate binaries, built apart, sharing no JUCE or standard library types.

    Steps count from the pattern's start in steps of the machine's chosen length (stepTicks, at
    960 ticks per quarter note); a page is 16 steps. A change that breaks either side changes the
    id; additions that don't raise the version. */
namespace gkmachine
{

using Steinberg::int32;

/** Raised when methods are added at the end of IMachine or IHost. 2: notes with their lengths,
    for machines that play lines (held, tied and slid notes): showNotes, setNotes and loadNotes. */
constexpr int32 version = 2;

/** A drum hit in the pattern: a note on the grid. */
struct Hit
{
    int32 key;          // MIDI note
    int32 step;         // from the pattern's start
    int32 velocity;     // 1-127; 110 and up is an accent
    int32 flam;         // not 0: a softer grace note comes just before it
};

/** A note of the channel's in the pattern, its times in ticks (960 per quarter note) from the
    pattern's start. A note that lasts past the start of the next one slides into it. */
struct Note
{
    int32 key;          // MIDI note
    int32 start;
    int32 length;
    int32 velocity;     // 1-127; 110 and up is an accent
};

/** The pattern GrooveKit shows the machine. */
struct Pattern
{
    const char* name;       // UTF-8; empty when there's no pattern yet
    int32 exists;           // 0: no pattern yet (setting a step makes one)
    int32 stepTicks;        // the step length the hits are counted in
    int32 lengthSteps;      // the pattern's length in those steps (rounded up)
    int32 stepsPerBar;      // a bar in those steps (rounded down; at least 1)
    float swing;            // the song's swing, 0 (straight) to 1
    const Hit* hits;        // the channel's notes that fall on a step, in no particular order
    int32 numHits;
};

/** What a machine asks of GrooveKit. Every edit is an ordinary, undoable change to the song; the
    edits between beginEdit and endEdit undo as one. */
class IHost
{
public:
    /** The step length the machine counts in (Scale); GrooveKit shows the pattern again in it. */
    virtual void PLUGIN_API setStepTicks (int32 ticks) = 0;

    /** Groups the edits until endEdit (a drag across the keys or a knob turned) into one. */
    virtual void PLUGIN_API beginEdit() = 0;
    virtual void PLUGIN_API endEdit() = 0;

    /** Sets or clears a hit: on, a note of one step at velocity 100 (127 with accent) and, with
        flam, a grace note flamMs earlier; off, the key's notes on that step go. */
    virtual void PLUGIN_API setStep (int32 key, int32 step, int32 on, int32 accent, int32 flam, float flamMs) = 0;

    /** Accents every hit on a step (velocity 127), or takes the accent off (100). */
    virtual void PLUGIN_API setAccent (int32 step, int32 on) = 0;

    /** Gives every hit on a step a grace note flamMs earlier, or takes them away. */
    virtual void PLUGIN_API setFlam (int32 step, int32 on, float flamMs) = 0;

    /** Moves every grace note in the pattern to flamMs before its hit. */
    virtual void PLUGIN_API setFlamTime (float flamMs) = 0;

    /** Sets the pattern's length in steps. */
    virtual void PLUGIN_API setLength (int32 steps) = 0;

    /** Removes the key's notes from numSteps steps from fromStep. */
    virtual void PLUGIN_API clearRow (int32 key, int32 fromStep, int32 numSteps) = 0;

    /** Replaces the key's notes there with these hits (their steps from the pattern's start). */
    virtual void PLUGIN_API pasteRow (int32 key, int32 fromStep, int32 numSteps, const Hit* hits, int32 numHits, float flamMs) = 0;

    /** Replaces all of the channel's notes in the pattern with a groove of lengthSteps steps,
        repeated through the pattern; an empty pattern takes the groove's length. */
    virtual void PLUGIN_API loadGroove (const char* name, const Hit* hits, int32 numHits, int32 lengthSteps, float flamMs) = 0;

    /** The song's swing, 0 (straight) to 1. */
    virtual void PLUGIN_API setShuffle (float amount) = 0;

    /** Plays the pattern on a loop (pattern mode), or stops. */
    virtual void PLUGIN_API play() = 0;
    virtual void PLUGIN_API stop() = 0;

    /** The machine is going (its plugin is deleted) while still connected: GrooveKit forgets it
        and never calls it again. The machine's last call. */
    virtual void PLUGIN_API detach() = 0;

    /** (2) Replaces the channel's notes that start within numSteps steps from fromStep with these;
        a pattern with a length of its own grows by whole pages to take them. */
    virtual void PLUGIN_API setNotes (int32 fromStep, int32 numSteps, const Note* notes, int32 numNotes) = 0;

    /** (2) Replaces all of the channel's notes in the pattern with a line of lengthSteps steps,
        repeated through the pattern as loadGroove does. */
    virtual void PLUGIN_API loadNotes (const char* name, const Note* notes, int32 numNotes, int32 lengthSteps) = 0;

protected:
    ~IHost() = default;
};

/** What a machine offers GrooveKit, from its VST3 component's queryInterface. */
class IMachine : public Steinberg::FUnknown
{
public:
    virtual int32 PLUGIN_API getVersion() = 0;

    /** GrooveKit connects (or with nullptr, lets go: the old host must never be called again). */
    virtual void PLUGIN_API setHost (IHost*) = 0;

    /** The pattern to show, whenever it or its notes change. The data is the machine's to copy;
        it doesn't outlive the call. */
    virtual void PLUGIN_API showPattern (const Pattern&) = 0;

    /** Where the pattern plays: its step, or -1 when it isn't playing here; and whether the
        transport runs. */
    virtual void PLUGIN_API setPlayhead (int32 step, int32 playing) = 0;

    /** (2) Every one of the channel's notes in the pattern shown, after each showPattern. The data
        is the machine's to copy. */
    virtual void PLUGIN_API showNotes (const Note* notes, int32 numNotes) = 0;

    static const Steinberg::TUID& getIid()
    {
        static const Steinberg::TUID id = INLINE_UID (0x4b1d6a03, 0x9c2e4f51, 0xa7e3b0c8, 0x5d16f2e9);
        return id;
    }
};

} // namespace gkmachine
