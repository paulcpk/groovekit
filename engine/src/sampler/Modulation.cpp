#include "Modulation.h"

namespace gk::sampler
{

float EnvelopeShape::levelBeforeRelease (double since) const
{
    auto t = static_cast<float> (since);

    if (t < delay)
        return 0;

    t -= delay;

    if (t < attack)
        return bend (t / attack, attackCurve, attackBend);

    t -= attack;

    if (t < hold)
        return 1;

    t -= hold;

    if (t < decay)
        return 1.0f - (1.0f - sustain) * bend (t / decay, decayCurve, decayBend);

    return sustain;
}

float EnvelopeShape::levelAt (double since, double untilEnd) const
{
    auto level = levelBeforeRelease (since);

    if (untilEnd < release)
        level *= 1.0f - bend (1.0f - static_cast<float> (std::max (0.0, untilEnd)) / release, releaseCurve, releaseBend);

    return level;
}

//==============================================================================
float EnvelopeGenerator::stageLength() const
{
    switch (stage)
    {
        case Stage::delay:      return shape.delay;
        case Stage::attack:     return shape.attack;
        case Stage::hold:       return shape.hold;
        case Stage::decay:      return shape.decay;
        case Stage::release:    return shape.release;
        case Stage::sustain:
        case Stage::idle:       break;
    }

    return std::numeric_limits<float>::infinity();
}

float EnvelopeGenerator::levelNow() const
{
    const auto p = [&] (float length) { return std::min (1.0f, static_cast<float> (time) / length); };

    switch (stage)
    {
        case Stage::delay:      return 0;
        case Stage::attack:     return EnvelopeShape::bend (p (shape.attack), shape.attackCurve, shape.attackBend);
        case Stage::hold:       return 1;
        case Stage::decay:      return 1.0f - (1.0f - shape.sustain) * EnvelopeShape::bend (p (shape.decay), shape.decayCurve, shape.decayBend);
        case Stage::sustain:    return shape.sustain;
        case Stage::release:    return releaseFrom * (1.0f - EnvelopeShape::bend (p (shape.release), shape.releaseCurve, shape.releaseBend));
        case Stage::idle:       break;
    }

    return 0;
}

void EnvelopeGenerator::skipEmptyStages()
{
    // A stage of no length is passed straight away: with no attack, the first sample is at full level.
    while (stage != Stage::idle && stage != Stage::sustain && time >= stageLength())
    {
        time -= stageLength();

        switch (stage)
        {
            case Stage::delay:      stage = Stage::attack; break;
            case Stage::attack:     stage = Stage::hold; break;
            case Stage::hold:       stage = Stage::decay; break;
            case Stage::decay:      stage = Stage::sustain; break;
            case Stage::release:    stage = Stage::idle; break;
            case Stage::sustain:
            case Stage::idle:       break;
        }
    }

    // Sustained at nothing, the note is over.
    if (stage == Stage::sustain && shape.sustain <= 0.0f)
        stage = Stage::idle;
}

void EnvelopeGenerator::noteOff()
{
    if (stage == Stage::idle || stage == Stage::release)
        return;

    releaseFrom = levelNow();
    stage = Stage::release;
    time = 0;
    skipEmptyStages();
}

float EnvelopeGenerator::advance (int n)
{
    level = levelNow();

    if (stage != Stage::idle && stage != Stage::sustain)
    {
        time += n * secondsPerSample;
        skipEmptyStages();
    }

    return level;
}

//==============================================================================
float LfoSettings::wave (double phase) const
{
    const auto cycle = std::floor (phase);
    const auto f = static_cast<float> (phase - cycle);

    switch (shape)
    {
        case Shape::sine:       return std::sin (juce::MathConstants<float>::twoPi * f);
        case Shape::triangle:   return f < 0.25f ? 4.0f * f : f < 0.75f ? 2.0f - 4.0f * f : 4.0f * f - 4.0f;
        case Shape::square:     return f < 0.5f ? 1.0f : -1.0f;
        case Shape::saw:        return f < 0.5f ? 2.0f * f : 2.0f * f - 2.0f;
        case Shape::random:
        {
            // A step a cycle, the same every time the song plays.
            auto h = static_cast<uint32_t> (static_cast<int64_t> (cycle)) * 2654435761u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            return static_cast<float> (h & 0xffffff) / static_cast<float> (0x800000) - 1.0f;
        }
    }

    return 0;
}

float LfoSettings::fadeIn (double since) const
{
    const auto t = static_cast<float> (since) - delay;

    if (t < 0)
        return 0;

    return attack > 0 ? std::min (1.0f, t / attack) : 1.0f;
}

//==============================================================================
juce::AudioBuffer<float> shapeTransients (const juce::AudioBuffer<float>& in, double sampleRate, float punch, float tail)
{
    const auto channels = in.getNumChannels(), frames = in.getNumSamples();
    juce::AudioBuffer<float> out (channels, frames);

    const auto coefficient = [&] (double ms) { return static_cast<float> (std::exp (-1.0 / (ms * 0.001 * sampleRate))); };

    // Two followers each: a hit makes the quick one rise before the slow one (attack), and a
    // ringing tail keeps the long one up after the short one has fallen (tail).
    struct Follower
    {
        float attack, release, value = 0;

        float operator() (float x)
        {
            const auto c = x > value ? attack : release;
            value = x + c * (value - x);
            return value;
        }
    };

    Follower quick { coefficient (0.5), coefficient (60) }, slow { coefficient (20), coefficient (60) };
    Follower longTail { coefficient (0.5), coefficient (400) }, shortTail { coefficient (0.5), coefficient (30) };

    constexpr float floor = 1.0e-5f, strength = 1.5f;
    const auto minGain = juce::Decibels::decibelsToGain (-24.0f), maxGain = juce::Decibels::decibelsToGain (24.0f);

    for (int i = 0; i < frames; ++i)
    {
        float level = 0;

        for (int c = 0; c < channels; ++c)
            level = std::max (level, std::abs (in.getSample (c, i)));

        const auto hit = (quick (level) + floor) / (slow (level) + floor);
        const auto ring = (longTail (level) + floor) / (shortTail (level) + floor);
        const auto gain = juce::jlimit (minGain, maxGain, std::pow (hit, punch * strength) * std::pow (ring, tail * strength));

        for (int c = 0; c < channels; ++c)
            out.setSample (c, i, in.getSample (c, i) * gain);
    }

    return out;
}

} // namespace gk::sampler
