#pragma once

#include "Parameters.h"
#include "Dsp.h"

namespace quartet
{

/** One block's settings, read from the parameters. */
struct Settings
{
    struct Op
    {
        int wave = sine;
        float ratio = 1, fine = 0, level = 1, velocity = 0.5f, keyScale = 0;
        float attack = 0.002f, decay = 0.5f, sustain = 0, release = 0.3f;
    };

    int algorithm = 0;
    float feedback = 0, fmLevel = 1, vaLevel = 0;
    std::array<Op, 4> ops;
    int vaShape = vaSaw, vaUnison = 1;
    float vaCoarse = 0, vaFine = 0, vaWidth = 0.5f, vaSub = 0, vaDetune = 0;
    int chord = chordOff;
    float chordSpread = 0, chordDetune = 0;
    int filterType = lowpass12;
    float cutoff = 8000, resonance = 0, filterEnv = 0, keyTrack = 0.5f;
    float filterAttack = 0.003f, filterDecay = 0.3f, filterSustain = 0, filterRelease = 0.3f;
    float ampAttack = 0.002f, ampDecay = 0.5f, ampSustain = 1, ampRelease = 0.3f;
    int lfoShape = 0, lfoTarget = lfoOff;
    float lfoRate = 5, lfoDepth = 0;
    int chorusMode = 0;
    float chorusDepth = 0.6f, drive = 0, tone = 0;
    int voiceMode = poly;
    float glide = 0, velocity = 0.5f, bendRange = 2, gain = 0.4f;
};

/** The synth: every voice plays the key and its chord memory's notes, each note through the
    four operators (in the algorithm's wiring, with their own envelopes) and the analogue layer
    (a band-limited oscillator in up to three unison copies, with a sub); the notes are mixed,
    filtered with the filter's envelope and shaped by the amp envelope. After the voices: drive,
    tone, the chorus and the volume. */
class Synth
{
public:
    static constexpr int maxVoices = 10, maxNotes = 5, maxUnison = 3;

    void prepare (double sampleRate);
    void reset();
    void render (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Settings&);
    int countActiveVoices() const;

    /** One sample of an operator wave at a phase (0-1). */
    static float opWave (int wave, double phase);

private:
    struct Line
    {
        int semitones = 0;
        float cents = 0, pan = 0;
        std::array<double, 4> phases {};
        std::array<float, 4> outputs {};
        float feedbackOut = 0;
        std::array<gkp::dsp::Envelope, 4> envelopes;
        std::array<std::array<double, 2>, maxUnison> vaPhases {};
        double subPhase = 0;
    };

    struct Voice
    {
        int note = -1;
        float velocity = 1;
        bool held = false;
        uint64_t started = 0;
        double frequency = 440, target = 440;
        std::array<Line, maxNotes> lines;
        int lineCount = 1;
        gkp::dsp::Envelope amp, filterEnvelope;
        std::array<std::array<gkp::dsp::Svf, 2>, 2> filters;   // stage, side
        juce::Random random;

        bool isActive() const   { return amp.isActive(); }
    };

    double sampleRate = 48000;
    std::array<Voice, maxVoices> voices;
    std::vector<int> heldNotes;
    bool sustainPedal = false;
    std::vector<int> sustained;
    uint64_t noteCounter = 0;
    double lastFrequency = 0;
    float bend = 0;
    gkp::dsp::Lfo lfo;
    gkp::dsp::OnePole toneLow[2], toneHigh[2];
    gkp::dsp::DelayLine chorusLines[2];
    double chorusPhase[2] { 0, 0.25 };
    gkp::dsp::DcBlocker dc[2];

    void handle (const juce::MidiMessage&, const Settings&);
    void noteOn (int note, float velocity, const Settings&);
    void noteOff (int note, const Settings&);
    void startVoice (Voice&, int note, float velocity, const Settings&, bool retrigger);
    void renderVoice (Voice&, const Settings&, float lfoValue, float& left, float& right);
    void renderLine (Voice&, Line&, const Settings&, double baseHz, float lfoValue, float& left, float& right);
};

} // namespace quartet
