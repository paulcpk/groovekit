#pragma once

#include "Parameters.h"
#include "Voices.h"

namespace groove8
{

/** One block's settings, read from the parameters. */
struct Settings
{
    std::array<float, numVoices> level {}, pan {};
    std::array<float, 3> tuning {};             // low, mid, high tom or conga
    std::array<bool, numVoices> alternate {};   // conga, claves, maracas
    float bdTone = 0.5f, bdDecay = 0.5f, bdPitch = 0, sdTone = 0.5f, sdSnappy = 0.5f;
    float cyTone = 0.5f, cyDecay = 0.5f, ohDecay = 0.3f;
    float accent = 0.5f, drive = 0, gain = 0.7f;
};

/** The sounds a note can strike: the panel's voices with their alternates. */
enum Sound { kick, snare, lowTom, midTom, highTom, lowConga, midConga, highConga, rimShot, claves, handClap, maracas, cowbell, cymbal, openHat, closedHat, numSounds };

/** The machine: each sound a model of its circuit, mixed by its row's level and pan, then the
    drive and the volume. */
class Synth
{
public:
    Synth();
    ~Synth();

    void prepare (double sampleRate);
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Settings&);

    /** The sound a note strikes, or -1. */
    static int soundForNote (int note);

    /** The panel row a sound belongs to. */
    static int rowForSound (int sound);

    /** The sound a row plays as its switch is set. */
    static int soundForRow (int row, const Settings&);
    static int noteForSound (int sound);

    using Voice = rhythm::dsp::Voice<Settings>;

private:
    double sampleRate = 48000;
    std::vector<std::unique_ptr<Voice>> voices;
    std::array<rhythm::dsp::Pulse, 6> metalOscillators;
    juce::SmoothedValue<float> gain;
    bool fresh = true;

    void strike (int sound, float velocity, const Settings&);
};

} // namespace groove8
