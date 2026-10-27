#pragma once

#include "Parameters.h"

#include <array>

namespace triad
{

/** One block's settings, read from the parameters. */
struct Settings
{
    struct Osc
    {
        int shape = saw, mod = mix;
        float coarse = 0, fine = 0, level = 1, pan = 0, phase = 0, detune = 0;
    };

    Osc osc[3];
    int unisonVoices = 1;
    float unisonDetune = 0, unisonSpread = 0;
    int filterType = lowpass24;
    float cutoff = 4000, resonance = 0, filterEnv = 0, keyTrack = 0;
    juce::ADSR::Parameters amp, filter;
    int lfoShape = 0, lfoTarget = lfoOff;
    float lfoRate = 1, lfoDepth = 0;
    int voiceMode = poly;
    float glide = 0, velocity = 0.5f, bendRange = 2, gain = 0.5f;
};

/** The synth: three oscillators per voice (each through up to seven unison copies, stereo), the
    second and third able to frequency- or ring-modulate the one before; a state-variable filter
    with its own envelope; an amp envelope; one LFO; poly, mono or legato with glide. */
class Synth
{
public:
    static constexpr int maxVoices = 16, maxUnison = 7;

    void prepare (double sampleRate);
    void reset();

    /** Renders a block into an empty stereo buffer, playing the MIDI at its sample positions. */
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Settings&);

    /** Voices sounding now. */
    int countActiveVoices() const;

    /** One sample of an oscillator shape at a phase (0-1), band-limited for a step of `increment`. */
    static float waveform (int shape, double phase, double increment, juce::Random&);

private:
    struct Filter
    {
        float ic1 = 0, ic2 = 0;
        float process (float x, float g, float k, int type);
    };

    struct Voice
    {
        int note = -1;
        float velocity = 1;
        bool held = false;
        uint64_t started = 0;
        double frequency = 440, target = 440;
        double phases[3][maxUnison][2] {};
        float outputs[3][maxUnison][2] {};
        juce::ADSR amp, filterEnvelope;
        Filter filters[2][2];   // stage, side
        juce::Random random;

        bool isActive() const   { return amp.isActive(); }
    };

    double sampleRate = 48000;
    std::array<Voice, maxVoices> voices;
    std::vector<int> heldNotes;   // mono and legato: in the order pressed
    bool sustainPedal = false;
    std::vector<int> sustained;   // notes released while the pedal is down
    uint64_t noteCounter = 0;
    double lastFrequency = 0;
    float bend = 0;               // in semitones
    double lfoPhase = 0;
    float lfoHeld = 0;
    juce::Random random;

    void handle (const juce::MidiMessage&, const Settings&);
    void noteOn (int note, float velocity, const Settings&);
    void noteOff (int note, const Settings&);
    void startVoice (Voice&, int note, float velocity, const Settings&, bool retrigger);
    float nextLfo (const Settings&);
    void renderVoice (Voice&, const Settings&, float lfo, float& left, float& right);
};

} // namespace triad
