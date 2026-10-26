#pragma once

#include "SampleCache.h"

#include <juce_audio_basics/juce_audio_basics.h>

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
};

struct Config
{
    std::vector<Layer> layers;
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

    /** Swaps the playing configuration and silences all voices. Call off the audio thread. */
    void setConfig (std::shared_ptr<const Config>);

    /** Adds this block's output to the first two channels of buffer. Events must be sorted by offset. */
    void process (juce::AudioBuffer<float>&, int startSample, int numSamples, const std::vector<MidiEvent>&);

    void allNotesOff();

    int getNumActiveVoices() const;

private:
    struct Voice
    {
        const Layer* layer = nullptr;
        int note = 0;
        uint64_t age = 0;
        double position = 0;
        double increment = 1;
        float gainL = 0, gainR = 0;
        juce::ADSR envelope;
        bool active = false;
        int stealSamplesLeft = 0;   // > 0 while fading out after being stolen

        bool isStolen() const { return stealSamplesLeft > 0; }
    };

    juce::CriticalSection configLock;
    std::shared_ptr<const Config> config;
    std::vector<Voice> voices;
    double sampleRate = 48000;
    int stealFadeSamples = 144;
    uint64_t nextAge = 0;

    void noteOn (int note, int velocity);
    void noteOff (int note);
    void startVoice (const Layer&, int note, int velocity);
    Voice* findFreeVoice();
    void stealOldestVoice();
    void renderVoices (juce::AudioBuffer<float>&, int startSample, int numSamples);
    void renderVoice (Voice&, float* left, float* right, int numSamples);
};

} // namespace gk::sampler
