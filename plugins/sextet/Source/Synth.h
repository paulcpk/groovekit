#pragma once

#include "Parameters.h"
#include "Dsp.h"

#include <array>

namespace sextet
{

struct Settings
{
    struct Osc
    {
        bool on = false, sync = false;
        float morph = 2, warp = 0, coarse = 0, fine = 0, level = 0.8f, pan = 0, detune = 15, fm = 0, ring = 0;
        int unison = 1, modSource = 0, route = routeFilter1;
    };

    struct Filter
    {
        int type = filterOff;
        float cutoff = 3000, resonance = 0.15f, drive = 0, env = 0, key = 0.5f;
    };

    struct Env
    {
        float attack = 0.003f, decay = 0.4f, sustain = 0.8f, release = 0.3f, curve = 0.6f;
    };

    struct Lfo
    {
        int shape = 0, sync = 0;
        float rate = 1;
    };

    struct Mod
    {
        int source = sourceOff, target = targetOff;
        float amount = 0;
    };

    std::array<Osc, numOscs> oscs;
    std::array<Filter, 2> filters;
    int filterRouting = 0;
    float filterBalance = 0;
    Env amp, env2, env3;
    std::array<Lfo, numLfos> lfos;
    std::array<Mod, numMods> mods;
    int driveType = 0;
    float drive = 0, chorusRate = 0.4f, chorusDepth = 0.5f, chorusMix = 0;
    int delaySync = 6;
    float delayFeedback = 0.4f, delayMix = 0, delayTone = 5000;
    bool delayPingPong = true;
    float reverbSize = 0.6f, reverbDamp = 0.4f, reverbMix = 0;
    int voiceMode = poly;
    float glide = 0, velocity = 0.5f, bendRange = 2, gain = 0.4f;
    double tempo = 120;
    float modWheel = 0, aftertouch = 0;
};

/** The synth: each voice runs the six oscillators (each in up to four unison copies across the
    field, morphing between shapes, warped, modulated, rung or synced by another), routes them
    through the two filters, and shapes them with the amp envelope; the two other envelopes, the
    three LFOs and the performance sources move whatever the matrix says. The voices' sum goes
    through drive, chorus, delay and reverb. */
class Synth
{
public:
    static constexpr int maxVoices = 12, maxUnison = 4;

    void prepare (double sampleRate);
    void reset();
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Settings&);
    int countActiveVoices() const;

    /** The LFOs' latest values, for the editor. */
    std::array<float, numLfos> lfoValues {};

private:
    /** What the matrix moves this sample, summed over the slots. */
    struct Mods
    {
        std::array<float, numOscs + 1> pitch {};   // [0] all, in semitones (an amount of 1 is an octave)
        std::array<float, numOscs> level {};
        float morph = 0, warp = 0, fm = 0, ring = 0, pan = 0, f1Cutoff = 0, f1Res = 0, f2Cutoff = 0, f2Res = 0, drive = 0, detune = 0;
    };

    struct Voice
    {
        int note = -1;
        float velocity = 1, randomValue = 0;
        bool held = false;
        uint64_t started = 0;
        double frequency = 440, target = 440;
        std::array<std::array<std::array<double, 2>, maxUnison>, numOscs> phases {};
        std::array<float, numOscs> outputs {};
        gkp::dsp::Envelope amp, env2, env3;
        std::array<std::array<std::array<gkp::dsp::Svf, 2>, 2>, 2> filters {};   // filter, stage, side
        juce::Random random;

        bool isActive() const   { return amp.isActive(); }
    };

    double sampleRate = 48000;
    std::array<Voice, maxVoices> voices;
    std::vector<int> heldNotes, sustained;
    bool sustainPedal = false;
    uint64_t noteCounter = 0;
    double lastFrequency = 0;
    float bend = 0;
    std::array<gkp::dsp::Lfo, numLfos> lfos;
    std::array<float, numLfos> lfoRateMod {};

    // Effects.
    gkp::dsp::DelayLine chorusLines[2], delayLines[2];
    double chorusPhase = 0;
    gkp::dsp::OnePole delayTone[2];
    struct Comb { gkp::dsp::DelayLine line; int length = 1; float store = 0; };
    std::array<std::array<Comb, 4>, 2> combs;
    std::array<std::array<gkp::dsp::Allpass, 2>, 2> allpasses;
    gkp::dsp::DcBlocker dc[2];

    void handle (const juce::MidiMessage&, Settings&);
    void noteOn (int note, float velocity, const Settings&);
    void noteOff (int note, const Settings&);
    void startVoice (Voice&, int note, float velocity, const Settings&, bool retrigger);
    Mods modulation (const Voice&, const Settings&, float ampValue, float env2Value, float env3Value) const;
    void renderVoice (Voice&, const Settings&, float& left, float& right);
    void effects (float& left, float& right, const Settings&);
};

} // namespace sextet
