#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <vector>

/** DSP pieces GrooveKit's plugins are built from. All per-sample, all plain. */
namespace gkp::dsp
{

constexpr double twoPi = juce::MathConstants<double>::twoPi;

/** PolyBLEP: the correction that rounds off a step at phase 0 so it doesn't alias. */
inline double blep (double t, double dt)
{
    if (t < dt)         { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt)     { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}

inline double wrap (double phase)       { return phase - std::floor (phase); }
inline double noteHz (double note)      { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
inline float softClip (float x)         { return std::tanh (x); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

/** Equal-power gains for a pan position (-1 to 1), 1 each in the centre. */
inline std::pair<float, float> panGains (float pan)
{
    const auto angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1) * juce::MathConstants<float>::pi / 4;
    return { std::cos (angle) * juce::MathConstants<float>::sqrt2, std::sin (angle) * juce::MathConstants<float>::sqrt2 };
}

/** The classic shapes, band-limited: 0 sine, 1 triangle, 2 saw, 3 square, 4 pulse (width 0-1), 5 soft saw. */
inline float waveform (int shape, double phase, double increment, double width = 0.25)
{
    const auto t = wrap (phase);
    const auto dt = std::min (0.5, std::abs (increment));

    switch (shape)
    {
        case 0:  return static_cast<float> (std::sin (twoPi * t));
        case 1:  return static_cast<float> (t < 0.5 ? 4 * t - 1 : 3 - 4 * t);
        case 3:  return static_cast<float> ((t < 0.5 ? 1 : -1) + blep (t, dt) - blep (wrap (t + 0.5), dt));
        case 4:  { const auto w = juce::jlimit (0.05, 0.95, width); return static_cast<float> ((t < w ? 1 : -1) + blep (t, dt) - blep (wrap (t + 1 - w), dt)); }
        case 5:  return static_cast<float> (std::tanh (1.6 * (2 * t - 1 - blep (t, dt))) / std::tanh (1.6));
        default: return static_cast<float> (2 * t - 1 - blep (t, dt));
    }
}

/** A shape that morphs: 0 sine, 1 triangle, 2 saw, 3 square, 4 pulse; in between, a crossfade. */
inline float morph (double position, double phase, double increment, double width)
{
    const auto p = juce::jlimit (0.0, 4.0, position);
    const auto a = static_cast<int> (std::floor (p));
    const auto f = static_cast<float> (p - a);
    const auto first = waveform (a, phase, increment, width);
    return f < 0.001f ? first : lerp (first, waveform (std::min (4, a + 1), phase, increment, width), f);
}

//==============================================================================
/** Cytomic's trapezoidal state-variable filter, one 12 dB stage: 0 low-pass, 1 high-pass,
    2 band-pass, 3 notch, 4 peak. Set cutoff in Hz and resonance 0-1 before processing. */
struct Svf
{
    float ic1 = 0, ic2 = 0, g = 0.1f, k = 2;

    void set (double sampleRate, float hz, float resonance)
    {
        g = static_cast<float> (std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, sampleRate * 0.45, static_cast<double> (hz)) / sampleRate));
        k = 2.0f - 1.96f * juce::jlimit (0.0f, 1.0f, resonance);
    }

    float process (float x, int type)
    {
        const auto a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;

        switch (type)
        {
            case 1:  return x - k * v1 - v2;
            case 2:  return v1 * k;
            case 3:  return x - k * v1;
            case 4:  return x - k * v1 - 2 * v2;
            default: return v2;
        }
    }

    void reset() { ic1 = ic2 = 0; }
};

/** A one-pole low-pass (or high-pass) for tone and damping. */
struct OnePole
{
    float z = 0, a = 0.1f;

    void set (double sampleRate, float hz)
    {
        a = static_cast<float> (1 - std::exp (-twoPi * juce::jlimit (1.0, sampleRate * 0.49, static_cast<double> (hz)) / sampleRate));
    }

    float lowpass (float x)    { z += a * (x - z); return z; }
    float highpass (float x)   { return x - lowpass (x); }
    void reset()               { z = 0; }
};

struct DcBlocker
{
    float x1 = 0, y1 = 0;
    float process (float x) { const auto y = x - x1 + 0.995f * y1; x1 = x; y1 = y; return y; }
};

//==============================================================================
/** An ADSR with a curve: 0 linear, towards 1 more exponential (snappier decays). */
struct Envelope
{
    enum Stage { idle, attack, decay, sustain, release };

    Stage stage = idle;
    float value = 0, attackRate = 0, decayCoefficient = 0, releaseCoefficient = 0, sustainLevel = 0;
    double sampleRate = 48000;

    void prepare (double rate)  { sampleRate = rate; }

    void set (float a, float d, float s, float r, float curve = 0.6f)
    {
        attackRate = 1.0f / std::max (1.0f, static_cast<float> (a * sampleRate));
        const auto shape = juce::jmap (juce::jlimit (0.0f, 1.0f, curve), 3.0f, 8.0f);
        decayCoefficient = std::exp (-shape / std::max (1.0f, static_cast<float> (d * sampleRate)));
        releaseCoefficient = std::exp (-shape / std::max (1.0f, static_cast<float> (r * sampleRate)));
        sustainLevel = juce::jlimit (0.0f, 1.0f, s);
    }

    void noteOn()               { stage = attack; }
    void noteOff()              { if (stage != idle) stage = release; }
    void reset()                { stage = idle; value = 0; }
    bool isActive() const       { return stage != idle; }

    float next()
    {
        switch (stage)
        {
            case attack:
                value += attackRate;
                if (value >= 1) { value = 1; stage = decay; }
                break;
            case decay:
                value = sustainLevel + (value - sustainLevel) * decayCoefficient;
                if (value - sustainLevel < 0.0005f) { value = sustainLevel; stage = sustain; }
                break;
            case sustain:
                value = sustainLevel;
                break;
            case release:
                value *= releaseCoefficient;
                if (value < 0.0005f) { value = 0; stage = idle; }
                break;
            default:
                break;
        }

        return value;
    }
};

//==============================================================================
/** A low-frequency oscillator: 0 sine, 1 triangle, 2 saw, 3 square, 4 sample and hold, 5 drifting random. */
struct Lfo
{
    double phase = 0;
    float held = 0, target = 0, drift = 0;
    juce::Random random;

    void reset (double startPhase = 0)   { phase = startPhase; held = target = drift = 0; }

    float next (double increment, int shape)
    {
        const auto previous = phase;
        phase = wrap (phase + increment);

        if (phase < previous || previous == 0.0)
        {
            held = random.nextFloat() * 2 - 1;
            target = random.nextFloat() * 2 - 1;
        }

        switch (shape)
        {
            case 1:  return static_cast<float> (phase < 0.5 ? 4 * phase - 1 : 3 - 4 * phase);
            case 2:  return static_cast<float> (2 * phase - 1);
            case 3:  return phase < 0.5 ? 1.0f : -1.0f;
            case 4:  return held;
            case 5:  drift += (target - drift) * static_cast<float> (increment * 4); return drift;
            default: return static_cast<float> (std::sin (twoPi * phase));
        }
    }
};

//==============================================================================
/** A delay line read at fractional positions. */
struct DelayLine
{
    std::vector<float> buffer;
    int write = 0;

    void prepare (int maxSamples)
    {
        buffer.assign (static_cast<size_t> (std::max (4, maxSamples)), 0.0f);
        write = 0;
    }

    void clear()    { std::fill (buffer.begin(), buffer.end(), 0.0f); }
    int size() const { return static_cast<int> (buffer.size()); }

    void push (float x)
    {
        buffer[static_cast<size_t> (write)] = x;
        write = (write + 1) % size();
    }

    /** The sample delaySamples ago (at least 1), linear interpolation. */
    float read (float delaySamples) const
    {
        const auto d = juce::jlimit (1.0f, static_cast<float> (size() - 2), delaySamples);
        const auto whole = static_cast<int> (d);
        const auto frac = d - static_cast<float> (whole);
        const auto n = size();
        const auto i0 = (write - whole + n) % n, i1 = (i0 - 1 + n) % n;
        return buffer[static_cast<size_t> (i0)] + (buffer[static_cast<size_t> (i1)] - buffer[static_cast<size_t> (i0)]) * frac;
    }

    float readInt (int delaySamples) const
    {
        const auto d = juce::jlimit (1, size() - 1, delaySamples);
        return buffer[static_cast<size_t> ((write - d + size()) % size())];
    }
};

/** A Schroeder all-pass: smears without colouring. */
struct Allpass
{
    DelayLine line;
    int length = 1;
    float gain = 0.5f;

    void prepare (int samples, float g)
    {
        length = std::max (1, samples);
        gain = g;
        line.prepare (length + 2);
    }

    float process (float x)
    {
        const auto delayed = line.readInt (length);
        const auto y = delayed - gain * x;
        line.push (x + gain * delayed);
        return y;
    }
};

/** A level follower for ducking and meters. */
struct Follower
{
    float value = 0, attackCoefficient = 0, releaseCoefficient = 0;

    void prepare (double sampleRate, float attackMs = 5, float releaseMs = 200)
    {
        attackCoefficient = static_cast<float> (std::exp (-1000.0 / (attackMs * sampleRate)));
        releaseCoefficient = static_cast<float> (std::exp (-1000.0 / (releaseMs * sampleRate)));
    }

    float process (float x)
    {
        const auto level = std::abs (x);
        value = level > value ? attackCoefficient * value + (1 - attackCoefficient) * level : releaseCoefficient * value + (1 - releaseCoefficient) * level;
        return value;
    }
};

/** A parameter smoothed over a few milliseconds, so knobs don't zip. */
struct Smoothed
{
    float current = 0, target = 0, coefficient = 0.001f;

    void prepare (double sampleRate, float ms = 20)  { coefficient = static_cast<float> (1 - std::exp (-1000.0 / (ms * sampleRate))); }
    void set (float v)                                { target = v; }
    void snap (float v)                               { current = target = v; }
    float next()                                      { current += coefficient * (target - current); return current; }
};

//==============================================================================
/** Tempo divisions for synced times: name and length in beats. */
struct Division
{
    const char* name;
    float beats;
};

inline const std::vector<Division>& divisions()
{
    static const std::vector<Division> list {
        { "1/32", 0.125f }, { "1/16T", 1.0f / 6 }, { "1/16", 0.25f }, { "1/16.", 0.375f }, { "1/8T", 1.0f / 3 }, { "1/8", 0.5f }, { "1/8.", 0.75f },
        { "1/4T", 2.0f / 3 }, { "1/4", 1 }, { "1/4.", 1.5f }, { "1/2T", 4.0f / 3 }, { "1/2", 2 }, { "1/2.", 3 }, { "1/1", 4 }, { "2/1", 8 },
    };
    return list;
}

inline juce::StringArray divisionNames()
{
    juce::StringArray names;
    for (auto& d : divisions()) names.add (d.name);
    return names;
}

} // namespace gkp::dsp
