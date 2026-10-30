#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdint>

namespace gk::sampler
{

/** Delay, attack, hold, decay, sustain, release, in seconds (sustain a level from 0 to 1), with
    curves bending the attack, decay and release: 0 is straight, positive moves fast at first and
    then eases, negative starts slowly. */
struct EnvelopeShape
{
    float delay = 0, attack = 0, hold = 0, decay = 0, sustain = 1, release = 0.01f;
    float attackCurve = 0, decayCurve = 0, releaseCurve = 0;

    /** The curves' factors (see bend), worked out once by prepare(); 0 until then. */
    float attackBend = 0, decayBend = 0, releaseBend = 0;

    /** Works the curves' factors out, so the per-sample level needs no exp2. The generator and
        the audio clip settings do this; a shape used without it still bends right, just slower. */
    void prepare()
    {
        attackBend = factor (attackCurve);
        decayBend = factor (decayCurve);
        releaseBend = factor (releaseCurve);
    }

    static float factor (float curve)   { return std::exp2 (-4.0f * curve); }

    /** How far along a segment (0..1) the level has moved at progress p (0..1), for a curve
        and its factor (0 when not prepared). */
    static float bend (float p, float curve, float prepared)
    {
        if (curve == 0.0f)
            return p;

        // A rational curve: one division, exactly 0 and 1 at the ends.
        const auto e = prepared > 0.0f ? prepared : factor (curve);
        return p / (p + (1.0f - p) * e);
    }

    /** The level since a note started, before its release. */
    float levelBeforeRelease (double since) const;

    /** The level at a point of a note held for a while: since its start, and until its end
        (where the release finishes). Used for audio clips, which are shaped by position. */
    float levelAt (double since, double untilEnd) const;

    float length() const    { return delay + attack + hold + decay; }
};

/** Steps an EnvelopeShape through a note. */
class EnvelopeGenerator
{
public:
    void setSampleRate (double sr)              { secondsPerSample = 1.0 / sr; }
    void setShape (const EnvelopeShape& s)      { shape = s; shape.prepare(); }

    void noteOn()                               { stage = Stage::delay; time = 0; level = 0; skipEmptyStages(); }
    void noteOff();
    void reset()                                { stage = Stage::idle; level = 0; }

    /** The level for this sample, then moves on by one. */
    float next()                                { return advance (1); }

    /** Moves on by n samples and returns the level at the start of them (for control-rate use). */
    float advance (int n);

    bool isActive() const                       { return stage != Stage::idle; }
    bool isReleasing() const                    { return stage == Stage::release; }
    float getLevel() const                      { return level; }

private:
    enum class Stage { idle, delay, attack, hold, decay, sustain, release };

    EnvelopeShape shape;
    Stage stage = Stage::idle;
    double time = 0, secondsPerSample = 1.0 / 48000.0;
    float level = 0, releaseFrom = 0;

    float stageLength() const;
    float levelNow() const;
    void skipEmptyStages();
};

/** A low-frequency oscillator's settings. */
struct LfoSettings
{
    enum class Shape { sine, triangle, square, saw, random };

    Shape shape = Shape::sine;
    float amount = 0;               // -1..1, a share of the target's span
    float speed = 4;                // Hz, when not synced
    float syncBeats = 0;            // beats per cycle; 0 follows speed
    float delay = 0, attack = 0;    // seconds
    bool retrigger = true;

    bool isOn() const               { return amount != 0.0f; }

    /** Cycles a second at a tempo. */
    double cyclesPerSecond (double bpm) const { return syncBeats > 0 ? bpm / 60.0 / syncBeats : speed; }

    /** The wave at a phase (cycles; the whole part counts the cycles, which picks the random
        steps), from -1 to 1. Starts at 0 and rises, except square and random. */
    float wave (double phase) const;

    /** How much of the swing is in at a time since the note started: 0 during the delay, rising
        over the attack. */
    float fadeIn (double since) const;
};

/** Transient shaping of a whole sample, done once: punch (-1..1) sharpens or softens each hit's
    attack, tail (-1..1) brings up or tightens what rings on after it. */
juce::AudioBuffer<float> shapeTransients (const juce::AudioBuffer<float>&, double sampleRate, float punch, float tail);

} // namespace gk::sampler
