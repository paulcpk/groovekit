#include "Synth.h"

namespace groove8
{

namespace dsp = rhythm::dsp;

namespace
{
    // What the models were measured against: recordings of an original machine at known knob
    // settings. Tables run across a knob, 0 to 10 in quarters.

    // Bass drum: the body's time constant and the pitch it settles on across the decay knob; the
    // pitch falls to it in two steps, a fast chirp through the first cycle and a slower glide; the
    // click comes through the tone filter.
    constexpr std::array<float, 5> kickDecay { 0.0158f, 0.0494f, 0.176f, 0.262f, 0.495f };
    constexpr std::array<float, 5> kickEndHz { 48.4f, 49.5f, 49.6f, 49.7f, 49.9f };
    constexpr std::array<float, 5> kickTone { 110, 380, 900, 2200, 9000 };
    constexpr float kickChirpHz = 232, kickChirpTau = 0.00275f, kickHold = 0.008f, kickClick = 0.6f, kickStrike = 0.22f;

    // Snare: two resonators, the second brought in by the tone; noise by the snappy.
    constexpr float snareLowHz = 172, snareHighHz = 339, snareLowTau = 0.0284f, snareHighTau = 0.012f;
    constexpr float snareNoiseTau = 0.032f, snareNoiseHold = 0.008f;
    constexpr std::array<float, 5> snareLowGain { 1, 1, 0.85f, 0.55f, 0.38f };
    constexpr std::array<float, 5> snareHighGain { 0.01f, 0.36f, 1.05f, 1.7f, 2.1f };
    constexpr std::array<float, 5> snareSnappy { 0, 0.15f, 0.95f, 1.6f, 2.0f };
    constexpr std::array<float, 5> snareNoiseHz { 8000, 8500, 9000, 10000, 11000 };

    // Toms and congas: the pitch across the tuning knob, and how long they ring.
    constexpr std::array<std::array<float, 5>, 6> drumHz { {
        { 81.5f, 83.8f, 88.5f, 93.4f, 99.7f },
        { 124.7f, 128.0f, 135.2f, 144.2f, 154.0f },
        { 170.3f, 176.1f, 187.4f, 201.3f, 212.2f },
        { 184.7f, 189.4f, 200.4f, 211.6f, 224.7f },
        { 259.2f, 265.1f, 281.1f, 300.0f, 319.4f },
        { 375.1f, 387.4f, 412.4f, 442.4f, 466.4f },
    } };
    constexpr std::array<float, 6> drumDecay { 0.088f, 0.057f, 0.042f, 0.077f, 0.0387f, 0.0342f };
    constexpr std::array<float, 6> drumNoise { 0.015f, 0.015f, 0.015f, 0, 0, 0 };

    constexpr float clavesTau = 0.0075f;

    // The six square-wave oscillators of the metal voices (and two of them the cowbell's).
    constexpr std::array<double, 6> metalHz { 212.9, 315.2, 381.8, 540.0, 558.5, 823.6 };

    // Open hat: how long its gate stays open across the decay knob.
    constexpr std::array<float, 5> openHatLength { 0.103f, 0.212f, 0.37f, 0.52f, 0.6f };

    // Cymbal: the tail's time constant across the decay knob, how loud the tail is against the
    // first quick fall, the bands, and the tone's balance of body and sizzle.
    constexpr std::array<float, 5> cymbalDecay { 0.215f, 0.36f, 0.52f, 0.68f, 0.74f };
    constexpr std::array<float, 5> cymbalBodyGain { 1.15f, 0.9f, 0.7f, 0.55f, 0.42f };
    constexpr std::array<float, 5> cymbalSizzleGain { 0.9f, 1.05f, 1.2f, 1.3f, 1.4f };
    constexpr float cymbalTail = 0.45f, cymbalFastTau = 0.04f, cymbalSizzleRatio = 0.15f;
    constexpr float cymbalLowCut = 3000, cymbalBodyHz = 7500, cymbalSizzleHz = 9500;

    // Hats: steep low cuts, a band and (the closed one) a brighter top.
    constexpr float openHatLowCut = 5500, openHatBandHz = 7500, closedHatLowCut = 6000, closedHatBandHz = 8500, closedHatTopHz = 9500;

    // Each sound's level against the others, in dB, so the kit sits together with its levels
    // at the same setting (the bass drum on top, as on the original).
    constexpr std::array<float, numSounds> soundTrim { 6.5f, -2.6f, -7.8f, -7.8f, -7.8f, -7.8f, -7.8f, -7.8f, -2.8f, -0.7f, 3.9f, -4.3f, -2.3f, 5.8f, 0.3f, 4.9f };

    float softClip (float x)    { return std::tanh (x); }

/** A bridged-T resonator struck by a pulse: a sine whose pitch falls a little as it settles,
    holding briefly before it decays; the pulse itself comes through the tone filter as the click. */
struct Kick : Synth::Voice
{
    dsp::Sine osc;
    dsp::Decay body, chirp, glide, strike, click;
    gkp::dsp::OnePole toneFilter;
    float ratio = 1, endHz = 50, clickLevel = 0;
    int held = 0;

    void trigger (float velocity, float accent, const Settings& s) override
    {
        const auto tau = dsp::lookupLog (kickDecay, s.bdDecay);
        ratio = std::pow (2.0f, s.bdPitch / 12);
        body.set (sampleRate, tau);
        body.trigger (velocity);
        chirp.set (sampleRate, kickChirpTau);
        chirp.trigger (kickChirpHz);
        // The slower glide lasts longer and falls less the longer the decay.
        const auto glideTau = 0.016f + 0.12f * tau;
        glide.set (sampleRate, glideTau);
        glide.trigger (16 * std::sqrt (0.03f / glideTau));
        strike.set (sampleRate, 0.004f);
        strike.trigger (kickStrike);
        endHz = dsp::lookup (kickEndHz, s.bdDecay);
        held = static_cast<int> (kickHold * sampleRate);
        click.set (sampleRate, 0.0004f);
        click.trigger (1);
        clickLevel = velocity * kickClick * (0.5f + s.bdTone) * (1 + accent);
        toneFilter.reset();
        toneFilter.set (sampleRate, dsp::lookupLog (kickTone, s.bdTone));
        osc.reset();
    }

    float render (const Settings&, float) override
    {
        const auto hz = (endHz + chirp.next() + glide.next()) * ratio;
        float level = body.value;

        if (held > 0)
            --held;
        else
            level = body.next();

        const auto y = osc.next (hz, sampleRate) * level * (1 + strike.next()) + toneFilter.lowpass (click.next()) * clickLevel;
        return softClip (y * 1.2f) / 1.2f;
    }

    bool isActive() const override      { return body.isActive(); }
    void stop() override                { body.stop(); }
};

/** Two resonators for the shell, the tone bringing in the higher, and noise for the snares. */
struct Snare : Synth::Voice
{
    dsp::Struck low, high;
    dsp::Decay lowEnv, highEnv, noiseEnv;
    dsp::Noise noise { 1234 };
    dsp::Biquad noiseHigh, noiseLow;
    gkp::dsp::OnePole round, roundHigh;
    float lowGain = 0, highGain = 0, noiseGain = 0;
    int noiseHeld = 0, noiseRise = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        lowEnv.set (rate, snareLowTau);
        highEnv.set (rate, snareHighTau);
        noiseEnv.set (rate, snareNoiseTau);
        noiseHigh.setHighPass (rate, 1200, 0.7f);
        round.set (rate, 2500);
        roundHigh.set (rate, 700);
    }

    void trigger (float velocity, float accent, const Settings& s) override
    {
        lowGain = velocity * dsp::lookup (snareLowGain, s.sdTone);
        highGain = velocity * dsp::lookup (snareHighGain, s.sdTone) * (1 + 0.3f * accent);
        noiseGain = velocity * dsp::lookup (snareSnappy, s.sdSnappy) * (1 + 0.5f * accent);
        noiseLow.setLowPass (sampleRate, dsp::lookupLog (snareNoiseHz, s.sdTone), 0.7f);
        lowEnv.trigger (1);
        highEnv.trigger (1);
        noiseEnv.trigger (1);
        noiseRise = noiseHeld = static_cast<int> (snareNoiseHold * sampleRate);
        low.reset();
        high.reset();
    }

    float render (const Settings&, float) override
    {
        const auto shell = round.lowpass (low.next (snareLowHz, sampleRate, snareLowTau) * lowEnv.next() * lowGain)
                         + roundHigh.lowpass (high.next (snareHighHz, sampleRate, snareHighTau) * highEnv.next() * highGain);

        // The snares swell for a few milliseconds, hold, then fall away.
        float level = noiseEnv.value;

        if (noiseHeld > 0)
            level *= 1 - static_cast<float> (--noiseHeld) / static_cast<float> (noiseRise) * 0.6f;
        else
            level = noiseEnv.next();

        const auto snares = noiseLow.process (noiseHigh.process (noise.next())) * level * noiseGain;
        return shell + snares;
    }

    bool isActive() const override      { return lowEnv.isActive() || noiseEnv.isActive(); }
    void stop() override                { lowEnv.stop(); highEnv.stop(); noiseEnv.stop(); }
};

/** A tom or a conga: a resonator at the tuning's pitch, the toms with a breath of noise. */
struct Drum : Synth::Voice
{
    explicit Drum (int k) : kind (k) {}

    const int kind;     // 0-2 toms, 3-5 congas
    dsp::Struck osc;
    dsp::Decay env, drop, noiseEnv;
    dsp::Noise noise { 777u + static_cast<uint32_t> (kind) };
    dsp::Biquad noiseFilter;
    gkp::dsp::OnePole round;
    float hz = 100, tau = 0.05f, gain = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        round.set (rate, 1000);
    }

    void trigger (float velocity, float accent, const Settings& s) override
    {
        const auto& table = drumHz[static_cast<size_t> (kind)];
        hz = dsp::lookup (table, s.tuning[static_cast<size_t> (kind % 3)]);
        tau = drumDecay[static_cast<size_t> (kind)];
        gain = velocity * (1 + 0.2f * accent);
        env.set (sampleRate, tau);
        env.trigger (1);
        drop.set (sampleRate, 0.02f);
        drop.trigger (kind < 3 ? 0.015f : 0.004f);
        noiseEnv.set (sampleRate, 0.03f);
        noiseEnv.trigger (drumNoise[static_cast<size_t> (kind)]);
        noiseFilter.setBandPass (sampleRate, hz * 2.5f, 0.8f);
        osc.reset();
    }

    float render (const Settings&, float) override
    {
        auto y = osc.next (hz * (1 + drop.next()), sampleRate, tau) * env.next();

        if (noiseEnv.isActive())
            y += noiseFilter.process (noise.next()) * noiseEnv.next();

        return round.lowpass (y) * gain * 3;
    }

    bool isActive() const override      { return env.isActive(); }
    void stop() override                { env.stop(); noiseEnv.stop(); }
};

/** Two short resonators driven hard into a clipper, high-passed: the rim shot. */
struct RimShot : Synth::Voice
{
    dsp::Sine a, b;
    dsp::Decay env;
    dsp::Biquad highPass;
    float gain = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        env.set (rate, 0.002f);
        highPass.setHighPass (rate, 400, 0.7f);
    }

    void trigger (float velocity, float accent, const Settings&) override
    {
        gain = velocity * (1 + 0.3f * accent);
        env.trigger (1);
        a.reset();
        b.reset();
    }

    float render (const Settings&, float) override
    {
        const auto e = env.next();
        const auto y = (a.next (468, sampleRate) + 1.6f * b.next (1700, sampleRate)) * e * 5;
        return highPass.process (softClip (y)) * gain;
    }

    bool isActive() const override      { return env.isActive(); }
    void stop() override                { env.stop(); }
};

/** One high resonator, briefly: the claves. */
struct Claves : Synth::Voice
{
    dsp::Struck osc;
    dsp::Decay env;
    gkp::dsp::OnePole round;
    float gain = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        env.set (rate, clavesTau);
        round.set (rate, 6000);
    }

    void trigger (float velocity, float accent, const Settings&) override
    {
        gain = velocity * (1 + 0.3f * accent);
        env.trigger (1);
        osc.reset();
    }

    float render (const Settings&, float) override  { return round.lowpass (osc.next (2422, sampleRate, clavesTau) * env.next()) * gain; }
    bool isActive() const override                   { return env.isActive(); }
    void stop() override                             { env.stop(); }
};

/** Band-passed noise through three quick sawtooth bursts, then a longer tail for the room. */
struct HandClap : Synth::Voice
{
    dsp::Noise noise { 4242 };
    dsp::Biquad band, lowCut, tailBand;
    dsp::Decay tail, tailLong;
    double t = 1;
    float gain = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        band.setBandPass (rate, 1100, 1.4f);
        lowCut.setHighPass (rate, 700, 0.7f);
        tailBand.setBandPass (rate, 1100, 1.6f);
        tail.set (rate, 0.024f);
        tailLong.set (rate, 0.09f);
    }

    void trigger (float velocity, float accent, const Settings&) override
    {
        gain = velocity * (1 + 0.3f * accent);
        t = 0;
        tail.trigger (0.75f);
        tailLong.trigger (0.25f);
    }

    float render (const Settings&, float) override
    {
        // Three bursts 11 ms apart, each falling fast; the tail swells in after them.
        constexpr double spacing = 0.0115, burstTau = 0.003, tailStart = 0.031;
        const auto n = lowCut.process (noise.next());
        float burst = 0, tailLevel = 0;

        if (t < tailStart)
            burst = static_cast<float> (std::exp (-std::fmod (t, spacing) / burstTau));
        else
            tailLevel = tail.next() + tailLong.next();

        t += 1 / sampleRate;
        return (band.process (n) * burst + tailBand.process (n) * tailLevel) * gain * 2;
    }

    bool isActive() const override      { return t < 0.031 || tailLong.isActive(); }
    void stop() override                { t = 1; tail.stop(); tailLong.stop(); }
};

/** High-passed noise that swells and stops: the maracas. */
struct Maracas : Synth::Voice
{
    dsp::Noise noise { 9001 };
    dsp::Biquad highPass, lowPass;
    double t = 1;
    float gain = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        highPass.setHighPass (rate, 7000, 0.9f);
        lowPass.setLowPass (rate, 16000, 0.7f);
    }

    void trigger (float velocity, float accent, const Settings&) override
    {
        gain = velocity * (1 + 0.3f * accent);
        t = 0;
    }

    float render (const Settings&, float) override
    {
        constexpr double rise = 0.008, length = 0.032;
        const auto env = t < rise ? t / rise : std::max (0.0, 1 - (t - rise) / (length - rise));
        t += 1 / sampleRate;
        return lowPass.process (highPass.process (noise.next())) * static_cast<float> (env * env) * gain * 1.4f;
    }

    bool isActive() const override      { return t < 0.032; }
    void stop() override                { t = 1; }
};

/** Two of the square oscillators through a band-pass, with a fast then a slow decay. */
struct Cowbell : Synth::Voice
{
    dsp::Pulse a, b;
    dsp::Decay fast, slow;
    dsp::Biquad band, lowCut;
    float gain = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        fast.set (rate, 0.009f);
        slow.set (rate, 0.105f);
        band.setBandPass (rate, 870, 2.5f);
        lowCut.setHighPass (rate, 600, 0.7f);
    }

    void trigger (float velocity, float accent, const Settings&) override
    {
        gain = velocity * (1 + 0.3f * accent);
        fast.trigger (0.88f);
        slow.trigger (0.12f);
    }

    float render (const Settings&, float) override
    {
        const auto x = 0.3f * a.next (metalHz[4], sampleRate, 0.45) + 0.6f * b.next (metalHz[5], sampleRate, 0.45);
        return band.process (lowCut.process (x)) * (fast.next() + slow.next()) * gain * 2.5f;
    }

    bool isActive() const override      { return slow.isActive(); }
    void stop() override                { fast.stop(); slow.stop(); }
};

/** The six oscillators high-passed, then split: a body band that rings long (a quick fall, then
    the decay knob's long tail) and a sizzle above it that dies sooner; the tone balances them. */
struct Cymbal : Synth::Voice
{
    dsp::Biquad lowCut, body, sizzle1, sizzle2;
    dsp::Decay fast, slow, bright;
    float gain = 0, bodyGain = 1, sizzleGain = 1;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        lowCut.setHighPass (rate, cymbalLowCut, 0.7f);
        body.setBandPass (rate, cymbalBodyHz, 1.4f);
        sizzle1.setHighPass (rate, cymbalSizzleHz, 0.7f);
        sizzle2.setHighPass (rate, cymbalSizzleHz, 0.7f);
        fast.set (rate, cymbalFastTau);
    }

    void trigger (float velocity, float accent, const Settings& s) override
    {
        gain = velocity * (1 + 0.3f * accent);
        const auto tau = dsp::lookupLog (cymbalDecay, s.cyDecay);
        slow.set (sampleRate, tau);
        bright.set (sampleRate, tau * cymbalSizzleRatio);
        fast.trigger (1 - cymbalTail);
        slow.trigger (cymbalTail);
        bright.trigger (1);
        bodyGain = dsp::lookup (cymbalBodyGain, s.cyTone);
        sizzleGain = dsp::lookup (cymbalSizzleGain, s.cyTone);
    }

    float render (const Settings&, float metal) override
    {
        const auto x = lowCut.process (metal);
        const auto y = body.process (x) * (fast.next() + slow.next()) * bodyGain + sizzle2.process (sizzle1.process (x)) * bright.next() * sizzleGain;
        return y * gain * 3;
    }

    bool isActive() const override      { return slow.isActive() || bright.isActive(); }
    void stop() override                { fast.stop(); slow.stop(); bright.stop(); }
};

/** The metal steeply high-passed, through a band-pass and a gate: the hats. The closed hat's
    gate shuts sooner and it is brighter. */
struct Hat : Synth::Voice
{
    explicit Hat (bool o) : open (o) {}

    const bool open;
    dsp::Biquad lowCut, lowCut2, band, top;
    dsp::Ramp gate;
    float gain = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        const auto cut = open ? openHatLowCut : closedHatLowCut;
        lowCut.setHighPass (rate, cut, open ? 0.54f : 0.7f);
        lowCut2.setHighPass (rate, cut, 1.31f);
        band.setBandPass (rate, open ? openHatBandHz : closedHatBandHz, 0.9f);
        top.setHighPass (rate, closedHatTopHz, 0.7f);
    }

    void trigger (float velocity, float accent, const Settings& s) override
    {
        gain = velocity * (1 + 0.3f * accent);

        if (open)
            gate.set (sampleRate, dsp::lookup (openHatLength, s.ohDecay), 1.0f);
        else
            gate.set (sampleRate, 0.125f, 6);

        gate.trigger();
    }

    float render (const Settings&, float metal) override
    {
        auto y = band.process (open ? lowCut2.process (lowCut.process (metal)) : lowCut.process (metal));

        if (! open)
            y = y * 0.4f + top.process (y);

        return y * gate.next() * gain * 4;
    }

    bool isActive() const override      { return gate.isActive(); }
    void stop() override                { gate.stop(); }
};

} // namespace

//==============================================================================
Synth::Synth()
{
    voices.push_back (std::make_unique<Kick>());
    voices.push_back (std::make_unique<Snare>());

    for (int k = 0; k < 6; ++k)
        voices.push_back (std::make_unique<Drum> (k));

    voices.push_back (std::make_unique<RimShot>());
    voices.push_back (std::make_unique<Claves>());
    voices.push_back (std::make_unique<HandClap>());
    voices.push_back (std::make_unique<Maracas>());
    voices.push_back (std::make_unique<Cowbell>());
    voices.push_back (std::make_unique<Cymbal>());
    voices.push_back (std::make_unique<Hat> (true));
    voices.push_back (std::make_unique<Hat> (false));
    jassert (voices.size() == numSounds);
}

Synth::~Synth() = default;

void Synth::prepare (double rate)
{
    sampleRate = rate;

    for (auto& v : voices)
        v->prepare (rate);

    gain.reset (rate, 0.02);
    fresh = true;
}

int Synth::soundForNote (int note)
{
    switch (note)
    {
        case 35: case 36:               return kick;
        case 37:                        return rimShot;
        case 38: case 40:               return snare;
        case 39:                        return handClap;
        case 41: case 43:               return lowTom;
        case 45: case 47:               return midTom;
        case 48: case 50:               return highTom;
        case 42: case 44:               return closedHat;
        case 46:                        return openHat;
        case 49: case 51: case 57: case 59: return cymbal;
        case 56:                        return cowbell;
        case 62:                        return highConga;
        case 63:                        return midConga;
        case 64:                        return lowConga;
        case 70:                        return maracas;
        case 75:                        return claves;
        default:                        return -1;
    }
}

int Synth::rowForSound (int sound)
{
    switch (sound)
    {
        case kick:                      return bd;
        case snare:                     return sd;
        case lowTom: case lowConga:     return lt;
        case midTom: case midConga:     return mt;
        case highTom: case highConga:   return ht;
        case rimShot: case claves:      return rs;
        case handClap: case maracas:    return cp;
        case cowbell:                   return cb;
        case cymbal:                    return cy;
        case openHat:                   return oh;
        case closedHat:                 return ch;
        default:                        return -1;
    }
}

int Synth::soundForRow (int row, const Settings& s)
{
    const bool alt = juce::isPositiveAndBelow (row, numVoices) && s.alternate[static_cast<size_t> (row)];

    switch (row)
    {
        case bd:    return kick;
        case sd:    return snare;
        case lt:    return alt ? lowConga : lowTom;
        case mt:    return alt ? midConga : midTom;
        case ht:    return alt ? highConga : highTom;
        case rs:    return alt ? claves : rimShot;
        case cp:    return alt ? maracas : handClap;
        case cb:    return cowbell;
        case cy:    return cymbal;
        case oh:    return openHat;
        case ch:    return closedHat;
        default:    return -1;
    }
}

int Synth::noteForSound (int sound)
{
    static constexpr std::array<int, numSounds> notes { 36, 38, 41, 45, 48, 64, 63, 62, 37, 75, 39, 70, 56, 49, 46, 42 };
    return juce::isPositiveAndBelow (sound, numSounds) ? notes[static_cast<size_t> (sound)] : -1;
}

void Synth::strike (int sound, float velocity, const Settings& s)
{
    // Velocity sets the level; from 110 it is an accent, as loud as the Accent knob says.
    const bool accented = velocity >= 110;
    const auto accent = accented ? s.accent : 0.0f;
    const auto level = velocity / 127.0f * (1 + 0.9f * accent);

    if (sound == closedHat)
        voices[openHat]->choke();

    voices[static_cast<size_t> (sound)]->strike (level, accent, s);
}

void Synth::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Settings& s)
{
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    auto events = midi.cbegin();
    std::array<float, numSounds> leftGain {}, rightGain {};

    for (int sound = 0; sound < numSounds; ++sound)
    {
        const auto row = static_cast<size_t> (rowForSound (sound));
        const auto level = 1.6f * s.level[row] * s.level[row] * juce::Decibels::decibelsToGain (soundTrim[static_cast<size_t> (sound)]);
        const auto [l, r] = gkp::dsp::panGains (s.pan[row]);
        leftGain[static_cast<size_t> (sound)] = level * l * 0.5f;
        rightGain[static_cast<size_t> (sound)] = level * r * 0.5f;
    }

    // The volume glides when it is turned, but starts where it is set.
    if (fresh)
        gain.setCurrentAndTargetValue (s.gain);

    fresh = false;
    gain.setTargetValue (s.gain);
    const auto driveGain = 1 + s.drive * 6;
    const auto driveMakeup = 1 / std::tanh (driveGain);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        for (; events != midi.cend() && (*events).samplePosition <= i; ++events)
        {
            const auto m = (*events).getMessage();

            if (m.isNoteOn())
            {
                const auto sound = soundForNote (m.getNoteNumber());

                if (sound >= 0)
                    strike (sound, m.getVelocity(), s);
            }
        }

        // The six oscillators run all the time, as they did in the machine.
        float metal = 0;

        for (size_t k = 0; k < metalOscillators.size(); ++k)
            metal += metalOscillators[k].next (metalHz[k], sampleRate, 0.45);

        metal *= 1.0f / 6;
        float l = 0, r = 0;

        for (size_t v = 0; v < voices.size(); ++v)
        {
            const auto y = voices[v]->next (s, metal);
            l += y * leftGain[v];
            r += y * rightGain[v];
        }

        if (s.drive > 0.001f)
        {
            l = std::tanh (l * driveGain) * driveMakeup;
            r = std::tanh (r * driveGain) * driveMakeup;
        }

        const auto g = gain.getNextValue();
        left[i] = rhythm::dsp::ceiling (l * g);
        right[i] = rhythm::dsp::ceiling (r * g);
    }
}

} // namespace groove8
