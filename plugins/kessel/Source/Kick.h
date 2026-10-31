#pragma once

#include "Parameters.h"
#include "Dsp.h"

#include <juce_dsp/juce_dsp.h>

namespace kessel
{

/** Every parameter's value, read once a block. */
struct Settings
{
    int clickType = clickTok;
    float clickLevel = 0.35f, clickTone = 2500, clickDecay = 10;
    int bodyShape = shapeSine;
    float tune = 29;
    bool followKeys = true;
    float hold = 30, decay = 380, sub = 0.25f;
    float pitchStart = 38, pitchTime = 38, pitchCurve = 0.45f, glide = 0;
    int driveType = driveWarm;
    float drive = 8, crunch = 0, crunchFreq = 900, tone = 14000;
    float rumble = 0, rumbleSize = 1.4f, rumbleTone = 140, rumbleDrive = 0.35f, rumbleDuck = 0.85f;
    float compress = 0.2f, clip = 0.15f, lowCut = 20, velocity = 0.3f, gain = -4;

    /** The tail's note for a key played. */
    float tailNote (int key) const      { return followKeys ? tune + static_cast<float> (key - rootKey) : tune; }

    /** The dive, in semitones over the tail, t seconds into a kick; and how far along the kick's
        length it is (0-1), which the glide follows. */
    float dive (double t) const;
    float along (double t) const        { return static_cast<float> (std::min (1.0, t / std::max (0.001, length()))); }

    /** How many semitones over the tail the body is, t seconds into a kick. */
    float pitchOffset (double t) const  { return dive (t) + glide * along (t); }

    /** Seconds from the kick's start to where its body has died away (-60 dB). */
    double length() const               { return (hold + decay) / 1000.0; }
};

/** An RBJ peaking filter, for the crunch: a boost in the mids before the drive. */
struct Peak
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void set (double sampleRate, float hz, float gainDb, float q);
    float process (float x);
    void reset()    { z1 = z2 = 0; }
};

/** The kick machine: one kick at a time (a new one fades the last out over a few milliseconds),
    then the rumble, compression, clipping, low cut and volume over everything. Mono. */
class Kick
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Starts a kick: the key sets its pitch when keys are followed, velocity 0-1. */
    void trigger (int key, float velocity);

    /** Writes numSamples of output. When rumbleOut is given, the rumble alone goes there too (for
        the display). Blocks of any length. */
    void render (float* out, int numSamples, const Settings&, float* rumbleOut = nullptr);

    /** The fractional latency of the drive's oversampling, in samples. */
    float getLatency() const            { return latency; }

    /** The compressor's gain reduction just now, in dB (positive). */
    float getGainReduction() const      { return gainReduction; }

private:
    struct Voice
    {
        bool active = false;
        double t = 0, phase = 0, subPhase = 0, clickPhase = 0;
        int key = rootKey;
        float velocity = 1, fade = 1, fadeStep = 0;
        gkp::dsp::Svf clickFilter;
    };

    double rate = 48000;
    int maxBlock = 512;
    float latency = 0;
    std::array<Voice, 2> voices;
    int current = 0;
    juce::Random noise;

    Peak crunch;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::vector<float> pre, clean, clicks;
    int crushHold = 0;
    float crushValue = 0;
    gkp::dsp::Svf toneFilter, toneFilter2;
    gkp::dsp::OnePole dc;      // a high-pass at a few hertz for what asymmetric drive leaves

    // The rumble: four delays in a feedback matrix, filtered dark, driven, ducked under the kick.
    static constexpr int lines = 4;
    std::array<std::vector<float>, lines> delays;
    std::array<int, lines> lengths {}, positions {};
    std::array<float, lines> damping {}, feedback {};
    float dampingCoefficient = 0;
    gkp::dsp::Svf rumbleIn, rumbleLow1, rumbleLow2, rumblePost, rumbleHigh;
    double sinceTrigger = 1e9;
    float duck = 1;

    float envelope = 0, gainReduction = 0;
    gkp::dsp::Svf lowCut;

    void renderVoice (Voice&, const Settings&, float& body, float& sub, float& click);
    float shape (float x, int type, float driveDb);
    void setRumble (const Settings&);
    float renderRumble (float in, const Settings&);
};

} // namespace kessel
