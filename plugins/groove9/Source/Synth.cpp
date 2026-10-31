#include "Synth.h"
#include "BinaryData.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace groove9
{

namespace dsp = rhythm::dsp;

namespace
{
    // What the models were measured against: recordings of a modern model of the original at
    // its default settings. Tables run across a knob, 0 to 10 in quarters.

    // Bass drum: the sweep's time constant, the pitch it starts from and lands on across the tune
    // knob; the body's time constant across the decay knob, after a short hold.
    constexpr std::array<float, 5> kickSweepTau { 0.007f, 0.011f, 0.0155f, 0.024f, 0.034f };
    constexpr std::array<float, 5> kickStartHz { 240, 280, 310, 330, 340 };
    constexpr std::array<float, 5> kickEndHz { 50.5f, 52, 53, 55, 57 };
    constexpr std::array<float, 5> kickDecay { 0.03f, 0.045f, 0.072f, 0.16f, 0.4f };
    constexpr float kickHold = 0.035f, kickDrive = 1.3f;

    // Snare: two resonators for the shell (the tune moves both), noise for the snares.
    // (The recordings carry a little an octave below the shell too.)
    constexpr float snareLowHz = 194, snareHighHz = 389, snareLowTau = 0.026f, snareHighTau = 0.02f, snareNoiseTau = 0.068f;
    constexpr float snareSub = 0.3f, snareSubTau = 0.03f;
    constexpr std::array<float, 5> snareTune { 0.8f, 0.9f, 1, 1.12f, 1.25f };
    constexpr std::array<float, 5> snareTone { 5000, 7500, 10500, 14000, 18000 };
    constexpr std::array<float, 5> snareSnappy { 0, 0.25f, 0.6f, 1.0f, 1.4f };

    // Toms: where each settles across its tune knob, how long it rings at the middle of its
    // decay knob (and the knob's scale), the sweep down from above.
    constexpr std::array<std::array<float, 5>, 3> tomHz { {
        { 65, 70, 76, 83, 90 },
        { 98, 106, 116, 126, 137 },
        { 130, 142, 156, 170, 185 },
    } };
    constexpr std::array<float, 3> tomTau { 0.085f, 0.047f, 0.054f };
    constexpr std::array<float, 5> tomDecayScale { 0.45f, 0.7f, 1, 1.6f, 2.6f };
    constexpr float tomSweep = 0.55f, tomSweepTau = 0.02f, tomNoise = 0.16f;

    // Rim shot: three short resonators driven into a clipper.
    constexpr std::array<float, 3> rimHz { 235, 516, 1000 }, rimTau { 0.009f, 0.005f, 0.0025f }, rimGain { 1, 0.9f, 0.7f };

    // Hats and cymbals: their decays across the knobs; the tune's range in semitones.
    constexpr std::array<float, 5> closedHatDecay { 0.012f, 0.025f, 0.05f, 0.12f, 1.0f };
    constexpr std::array<float, 5> openHatDecay { 0.05f, 0.1f, 0.2f, 0.45f, 2.0f };
    constexpr float cymbalTuneRange = 12;

    // Each voice's level against the others, in dB, so the kit sits together with its levels at
    // the same setting.
    constexpr std::array<float, numVoices> voiceTrim { 5, -1.3f, 1, 1, 1, -0.5f, 2.5f, 0, 0, 1, 0 };

    float shape (float x, float drive)     { return std::tanh (x * drive) / std::tanh (drive); }

    /** A recording decoded from the plugin's binary data. */
    juce::AudioBuffer<float> decode (const char* resource, double& fileRate)
    {
        int size = 0;
        const auto* data = BinaryData::getNamedResource (resource, size);
        juce::AudioBuffer<float> buffer;
        fileRate = 48000;

        if (data == nullptr)
            return buffer;

        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (data, static_cast<size_t> (size), false), true));

        if (reader == nullptr)
            return buffer;

        fileRate = reader->sampleRate;
        buffer.setSize (1, static_cast<int> (reader->lengthInSamples));
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, false);
        return buffer;
    }

//==============================================================================
/** An oscillator swept down fast from a few hundred hertz, shaped towards a sine and driven a
    little flat at the top; a click of noise and pulse in front, as loud as the attack. */
struct Kick : Synth::Voice
{
    dsp::Sine osc;
    dsp::Decay body, sweep, click;
    dsp::Noise noise { 1201 };
    dsp::Biquad clickFilter, clickTop;
    float startHz = 280, endHz = 53, clickLevel = 0;
    int held = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        click.set (rate, 0.0015f);
        clickFilter.setHighPass (rate, 600, 0.7f);
        clickTop.setLowPass (rate, 3500, 0.7f);
    }

    void trigger (float velocity, float accent, const Settings& s) override
    {
        body.set (sampleRate, dsp::lookupLog (kickDecay, s.bdDecay));
        body.trigger (velocity);
        sweep.set (sampleRate, dsp::lookupLog (kickSweepTau, s.bdTune));
        sweep.trigger (1);
        startHz = dsp::lookup (kickStartHz, s.bdTune);
        endHz = dsp::lookup (kickEndHz, s.bdTune);
        click.trigger (1);
        clickLevel = velocity * (0.15f + 1.2f * s.bdAttack) * (1 + 0.5f * accent);
        held = static_cast<int> (kickHold * sampleRate);
        osc.reset();
    }

    float render (const Settings&, float) override
    {
        const auto hz = endHz + (startHz - endHz) * sweep.next();
        float level = body.value;

        if (held > 0)
            --held;
        else
            level = body.next();

        const auto c = click.next();
        const auto attack = clickTop.process (clickFilter.process (noise.next() * 0.6f + (c > 0.5f ? 1.0f : 0.0f))) * c * clickLevel;
        return shape (osc.next (hz, sampleRate), kickDrive) * level + attack;
    }

    bool isActive() const override      { return body.isActive(); }
    void stop() override                { body.stop(); }
};

/** Two resonators for the shell and noise through the tone's low-pass for the snares. */
struct Snare : Synth::Voice
{
    dsp::Struck low, high, sub;
    dsp::Decay lowEnv, highEnv, subEnv, noiseEnv, drop;
    dsp::Noise noise { 1983 };
    dsp::Biquad noiseHigh, noiseLow;
    gkp::dsp::OnePole round;
    float ratio = 1, shellGain = 0, noiseGain = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        lowEnv.set (rate, snareLowTau);
        highEnv.set (rate, snareHighTau);
        subEnv.set (rate, snareSubTau);
        noiseEnv.set (rate, snareNoiseTau);
        drop.set (rate, 0.01f);
        noiseHigh.setHighPass (rate, 900, 0.7f);
        round.set (rate, 1800);
    }

    void trigger (float velocity, float accent, const Settings& s) override
    {
        ratio = dsp::lookup (snareTune, s.sdTune);
        shellGain = velocity * (1 + 0.2f * accent);
        noiseGain = velocity * dsp::lookup (snareSnappy, s.sdSnappy) * (1 + 0.5f * accent);
        noiseLow.setLowPass (sampleRate, dsp::lookupLog (snareTone, s.sdTone), 0.7f);
        lowEnv.trigger (1);
        highEnv.trigger (0.6f);
        subEnv.trigger (snareSub);
        sub.reset();
        noiseEnv.trigger (1);
        drop.trigger (0.25f);
        low.reset();
        high.reset();
    }

    float render (const Settings&, float) override
    {
        const auto bend = ratio * (1 + drop.next());
        const auto shell = low.next (snareLowHz * bend, sampleRate, snareLowTau) * lowEnv.next() + high.next (snareHighHz * bend, sampleRate, snareHighTau) * highEnv.next()
                         + sub.next (snareLowHz * 0.5f * bend, sampleRate, snareSubTau) * subEnv.next();
        const auto snares = noiseLow.process (noiseHigh.process (noise.next())) * noiseEnv.next() * noiseGain;
        return round.lowpass (shell) * shellGain + snares;
    }

    bool isActive() const override      { return lowEnv.isActive() || noiseEnv.isActive(); }
    void stop() override                { lowEnv.stop(); highEnv.stop(); noiseEnv.stop(); }
};

/** A tom: an oscillator swept down onto its pitch and driven a little square, with noise. */
struct Tom : Synth::Voice
{
    explicit Tom (int k) : kind (k) {}

    const int kind;
    dsp::Sine osc;
    dsp::Decay env, sweep, noiseEnv;
    dsp::Noise noise { 4100u + static_cast<uint32_t> (kind) };
    dsp::Biquad noiseFilter;
    float hz = 100, gain = 0;

    void trigger (float velocity, float accent, const Settings& s) override
    {
        const auto k = static_cast<size_t> (kind);
        hz = dsp::lookup (tomHz[k], s.tomTune[k]);
        gain = velocity * (1 + 0.2f * accent);
        env.set (sampleRate, tomTau[k] * dsp::lookupLog (tomDecayScale, s.tomDecay[k]));
        env.trigger (1);
        sweep.set (sampleRate, tomSweepTau);
        sweep.trigger (tomSweep);
        noiseEnv.set (sampleRate, tomTau[k] * 0.5f);
        noiseEnv.trigger (tomNoise);
        noiseFilter.setLowPass (sampleRate, 3500, 0.7f);
        osc.reset();
    }

    float render (const Settings&, float) override
    {
        const auto tone = shape (osc.next (hz * (1 + sweep.next()), sampleRate), 2.0f) * env.next();
        return (tone + noiseFilter.process (noise.next()) * noiseEnv.next()) * gain;
    }

    bool isActive() const override      { return env.isActive(); }
    void stop() override                { env.stop(); noiseEnv.stop(); }
};

/** Three short resonators driven into a clipper, high-passed. */
struct RimShot : Synth::Voice
{
    std::array<dsp::Struck, 3> rings;
    std::array<dsp::Decay, 3> envs;
    dsp::Biquad highPass, lowPass;
    float gain = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);

        for (size_t i = 0; i < 3; ++i)
            envs[i].set (rate, rimTau[i]);

        highPass.setHighPass (rate, 150, 0.7f);
        lowPass.setLowPass (rate, 8000, 0.7f);
    }

    void trigger (float velocity, float accent, const Settings&) override
    {
        gain = velocity * (1 + 0.3f * accent);

        for (size_t i = 0; i < 3; ++i)
        {
            envs[i].trigger (rimGain[i]);
            rings[i].reset();
        }
    }

    float render (const Settings&, float) override
    {
        float y = 0;

        for (size_t i = 0; i < 3; ++i)
            y += rings[i].next (rimHz[i], sampleRate, rimTau[i]) * envs[i].next();

        return lowPass.process (highPass.process (shape (y, 2.0f))) * gain;
    }

    bool isActive() const override      { return envs[0].isActive(); }
    void stop() override                { for (auto& e : envs) e.stop(); }
};

/** Band-passed noise through three quick bursts, the last running into a short tail. */
struct HandClap : Synth::Voice
{
    dsp::Noise noise { 3939 };
    dsp::Biquad band, lowCut, lowCut2;
    dsp::Decay tail, tailLong;
    double t = 1;
    float gain = 0;

    void prepare (double rate) override
    {
        Voice::prepare (rate);
        band.setBandPass (rate, 1250, 0.9f);
        lowCut.setHighPass (rate, 650, 0.54f);
        lowCut2.setHighPass (rate, 650, 1.31f);
        tail.set (rate, 0.015f);
        tailLong.set (rate, 0.04f);
    }

    void trigger (float velocity, float accent, const Settings&) override
    {
        gain = velocity * (1 + 0.3f * accent);
        t = 0;
        tail.trigger (0.4f);
        tailLong.trigger (0.12f);
    }

    float render (const Settings&, float) override
    {
        constexpr double spacing = 0.0103, burstTau = 0.0028, tailStart = 0.0206;
        float level = 0;

        // The second burst is the loudest, as two hands meet squarest.
        if (t < tailStart)
            level = static_cast<float> (std::exp (-std::fmod (t, spacing) / burstTau)) * (t < spacing ? 0.7f : 1.0f);
        else
            level = tail.next() + tailLong.next();

        t += 1 / sampleRate;
        return band.process (lowCut2.process (lowCut.process (noise.next()))) * level * gain * 2.6f;
    }

    bool isActive() const override      { return t < 0.0206 || tailLong.isActive(); }
    void stop() override                { t = 1; tail.stop(); tailLong.stop(); }
};

/** A recording played at the tune's speed through a decay: the hats and cymbals. */
struct Recording : Synth::Voice
{
    Recording (const char* resource, int r) : row (r)
    {
        sound = decode (resource, fileRate);
    }

    const int row;
    juce::AudioBuffer<float> sound;
    double fileRate = 48000, position = 0, step = 1;
    dsp::Decay env;
    float gain = 0;

    void trigger (float velocity, float accent, const Settings& s) override
    {
        gain = velocity * (1 + 0.3f * accent);
        auto semitones = 0.0f;
        float tau = 10;

        switch (row)
        {
            case ch:    tau = dsp::lookupLog (closedHatDecay, s.chDecay); break;
            case oh:    tau = dsp::lookupLog (openHatDecay, s.ohDecay); break;
            case cr:    semitones = (s.crTune - 0.5f) * cymbalTuneRange; break;
            case rd:    semitones = (s.rdTune - 0.5f) * cymbalTuneRange; break;
            default:    break;
        }

        step = fileRate / sampleRate * std::pow (2.0, semitones / 12.0);
        env.set (sampleRate, tau);
        env.trigger (1);
        position = 0;
    }

    float render (const Settings&, float) override
    {
        const auto n = sound.getNumSamples();
        const auto i = static_cast<int> (position);

        if (i + 1 >= n)
        {
            position = n;
            return 0;
        }

        const auto* data = sound.getReadPointer (0);
        const auto frac = static_cast<float> (position - i);
        position += step;
        return (data[i] + (data[i + 1] - data[i]) * frac) * env.next() * gain;
    }

    bool isActive() const override      { return position < sound.getNumSamples() && env.isActive(); }
    void stop() override                { position = sound.getNumSamples(); }
};

} // namespace

//==============================================================================
Synth::Synth()
{
    voices.push_back (std::make_unique<Kick>());
    voices.push_back (std::make_unique<Snare>());

    for (int k = 0; k < 3; ++k)
        voices.push_back (std::make_unique<Tom> (k));

    voices.push_back (std::make_unique<RimShot>());
    voices.push_back (std::make_unique<HandClap>());
    voices.push_back (std::make_unique<Recording> ("hatclosed_flac", ch));
    voices.push_back (std::make_unique<Recording> ("hatopen_flac", oh));
    voices.push_back (std::make_unique<Recording> ("crash_flac", cr));
    voices.push_back (std::make_unique<Recording> ("ride_flac", rd));
    jassert (voices.size() == numVoices);
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

int Synth::rowForNote (int note)
{
    switch (note)
    {
        case 35: case 36:   return bd;
        case 37:            return rs;
        case 38: case 40:   return sd;
        case 39:            return cp;
        case 41: case 43:   return lt;
        case 45: case 47:   return mt;
        case 48: case 50:   return ht;
        case 42: case 44:   return ch;
        case 46:            return oh;
        case 49: case 57:   return cr;
        case 51: case 59:   return rd;
        default:            return -1;
    }
}

int Synth::noteForRow (int row)
{
    static constexpr std::array<int, numVoices> notes { 36, 38, 41, 45, 48, 37, 39, 42, 46, 49, 51 };
    return juce::isPositiveAndBelow (row, numVoices) ? notes[static_cast<size_t> (row)] : -1;
}

void Synth::strike (int row, float velocity, const Settings& s)
{
    // Velocity sets the level; from 110 it is an accent, as loud as the Total Accent knob says.
    const auto accent = velocity >= 110 ? s.accent : 0.0f;
    const auto level = velocity / 127.0f * (1 + 0.9f * accent);

    if (row == ch)
        voices[oh]->choke();

    voices[static_cast<size_t> (row)]->strike (level, accent, s);
}

void Synth::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Settings& s)
{
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    auto events = midi.cbegin();
    std::array<float, numVoices> leftGain {}, rightGain {};

    for (size_t row = 0; row < numVoices; ++row)
    {
        const auto level = 1.6f * s.level[row] * s.level[row] * juce::Decibels::decibelsToGain (voiceTrim[row]);
        const auto [l, r] = gkp::dsp::panGains (s.pan[row]);
        leftGain[row] = level * l * 0.5f;
        rightGain[row] = level * r * 0.5f;
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
                const auto row = rowForNote (m.getNoteNumber());

                if (row >= 0)
                    strike (row, m.getVelocity(), s);
            }
        }

        float l = 0, r = 0;

        for (size_t v = 0; v < voices.size(); ++v)
        {
            const auto y = voices[v]->next (s, 0);
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

} // namespace groove9
