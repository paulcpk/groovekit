#pragma once

#include "Dsp.h"

/** What the machines' voices are built from: noise, decays, filters and oscillators, per sample. */
namespace rhythm::dsp
{

using gkp::dsp::twoPi;

/** The output's ceiling: untouched below -1 dB, peaks over it rounded off short of full scale,
    so hits landing together never clip. */
inline float ceiling (float x)
{
    constexpr float knee = 0.89f;
    const auto a = std::abs (x);
    return a <= knee ? x : std::copysign (knee + (1 - knee) * std::tanh ((a - knee) / (1 - knee)), x);
}

/** White noise, -1 to 1, from a fast generator (each voice its own, so they don't correlate). */
struct Noise
{
    uint32_t state = 0x9e3779b9;

    explicit Noise (uint32_t seed = 0x9e3779b9) : state (seed | 1) {}

    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (static_cast<int32_t> (state)) * (1.0f / 2147483648.0f);
    }
};

/** An exponential fall from the level it was struck at, by a time constant. */
struct Decay
{
    float value = 0, coefficient = 0;

    void set (double sampleRate, float seconds)    { coefficient = static_cast<float> (std::exp (-1.0 / (std::max (1.0e-5, static_cast<double> (seconds)) * sampleRate))); }
    void trigger (float level = 1)                  { value = level; }
    void stop()                                     { value = 0; }
    bool isActive() const                           { return value > 1.0e-5f; }

    float next()
    {
        const auto v = value;
        value *= coefficient;
        return v;
    }
};

/** A ramp from 1 to 0 over a time, raised to a power: the shape of a transistor gate closing,
    which holds up and then falls away quickly (power 1 is a straight line in level). */
struct Ramp
{
    float position = 1, increment = 0, power = 1;

    void set (double sampleRate, float seconds, float shape)  { increment = static_cast<float> (1.0 / (std::max (1.0e-4, static_cast<double> (seconds)) * sampleRate)); power = shape; }
    void trigger()                                              { position = 0; }
    void stop()                                                 { position = 1; }
    bool isActive() const                                       { return position < 1; }

    float next()
    {
        if (position >= 1)
            return 0;

        const auto v = std::pow (1 - position, power);
        position += increment;
        return v;
    }
};

/** RBJ's biquads: low-pass, high-pass and band-pass (0 dB at the centre). */
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setLowPass (double sampleRate, float hz, float q)    { design (sampleRate, hz, q, 0); }
    void setHighPass (double sampleRate, float hz, float q)   { design (sampleRate, hz, q, 1); }
    void setBandPass (double sampleRate, float hz, float q)   { design (sampleRate, hz, q, 2); }
    void reset()                                               { z1 = z2 = 0; }

    float process (float x)
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    void design (double sampleRate, float hz, float q, int type)
    {
        const auto w = twoPi * juce::jlimit (10.0, sampleRate * 0.49, static_cast<double> (hz)) / sampleRate;
        const auto cosw = std::cos (w), alpha = std::sin (w) / (2 * std::max (0.05, static_cast<double> (q)));
        const auto a0 = 1 + alpha;
        double nb0, nb1, nb2;

        switch (type)
        {
            case 0:  nb0 = (1 - cosw) / 2; nb1 = 1 - cosw; nb2 = nb0; break;
            case 1:  nb0 = (1 + cosw) / 2; nb1 = -(1 + cosw); nb2 = nb0; break;
            default: nb0 = alpha; nb1 = 0; nb2 = -alpha; break;
        }

        b0 = static_cast<float> (nb0 / a0);
        b1 = static_cast<float> (nb1 / a0);
        b2 = static_cast<float> (nb2 / a0);
        a1 = static_cast<float> (-2 * cosw / a0);
        a2 = static_cast<float> ((1 - alpha) / a0);
    }
};

/** A sine whose pitch may move every sample. */
struct Sine
{
    double phase = 0;

    void reset (double startPhase = 0)  { phase = startPhase; }

    float next (double hz, double sampleRate)
    {
        const auto v = static_cast<float> (std::sin (twoPi * phase));
        phase = gkp::dsp::wrap (phase + hz / sampleRate);
        return v;
    }
};

/** A resonator struck by a pulse, as a bridged-T network rings: an oscillation that starts at
    its peak and rings down with no offset (so no thump below its pitch). Round its attack off
    with a gentle low-pass, as the circuit's own capacitors do. */
struct Struck
{
    double phase = 0;

    void reset()    { phase = 0; }

    float next (double hz, double sampleRate, float tau)
    {
        const auto w = twoPi * phase;
        const auto k = 1 / (twoPi * hz * std::max (1.0e-4f, tau));
        phase = gkp::dsp::wrap (phase + hz / sampleRate);
        return static_cast<float> (std::cos (w) - k * std::sin (w));
    }
};

/** A free-running pulse oscillator with smoothed edges, as the square wave of a logic-gate
    oscillator (never quite even, so it has some even harmonics too). */
struct Pulse
{
    double phase = 0;

    float next (double hz, double sampleRate, double width = 0.5)
    {
        const auto increment = hz / sampleRate;
        const auto v = gkp::dsp::waveform (4, phase, increment, width);
        phase = gkp::dsp::wrap (phase + increment);
        return v;
    }
};

/** What's left of a voice when it is struck again: faded out over a couple of milliseconds
    instead of cut, so a retrigger doesn't click. */
struct Declick
{
    float value = 0, coefficient = 0.99f;

    void prepare (double sampleRate)    { coefficient = static_cast<float> (std::exp (-1.0 / (0.0015 * sampleRate))); }
    void take (float last)              { value += last; }
    float next()                        { value *= coefficient; return value; }
};

/** A machine's voice: struck with a level and an accent, it renders a sample at a time (given
    the machine's settings and a shared input, such as the metal oscillators). Struck again while
    it sounds, what was left fades out instead of cutting off. */
template <typename Settings>
struct Voice
{
    virtual ~Voice() = default;

    virtual void prepare (double rate)
    {
        sampleRate = rate;
        declick.prepare (rate);
    }

    void strike (float velocity, float accent, const Settings& s)
    {
        declick.take (last);
        trigger (velocity, accent, s);
    }

    /** Fades out at once (a closed hat stops the open one). */
    void choke()
    {
        declick.take (last);
        stop();
    }

    float next (const Settings& s, float input)
    {
        last = (isActive() ? render (s, input) : 0.0f) + declick.next();
        return last;
    }

    double sampleRate = 48000;
    float last = 0;
    Declick declick;

protected:
    virtual void trigger (float velocity, float accent, const Settings&) = 0;
    virtual float render (const Settings&, float input) = 0;
    virtual bool isActive() const = 0;
    virtual void stop() = 0;
};

/** Linear interpolation in a table of values at evenly spaced knob positions (0-1). */
template <size_t N>
float lookup (const std::array<float, N>& table, float position)
{
    const auto x = juce::jlimit (0.0f, 1.0f, position) * static_cast<float> (N - 1);
    const auto i = std::min (static_cast<size_t> (x), N - 2);
    return table[i] + (table[i + 1] - table[i]) * (x - static_cast<float> (i));
}

/** The same in a table of time constants, between which it moves by ratio rather than amount. */
template <size_t N>
float lookupLog (const std::array<float, N>& table, float position)
{
    const auto x = juce::jlimit (0.0f, 1.0f, position) * static_cast<float> (N - 1);
    const auto i = std::min (static_cast<size_t> (x), N - 2);
    return table[i] * std::pow (table[i + 1] / table[i], x - static_cast<float> (i));
}

} // namespace rhythm::dsp
