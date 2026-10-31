#pragma once

#include "Parameters.h"
#include "Voices.h"

namespace groove9
{

/** One block's settings, read from the parameters. */
struct Settings
{
    std::array<float, numVoices> level {}, pan {};
    std::array<float, 3> tomTune {}, tomDecay {};
    float bdTune = 0.5f, bdAttack = 0.5f, bdDecay = 0.5f, sdTune = 0.5f, sdTone = 0.5f, sdSnappy = 0.5f;
    float chDecay = 0.6f, ohDecay = 0.6f, crTune = 0.5f, rdTune = 0.5f;
    float accent = 0.5f, drive = 0, gain = 0.7f;
};

/** The machine: the drums modelled circuits, the hats and cymbals recordings played back at the
    tune's speed through a decay; each mixed by its level and pan, then the drive and the volume. */
class Synth
{
public:
    Synth();
    ~Synth();

    void prepare (double sampleRate);
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Settings&);

    /** The row (and voice) a note strikes, or -1. */
    static int rowForNote (int note);
    static int noteForRow (int row);

    using Voice = rhythm::dsp::Voice<Settings>;

private:
    double sampleRate = 48000;
    std::vector<std::unique_ptr<Voice>> voices;
    juce::SmoothedValue<float> gain;
    bool fresh = true;

    void strike (int row, float velocity, const Settings&);
};

} // namespace groove9
