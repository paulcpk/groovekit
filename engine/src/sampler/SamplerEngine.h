#pragma once

#include "SampleCache.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <memory>
#include <vector>

namespace gk::sampler
{

/** One playable layer, already resolved from the project model. */
struct Layer
{
    std::shared_ptr<const Sample> sample;
    int rootKey = 60;
    int keyLow = 0, keyHigh = 127;
    int velocityLow = 1, velocityHigh = 127;
    bool pitchTracking = true;
    double tuneSemitones = 0;   // transpose plus fine tune
    float gain = 1.0f;          // linear
    float pan = 0;              // -1..1, balance law: centre leaves both channels at unity
    bool oneShot = true;
    juce::ADSR::Parameters envelope { 0.0f, 0.0f, 1.0f, 0.01f };
    /** The part of the sample that plays, in frames: [start, end); a negative end is the sample's end. */
    int start = 0, end = -1;
    bool reverse = false;

    int endFrame() const    { return end < 0 ? sample->audio.getNumSamples() : end; }
};

/** A resonant 12 dB/octave state-variable filter per voice, its cutoff moved by its own envelope. */
struct Filter
{
    enum class Type { off, lowpass, highpass, bandpass };

    Type type = Type::off;
    float cutoff = 20000.0f;      // Hz
    float resonance = 0;          // 0..1
    float envelopeAmount = 0;     // octaves at the envelope's peak
    juce::ADSR::Parameters envelope { 0.0f, 0.0f, 1.0f, 0.01f };

    /** Q from resonance: 0.71 (no peak) at 0 up to 25 at 1. The UI draws the curve with the same mapping. */
    static float qFor (float resonance)     { return 0.7071f * std::pow (25.0f / 0.7071f, juce::jlimit (0.0f, 1.0f, resonance)); }
};

struct Config
{
    std::vector<Layer> layers;
    Filter filter;
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

    SamplerEngine();

    void prepare (double sampleRate, int maxBlockSize);

    /** Swaps the playing configuration. Voices keep playing when the layers still play the same
        samples (so envelopes and the filter can be changed while a note sounds); otherwise
        they're silenced. Call off the audio thread. */
    void setConfig (std::shared_ptr<const Config>);

    /** Adds this block's output to the first two channels of buffer. Events must be sorted by offset. */
    void process (juce::AudioBuffer<float>&, int startSample, int numSamples, const std::vector<MidiEvent>&);

    void allNotesOff();

    int getNumActiveVoices() const;

private:
    struct Voice
    {
        const Layer* layer = nullptr;
        size_t layerIndex = 0;
        int note = 0;
        uint64_t age = 0;
        double position = 0;
        double increment = 1;
        float gainL = 0, gainR = 0;
        juce::ADSR envelope, filterEnvelope;
        float filterState[2][2] {};   // per channel: the SVF's two integrators
        float a1 = 1, a2 = 0, a3 = 0, k = 1.4142f;
        int samplesToCoefficients = 0;
        int fadeInSamples = 0, fadeOutSamples = 0;
        bool active = false;
        int stealSamplesLeft = 0;   // > 0 while fading out after being stolen

        bool isStolen() const { return stealSamplesLeft > 0; }
    };

    juce::CriticalSection configLock;
    std::shared_ptr<const Config> config;
    std::vector<Voice> voices;
    double sampleRate = 48000;
    int stealFadeSamples = 144;
    int edgeFadeSamples = 48;
    uint64_t nextAge = 0;

    void noteOn (int note, int velocity);
    void noteOff (int note);
    void startVoice (const Layer&, int note, int velocity);
    Voice* findFreeVoice();
    void stealOldestVoice();
    void renderVoices (juce::AudioBuffer<float>&, int startSample, int numSamples);
    void renderVoice (Voice&, float* left, float* right, int numSamples);
    void updateFilter (Voice&, float envelopeLevel);
};

} // namespace gk::sampler
