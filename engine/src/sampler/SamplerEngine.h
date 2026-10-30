#pragma once

#include "Handoff.h"
#include "Modulation.h"
#include "SampleCache.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <memory>
#include <optional>
#include <vector>

namespace gk::sampler
{

/** Granular playback: grains read at the note's pitch while a playhead moves through the part
    at its own speed (see the model's Grains). */
struct Grains
{
    enum class Shape { smooth, flat, pluck };

    bool on = false;
    float size = 0.08f;         // seconds
    float density = 40;         // grains a second
    float speed = 1;            // the playhead's pace through the part
    float spray = 0.005f;       // seconds of scatter around the playhead
    float detune = 0;           // cents, either way
    float width = 0;            // 0..1
    Shape shape = Shape::smooth;

    /** A grain's level at a point through it (0-1). The UI draws the same shapes. */
    static float window (Shape, float progress);
};

/** One playable layer, already resolved from the project model. */
struct Layer
{
    enum class Loop { off, forward, pingPong };

    std::shared_ptr<const Sample> sample;   // with its transients shaped, if asked
    int rootKey = 60;
    int keyLow = 0, keyHigh = 127;
    int velocityLow = 1, velocityHigh = 127;
    bool pitchTracking = true;
    double tuneSemitones = 0;   // transpose plus fine tune
    float gain = 1.0f;          // linear, with normalizing and polarity
    float pan = 0;              // -1..1, balance law: centre leaves both channels at unity
    bool oneShot = true;
    EnvelopeShape envelope { .release = 0.01f };
    /** The part of the sample that plays, in frames: [start, end); a negative end is the sample's end. */
    int start = 0, end = -1;
    bool reverse = false;
    Loop loop = Loop::off;
    int fadeIn = 0, fadeOut = 0;    // frames of the sample
    bool swapStereo = false;
    Grains grains;
    float length = 1;               // share of the part that plays, cut off there (fadeOut shapes the end)
    int crossfade = 0;              // frames a forward loop crossfades over

    /** Frames of the part that play: the part, shortened by the length. */
    double playedFrames() const     { return std::max (1.0, (endFrame() - start) * static_cast<double> (length)); }

    int endFrame() const        { return end < 0 ? sample->audio.getNumSamples() : end; }

    /** A looping layer plays until its note ends, like a gated one; so do grains held still. */
    bool releasesOnNoteOff() const  { return ! oneShot || loop != Loop::off || (grains.on && grains.speed < 0.01f); }
};

/** A resonant state-variable filter per voice (12 or 24 dB/octave), its cutoff moved by its own envelope. */
struct Filter
{
    enum class Type { off, lowpass, lowpass24, highpass, highpass24, bandpass, notch };

    Type type = Type::off;
    float cutoff = 20000.0f;      // Hz
    float resonance = 0;          // 0..1
    float envelopeAmount = 0;     // octaves at the envelope's peak
    EnvelopeShape envelope { .release = 0.01f };

    /** Q from resonance: 0.71 (no peak) at 0 up to 25 at 1. The UI draws the curve with the same mapping. */
    static float qFor (float resonance)     { return 0.7071f * std::pow (25.0f / 0.7071f, juce::jlimit (0.0f, 1.0f, resonance)); }

    /** The 24 dB/octave types are two stages: a fixed one (Q 0.54) and one with this Q (1.31, a
        flat Butterworth response, up to 25). */
    static float resonantStageQ (float resonance)   { return 1.3066f * std::pow (25.0f / 1.3066f, juce::jlimit (0.0f, 1.0f, resonance)); }

    static bool isSteep (Type t)            { return t == Type::lowpass24 || t == Type::highpass24; }
};

/** The TPT state-variable filter (Zavalishin): coefficients for one cutoff, applied to a channel's
    two-integrator state. */
struct Svf
{
    float a1 = 1, a2 = 0, a3 = 0, k = 1.4142f;

    void setup (float cutoff, float q, double sampleRate)
    {
        const auto fc = juce::jlimit (16.0f, static_cast<float> (sampleRate * 0.45), cutoff);
        const auto g = std::tan (juce::MathConstants<float>::pi * fc / static_cast<float> (sampleRate));
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float process (float in, float* state, Filter::Type type) const
    {
        const auto v3 = in - state[1];
        const auto v1 = a1 * state[0] + a2 * v3;
        const auto v2 = state[1] + a2 * state[0] + a3 * v3;
        state[0] = 2.0f * v1 - state[0];
        state[1] = 2.0f * v2 - state[1];

        switch (type)
        {
            case Filter::Type::lowpass:
            case Filter::Type::lowpass24:   return v2;
            case Filter::Type::highpass:
            case Filter::Type::highpass24:  return in - k * v1 - v2;
            case Filter::Type::bandpass:    return k * v1;
            case Filter::Type::notch:       return in - k * v1;
            case Filter::Type::off:         break;
        }

        return in;
    }
};

/** One channel's filter: one stage, or two for the 24 dB/octave types. */
struct FilterStages
{
    Svf first, second;

    void setup (Filter::Type type, float cutoff, float resonance, double sampleRate)
    {
        if (Filter::isSteep (type))
        {
            first.setup (cutoff, 0.5412f, sampleRate);
            second.setup (cutoff, Filter::resonantStageQ (resonance), sampleRate);
        }
        else
        {
            first.setup (cutoff, Filter::qFor (resonance), sampleRate);
        }
    }

    /** state holds four values: two integrators a stage. */
    float process (float in, float* state, Filter::Type type) const
    {
        const auto out = first.process (in, state, type);
        return Filter::isSteep (type) ? second.process (out, state + 2, type) : out;
    }
};

/** Envelopes and LFOs moving a sampler's volume, cutoff, resonance, pan and pitch (amounts are
    shares of each one's span; see the model). The volume envelope is each layer's, the cutoff
    envelope the filter's. */
struct Modulation
{
    struct Target
    {
        EnvelopeShape envelope;
        float amount = 0;
        LfoSettings lfo;
    };

    LfoSettings volume, cutoff;
    Target resonance, pan, pitch;

    static constexpr float cutoffOctaves = 5, pitchSemitones = 24;
};

struct Config
{
    std::vector<Layer> layers;
    Filter filter;
    Modulation modulation;
    int polyphony = 32;
};

/** The sampler's DSP, independent of Tracktion so it can be tested directly.

    Every layer whose key and velocity range contain a note starts a voice. When the
    voice count would exceed the polyphony, the oldest voice is faded out over a few
    milliseconds. Samples play with cubic interpolation; at an increment of exactly
    1 (root pitch, matching sample rate) the output is the sample data itself.
*/
class SamplerEngine
{
public:
    struct MidiEvent
    {
        int sampleOffset;   // relative to the start of the block
        juce::MidiMessage message;
    };

    static constexpr int maxVoices = 256;
    static constexpr double stealFadeSeconds = 0.003;
    /** Modulation (envelopes other than volume, LFOs, pan and pitch) moves every this many samples,
        gains gliding in between. */
    static constexpr int controlInterval = 16;

    SamplerEngine();

    void prepare (double sampleRate, int maxBlockSize);

    /** Hands over a new configuration, which the audio thread takes at the start of its next
        block. A voice keeps playing when its layer still plays the same part of the same sample
        (so envelopes and the filter can change while a note sounds); otherwise it fades out like
        a stolen voice, from the previous configuration, which is kept until the next swap. Call
        off the audio thread; the audio thread never waits for it. */
    void setConfig (std::shared_ptr<const Config>);

    /** Adds this block's output to the first two channels of buffer. Events must be sorted by offset. */
    void process (juce::AudioBuffer<float>&, int startSample, int numSamples, const std::vector<MidiEvent>&);

    void allNotesOff();

    /** Moves the filter away from the config's values (automation). Call on the audio thread,
        before process; setConfig goes back to the config's values. */
    void setFilterControls (float cutoff, float resonance, float envelopeAmount);

    /** The tempo synced LFOs follow. Call on the audio thread, before process. */
    void setTempo (double bpm)      { tempo = bpm; }

    int getNumActiveVoices() const;

private:
    /** The LFOs, in the order of their phases. */
    enum Lfo { volumeLfo, cutoffLfo, resonanceLfo, panLfo, pitchLfo, numLfos };

    static constexpr int maxGrains = 32;   // a voice's

    struct Grain
    {
        double read = 0;            // frames into the part
        float step = 1;             // its detuning, times the voice's increment
        float left = 1, right = 1;
        int age = 0, length = 0;
        bool active = false;
    };

    struct Voice
    {
        const Layer* layer = nullptr;
        size_t layerIndex = 0;
        int note = 0;
        uint64_t age = 0;
        double position = 0;        // frames into the part that plays, in the order it plays
        double baseIncrement = 1;   // frames a sample at the note's pitch
        double increment = 1;       // with the pitch moved
        int direction = 1;          // ping-pong loops play back and forth
        float velocityGain = 1;
        float gainL = 0, gainR = 0, gainStepL = 0, gainStepR = 0;
        EnvelopeGenerator envelope, filterEnvelope, resonanceEnvelope, panEnvelope, pitchEnvelope;
        double lfoPhase[numLfos] {};
        double since = 0;           // seconds since the note started
        float filterState[2][4] {}; // per channel: two integrators for each of two stages
        FilterStages filter;
        float filterCutoffSet = -1, filterResonanceSet = -1;   // what the filter is set up for
        Filter::Type filterTypeSet = Filter::Type::off;
        int samplesToControls = 0;
        bool controlsSet = false;
        int edgeFadeIn = 0, edgeFadeOut = 0;
        bool active = false;
        int stealSamplesLeft = 0;   // > 0 while fading out after being stolen

        // Granular playback: v.position is the playhead.
        Grain grains[maxGrains];
        int activeGrains = 0;
        double untilGrain = 0;
        uint32_t seed = 1;

        bool isStolen() const { return stealSamplesLeft > 0; }

        /** 0-1, from the voice's own sequence. */
        float random()
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return static_cast<float> (seed & 0xffffff) / static_cast<float> (0x1000000);
        }
    };

    Handoff<Config> handoff;
    const Config* config = nullptr;
    /** The configuration before, which voices fading out of it still read. */
    const Config* retired = nullptr;
    std::vector<Voice> voices;
    double sampleRate = 48000;
    int stealFadeSamples = 144;
    int edgeFadeSamples = 48;
    uint64_t nextAge = 0;
    float filterCutoff = 20000.0f, filterResonance = 0, filterEnvelopeAmount = 0;
    double tempo = 120;
    /** The phases of LFOs that don't restart with each note. */
    double sharedPhase[numLfos] {};

    /** Audio thread: makes a published configuration the one that plays. */
    void adopt (const Config& next);
    void noteOn (int note, int velocity);
    void noteOff (int note);
    void startVoice (const Layer&, int note, int velocity);
    Voice* findFreeVoice();
    void stealOldestVoice();
    void renderVoices (juce::AudioBuffer<float>&, int startSample, int numSamples);
    void renderVoice (Voice&, float* left, float* right, int numSamples);
    void startGrain (Voice&, double length);
    void updateControls (Voice&);
    void setEnvelopes (Voice&);
};

} // namespace gk::sampler
