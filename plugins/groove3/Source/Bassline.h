#pragma once

#include "Dsp.h"
#include "Voices.h"
#include "Parameters.h"

#include <juce_dsp/juce_dsp.h>

namespace groove3
{

/** The knobs and switches as the voice reads them. */
struct Settings
{
    float tuning = 0, cutoff = 0.42f, resonance = 0.7f, envMod = 0.5f, decayMs = 450, accent = 0.6f, volumeDb = -3;
    int waveform = saw;
    float overdrive = 0.25f, accentDecayMs = 200, ampDecayMs = 3000, softAttackMs = 3, slideMs = 60;
    float tracking = 0, filterFm = 0, sub = 0;
    int subOctave = 0, sweepSpeed = sweepNormal, resonanceRange = 0, muffler = mufflerOff;
    float distortion = 0;
};

/** The voice, one note at a time, after the original's circuits:

    - the oscillator: a saw (its ramp bowed a little, as a capacitor charges), or a square shaped
      from it by a transistor (edges not quite sharp, tops drooping), and a square sub oscillator
      an octave or two down;
    - the filter: a four-pole diode ladder, its stages loading one another (so it falls about
      18 dB an octave near the cutoff, and its resonance is rounder than a transistor ladder's),
      the diodes saturating as the level rises, its resonance fed back through a high-pass so
      the bass stays when it's turned up;
    - the main envelope (the filter's): a decay from the top on every note that isn't slid to,
      shorter on accents;
    - accents: louder, through the main envelope into the volume, and a filter sweep through a
      capacitor that charges on each accent and drains slowly, so accents in a row climb;
    - the volume envelope, with a soft attack on notes without an accent, its own decay, and a
      quick fall when the gate closes;
    - slides: the pitch glides on an RC curve, the envelopes run on;
    - after the volume: the muffler (a soft clipper that keeps the bass), then the distortion of
      a pedal.

    It all runs at four times the sample rate, so the saturating parts alias less. */
class Bassline
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();

    /** A note: the envelopes start from the top. With slide (a note held on into it), the pitch
        glides there instead and the envelopes run on. */
    void noteOn (int key, bool accent, bool slide);

    /** The gate closes: the note falls away in a few milliseconds. */
    void noteOff();

    void render (float* out, int numSamples, const Settings&);

    /** The oversampling's latency, in samples. */
    float getLatency() const            { return latency; }

    /** For the panel's screen: the filter's cutoff just now, the main envelope, the accent
        sweep's charge, and whether a note sounds. */
    float getCutoffHz() const           { return shownCutoff; }
    float getEnvelope() const           { return meg; }
    float getSweep() const              { return sweepCharge; }
    bool isSounding() const             { return gate || veg > 1.0e-4f; }

private:
    double rate = 48000, fs = 192000;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::vector<float> zeros;
    float latency = 0;

    // The note.
    float target = 36, pitch = 36;
    bool gate = false, accented = false, sliding = false;

    // The oscillator.
    double phase = 0;
    int cycles = 0;
    gkp::dsp::OnePole squareDroop;

    // The envelopes and the accent sweep.
    float meg = 0;
    bool megAccented = false;       // the note that started it was accented: the shorter decay
    enum class Stage { idle, attack, decay, release } stage = Stage::idle;
    float veg = 0;
    float sweepCharge = 0, accentLevel = 0;

    // The filter: the four capacitors, their slopes the sample before, the input then, and the
    // resonance's high-pass (the low-passed feedback it takes away).
    std::array<double, 4> v {}, slope {};
    double input = 0, feedbackLow = 0;

    // After the volume.
    float fmSignal = 0;
    rhythm::dsp::Noise noise;
    gkp::dsp::OnePole fmBlock, mufflerSplit, distortionEmphasis, distortionTone;
    gkp::dsp::DcBlocker dcBlock;
    gkp::dsp::Smoothed cutoffKnob, resonanceKnob, envModKnob, accentKnob, overdriveKnob, distortionKnob, volume;
    float shownCutoff = 0;

    float filter (float x, double cutoffHz, double feedback);
};

} // namespace groove3
