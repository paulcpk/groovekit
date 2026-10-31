#include "Bassline.h"
#include "Voices.h"

namespace groove3
{

namespace
{
    constexpr int oversamplingStages = 2;       // four times the rate
    constexpr double pi = juce::MathConstants<double>::pi;

    // The ladder (four capacitors, the last half the others, joined by diode pairs) peaks 1.41
    // times above its coefficient, and sings on its own from a feedback of 16.96 (both from its
    // equations, solved for where its phase turns half a circle).
    constexpr double peakRatio = 1.4126, selfOscillation = 16.96;

    // How hard the diodes saturate: the level in the ladder where they start to bend.
    constexpr double diodeScale = 1.1;

    // The level the oscillator goes into the filter at, with the overdrive at its normal setting.
    constexpr float oscillatorLevel = 0.5f;

    // The resonance is fed back through a high-pass this far under the cutoff (it keeps the bass
    // when the resonance is up, without turning the peak aside).
    constexpr double feedbackHighPass = 0.1;

    // The cutoff knob's range (the resonant peak's frequency): 60 Hz up 7.4 octaves to 10 kHz;
    // the envelope's reach at full Env Mod, and how far Env Mod lowers where it starts, in octaves.
    constexpr float lowestCutoff = 60, cutoffOctaves = 7.4f, envelopeOctaves = 6, envModShift = 1.2f;

    // The filter's output brought back up to the oscillator's level.
    constexpr float makeup = 1.6f;

    float coefficient (double seconds, double rate)  { return static_cast<float> (1 - std::exp (-1 / std::max (1.0e-6, seconds * rate))); }
    float multiplier (double seconds, double rate)   { return static_cast<float> (std::exp (-1 / std::max (1.0e-6, seconds * rate))); }

    /** The overdrive knob as a gain into the filter: nothing at 0 (only the resonance left
        singing), the normal level at a quarter, 40 times it at the top. */
    float overdriveGain (float knob)
    {
        return knob < 0.25f ? knob / 0.25f : std::pow (40.0f, (knob - 0.25f) / 0.75f);
    }

    /** The accent sweep's capacitor, charging through a diode and draining through the
        resonance pot: faster or slower by the switch (seconds). */
    std::pair<double, double> sweepTimes (int speed)
    {
        switch (speed)
        {
            case sweepFast: return { 0.006, 0.035 };
            case sweepSlow: return { 0.060, 0.450 };
            default:        return { 0.020, 0.120 };
        }
    }
}

void Bassline::prepare (double sampleRate, int maxBlock)
{
    rate = sampleRate;
    fs = sampleRate * (1 << oversamplingStages);
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (1, oversamplingStages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampling->initProcessing (static_cast<size_t> (std::max (1, maxBlock)));
    latency = oversampling->getLatencyInSamples();
    zeros.assign (static_cast<size_t> (std::max (1, maxBlock)), 0);

    squareDroop.set (fs, 30);
    fmBlock.set (fs, 25);
    mufflerSplit.set (fs, 220);
    distortionEmphasis.set (fs, 650);
    distortionTone.set (fs, 3800);

    for (auto* s : { &cutoffKnob, &resonanceKnob, &envModKnob, &accentKnob, &overdriveKnob, &distortionKnob })
        s->prepare (fs, 12);

    volume.prepare (sampleRate, 20);
    reset();
}

void Bassline::reset()
{
    if (oversampling != nullptr)
        oversampling->reset();

    gate = accented = sliding = false;
    stage = Stage::idle;
    veg = meg = sweepCharge = accentLevel = fmSignal = 0;
    phase = 0;
    cycles = 0;
    v = {};
    slope = {};
    input = feedbackLow = 0;

    for (auto* p : { &squareDroop, &fmBlock, &mufflerSplit, &distortionEmphasis, &distortionTone })
        p->reset();

    dcBlock = {};
}

void Bassline::noteOn (int key, bool accent, bool slide)
{
    target = static_cast<float> (key);
    accented = accent;

    // Held on into: the pitch glides, nothing starts again.
    if (slide && gate)
    {
        sliding = true;
        return;
    }

    pitch = target;
    sliding = false;
    gate = true;
    meg = 1;
    megAccented = accent;
    stage = Stage::attack;
}

void Bassline::noteOff()
{
    gate = false;

    if (stage != Stage::idle)
        stage = Stage::release;
}

float Bassline::filter (float x, double cutoffHz, double feedback)
{
    // The diodes' conductance at each stage, from the voltages across them the sample before:
    // less as they're driven harder (tanh(d) / d), which is where the saturation comes from.
    const auto conductance = [] (double d)
    {
        d *= diodeScale;
        return std::abs (d) < 1.0e-5 ? 1.0 : std::tanh (d) / d;
    };

    const auto g1 = conductance (input - feedback * v[3] - v[0]);
    const auto g2 = conductance (v[0] - v[1]);
    const auto g3 = conductance (v[1] - v[2]);
    const auto g4 = conductance (v[2] - v[3]);

    // Each capacitor takes the current from the stage before less what goes on to the next; the
    // last has half the capacitance. The resonance feeds the last back into the first, less its
    // low end (the high-pass), which is known from the sample before.
    const double m[4][4] {
        { -g1 - g2, g2, 0, -g1 * feedback },
        { g2, -g2 - g3, g3, 0 },
        { 0, g3, -g3 - g4, g4 },
        { 0, 0, 2 * g4, -2 * g4 },
    };

    const auto in = static_cast<double> (x) + feedback * feedbackLow;

    // Trapezoidal integration, solved for this sample's voltages together (no delay in the loop).
    const auto g = std::tan (pi * cutoffHz / fs) / peakRatio;
    double a[4][4], r[4];

    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
            a[i][j] = (i == j ? 1.0 : 0.0) - g * m[i][j];

        r[i] = v[static_cast<size_t> (i)] + g * slope[static_cast<size_t> (i)];
    }

    r[0] += g * g1 * in;

    for (int col = 0; col < 3; ++col)
    {
        for (int row = col + 1; row < 4; ++row)
        {
            const auto f = a[row][col] / a[col][col];

            if (f == 0.0)
                continue;

            for (int j = col; j < 4; ++j)
                a[row][j] -= f * a[col][j];

            r[row] -= f * r[col];
        }
    }

    for (int i = 3; i >= 0; --i)
    {
        auto sum = r[i];

        for (int j = i + 1; j < 4; ++j)
            sum -= a[i][j] * v[static_cast<size_t> (j)];

        v[static_cast<size_t> (i)] = sum / a[i][i];
    }

    for (int i = 0; i < 4; ++i)
    {
        auto s = i == 0 ? g1 * in : 0.0;

        for (int j = 0; j < 4; ++j)
            s += m[i][j] * v[static_cast<size_t> (j)];

        slope[static_cast<size_t> (i)] = s;
    }

    input = in;
    feedbackLow += (1 - std::exp (-2 * pi * std::max (30.0, cutoffHz * feedbackHighPass) / fs)) * (v[3] - feedbackLow);

    // A note's voltages never run away, but a broken number would stay for good: start again.
    if (! std::isfinite (v[3]))
    {
        v = {};
        slope = {};
        input = feedbackLow = 0;
    }

    return static_cast<float> (v[3]);
}

void Bassline::render (float* out, int numSamples, const Settings& s)
{
    const auto maxBlock = static_cast<int> (zeros.size());

    // In pieces no longer than the oversampling was prepared for.
    while (numSamples > maxBlock)
    {
        render (out, maxBlock, s);
        out += maxBlock;
        numSamples -= maxBlock;
    }

    if (numSamples <= 0 || oversampling == nullptr)
        return;

    cutoffKnob.set (s.cutoff);
    resonanceKnob.set (s.resonance);
    envModKnob.set (s.envMod);
    accentKnob.set (s.accent);
    overdriveKnob.set (overdriveGain (s.overdrive));
    distortionKnob.set (s.distortion);
    volume.set (juce::Decibels::decibelsToGain (s.volumeDb));

    const auto slideCoefficient = coefficient (s.slideMs * 0.001 / 3, fs);
    const auto normalDecay = multiplier (s.decayMs * 0.001 / 3, fs);
    const auto accentDecay = multiplier (s.accentDecayMs * 0.001 / 3, fs);
    const auto ampDecay = s.ampDecayMs >= ampDecayHold - 1 ? 1.0f : multiplier (s.ampDecayMs * 0.001 / 6.9, fs);
    const auto release = multiplier (0.003, fs);
    const auto attackStep = static_cast<float> (1 / (std::max (0.0003, (accented ? 0.3 : s.softAttackMs) * 0.001) * fs));
    const auto [chargeTime, drainTime] = sweepTimes (s.sweepSpeed);
    const auto charge = coefficient (chargeTime, fs), drain = coefficient (drainTime, fs);
    const auto accentSmoothing = coefficient (0.0015, fs);
    const auto subDivider = s.subOctave == 0 ? 2 : 4;
    const auto sweepDepth = s.resonanceRange == 0 ? 2.2f : 3.4f;
    // Normal stops short of singing; High goes past it (the feedback's high-pass costs the loop
    // some gain: it sings from about 1.25 times the bare ladder's limit).
    const auto maxFeedback = selfOscillation * (s.resonanceRange == 0 ? 0.95 : 1.4);
    const auto mufflerDrive = s.muffler == mufflerHard ? 7.0f : 2.5f;
    const auto mufflerMakeup = s.muffler == mufflerHard ? 2.6f : 1.5f;
    const auto maxCutoff = std::min (20000.0, fs * 0.2);

    std::array<float*, 1> silent { zeros.data() };
    const juce::dsp::AudioBlock<float> silence (silent.data(), 1, static_cast<size_t> (numSamples));
    auto up = oversampling->processSamplesUp (silence);
    auto* x = up.getChannelPointer (0);

    for (size_t i = 0; i < up.getNumSamples(); ++i)
    {
        // The pitch, gliding on an RC curve when slid to.
        if (sliding)
            pitch += (target - pitch) * slideCoefficient;

        const auto dt = gkp::dsp::noteHz (pitch + s.tuning) / fs;
        phase += dt;

        if (phase >= 1)
        {
            phase -= 1;
            cycles = (cycles + 1) & 3;
        }

        // The saw, its ramp bowed a little; the square shaped from it.
        const auto ramp = 2 * phase - 1;
        const auto saw = ramp + 0.08 * (ramp * ramp - 1.0 / 3) - gkp::dsp::blep (phase, dt);
        auto wave = static_cast<float> (saw);

        if (s.waveform == square)
            wave = squareDroop.highpass (0.9f * std::tanh (12.0f * (static_cast<float> (ramp - gkp::dsp::blep (phase, dt)) + 0.04f)));

        if (s.sub > 0)
        {
            const auto subPhase = ((cycles % subDivider) + phase) / subDivider;
            const auto subDt = dt / subDivider;
            const auto subWave = (subPhase < 0.5 ? 1.0 : -1.0) + gkp::dsp::blep (subPhase, subDt) - gkp::dsp::blep (gkp::dsp::wrap (subPhase + 0.5), subDt);
            wave += s.sub * 0.8f * static_cast<float> (subWave);
        }

        // The envelopes and the accent: into the volume smoothed a little, into the filter
        // through the sweep's capacitor.
        meg *= megAccented ? accentDecay : normalDecay;
        const auto accentAmount = accentKnob.next();
        const auto accentTarget = accented ? accentAmount * meg : 0.0f;
        accentLevel += (accentTarget - accentLevel) * accentSmoothing;

        if (accentTarget > sweepCharge)
            sweepCharge += (accentTarget - sweepCharge) * charge;

        sweepCharge -= sweepCharge * drain;

        switch (stage)
        {
            case Stage::attack:
                veg += attackStep;

                if (veg >= 1)
                {
                    veg = 1;
                    stage = Stage::decay;
                }
                break;
            case Stage::decay:      veg *= ampDecay; break;
            case Stage::release:
                veg *= release;

                if (veg < 1.0e-5f)
                {
                    veg = 0;
                    stage = Stage::idle;
                }
                break;
            case Stage::idle:       break;
        }

        // The cutoff, in octaves: the knob, the envelope (turning Env Mod up lowers where it
        // starts, as on the original), the accent sweep (with the resonance), the keys, and the
        // output itself.
        const auto envMod = envModKnob.next();
        const auto resonance = resonanceKnob.next();
        auto octaves = std::log2 (lowestCutoff) + cutoffKnob.next() * cutoffOctaves - envMod * envModShift + envMod * envelopeOctaves * meg
                     + sweepCharge * resonance * sweepDepth + s.tracking * (pitch - 36) / 12
                     + s.filterFm * s.filterFm * 8.0f * fmSignal;
        const auto cutoffHz = juce::jlimit (12.0, maxCutoff, std::exp2 (static_cast<double> (octaves)));
        // The ladder only rings near where it would sing on its own: the knob's turn crowds there.
        const auto feedback = maxFeedback * (1 - std::pow (1 - static_cast<double> (resonance), 2.2));

        // Overdrive into the filter (with a breath of noise, as in any circuit, so a resonance
        // turned up to sing starts even with nothing else going in), the level made up for after
        // it in part; and the passband the
        // feedback takes away (by 1 + feedback) half given back.
        const auto drive = overdriveKnob.next();
        auto y = filter (wave * oscillatorLevel * drive + 1.0e-5f * noise.next(), cutoffHz, feedback) * (drive > 1 ? std::pow (drive, -0.35f) : 1.0f) * makeup
                 * static_cast<float> (std::sqrt (1 + feedback));

        // The volume, louder with an accent.
        y *= veg * (1 + 1.4f * accentLevel);
        fmSignal = fmBlock.highpass (y);

        if (s.muffler != mufflerOff)
        {
            const auto low = mufflerSplit.lowpass (y);
            y = low + std::tanh ((y - low) * mufflerDrive) / mufflerDrive * mufflerMakeup;
        }

        if (const auto d = distortionKnob.next(); d > 0.001f)
        {
            const auto gain = juce::Decibels::decibelsToGain (d * 36.0f);
            const auto emphasised = y + 2.0f * distortionEmphasis.highpass (y);
            const auto clipped = std::tanh (gain * emphasised + 0.15f) - std::tanh (0.15f);
            y += (distortionTone.lowpass (clipped) * 0.6f - y) * std::min (1.0f, d * 5);
        }

        x[i] = dcBlock.process (y);
        shownCutoff = static_cast<float> (cutoffHz);
    }

    std::array<float*, 1> outputs { out };
    juce::dsp::AudioBlock<float> outBlock (outputs.data(), 1, static_cast<size_t> (numSamples));
    oversampling->processSamplesDown (outBlock);

    for (int i = 0; i < numSamples; ++i)
        out[i] = rhythm::dsp::ceiling (out[i] * volume.next());
}

} // namespace groove3
