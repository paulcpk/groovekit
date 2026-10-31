#include "Kick.h"

namespace kessel
{

namespace
{
    constexpr double fadeSeconds = 0.004;     // a kick cut off by the next one fades this long
    constexpr int oversamplingStages = 2;     // the drive runs at four times the rate

    float decibels (float db)   { return juce::Decibels::decibelsToGain (db); }
}

float Settings::dive (double t) const
{
    // exp (-(t / time) ^ power): a low curve falls at once (a sharp punch), a high one holds the
    // pitch up a moment before it drops (the pitched punch of hardstyle kicks).
    const auto power = 0.6 * std::pow (10.0 / 3.0, static_cast<double> (pitchCurve));
    const auto tau = std::max (0.001, static_cast<double> (pitchTime) / 1000.0);
    return static_cast<float> (pitchStart * std::exp (-std::pow (t / tau, power)));
}

//==============================================================================
void Peak::set (double sampleRate, float hz, float gainDb, float q)
{
    const auto a = std::pow (10.0, gainDb / 40.0);
    const auto w = juce::MathConstants<double>::twoPi * juce::jlimit (20.0, sampleRate * 0.45, static_cast<double> (hz)) / sampleRate;
    const auto alpha = std::sin (w) / (2 * q);
    const auto a0 = 1 + alpha / a;
    b0 = static_cast<float> ((1 + alpha * a) / a0);
    b1 = static_cast<float> (-2 * std::cos (w) / a0);
    b2 = static_cast<float> ((1 - alpha * a) / a0);
    a1 = b1;
    a2 = static_cast<float> ((1 - alpha / a) / a0);
}

float Peak::process (float x)
{
    const auto y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

//==============================================================================
void Kick::prepare (double sampleRate, int maxBlockSize)
{
    rate = sampleRate;
    maxBlock = std::max (32, maxBlockSize);
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (1, oversamplingStages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampling->initProcessing (static_cast<size_t> (maxBlock));
    latency = oversampling->getLatencyInSamples();
    pre.assign (static_cast<size_t> (maxBlock), 0);
    clean.assign (static_cast<size_t> (maxBlock), 0);
    clicks.assign (static_cast<size_t> (maxBlock), 0);

    // Delay lengths with no common factors, so the rumble's echoes don't line up into a pitch.
    const double milliseconds[lines] { 29.7, 37.1, 41.3, 53.9 };

    for (size_t i = 0; i < lines; ++i)
    {
        lengths[i] = static_cast<int> (milliseconds[i] / 1000.0 * rate);
        delays[i].assign (static_cast<size_t> (lengths[i]) + 1, 0);
    }

    dc.set (rate, 5);
    reset();
}

void Kick::reset()
{
    for (auto& v : voices)
        v = {};

    crunch.reset();

    if (oversampling != nullptr)
        oversampling->reset();

    toneFilter.reset();
    toneFilter2.reset();
    dc.reset();
    rumbleIn.reset();
    rumbleLow1.reset();
    rumbleLow2.reset();
    rumblePost.reset();
    rumbleHigh.reset();
    lowCut.reset();

    for (size_t i = 0; i < lines; ++i)
    {
        std::fill (delays[i].begin(), delays[i].end(), 0.0f);
        positions[i] = 0;
        damping[i] = 0;
    }

    sinceTrigger = 1e9;
    duck = 1;
    envelope = 0;
    gainReduction = 0;
    crushHold = 0;
    crushValue = 0;
}

void Kick::trigger (int key, float velocity)
{
    // The kick playing fades out under the new one rather than stopping with a click.
    auto& last = voices[static_cast<size_t> (current)];

    if (last.active)
        last.fadeStep = static_cast<float> (1.0 / (fadeSeconds * rate));

    current = 1 - current;
    auto& v = voices[static_cast<size_t> (current)];
    v = {};
    v.active = true;
    v.velocity = juce::jlimit (0.0f, 1.0f, velocity);
    v.key = key;
    sinceTrigger = 0;

    // The same noise every kick, as a drum machine's would be: no kick sounds different from the last.
    noise.setSeed (0x4b657373);
}

void Kick::renderVoice (Voice& v, const Settings& s, float& body, float& sub, float& click)
{
    const auto t = v.t;
    const auto tail = s.tailNote (v.key);
    const auto dive = s.dive (t), glide = s.glide * s.along (t);
    const auto hz = gkp::dsp::noteHz (tail + dive + glide);
    const auto increment = hz / rate;

    // The level: a millisecond's rise, flat for the hold, then down 60 dB over the decay.
    const auto holdSeconds = s.hold / 1000.0, decaySeconds = std::max (0.005, s.decay / 1000.0);
    double amp = std::min (1.0, t / 0.0007);

    if (t > holdSeconds)
        amp *= std::exp (-6.9 * (t - holdSeconds) / decaySeconds);

    const auto velocityGain = 1.0f - s.velocity * (1.0f - v.velocity);
    const auto level = static_cast<float> (amp) * velocityGain * v.fade;

    static constexpr int shapes[] { 0, 1, 5, 3 };    // sine, triangle, soft saw, square in gkp::dsp::waveform
    body = gkp::dsp::waveform (shapes[juce::jlimit (0, 3, s.bodyShape)], v.phase, increment) * level;
    v.phase += increment;

    // The sub: a clean sine on the tail's note, coming in as the body arrives there.
    v.subPhase += gkp::dsp::noteHz (tail + glide) / rate;
    const auto arrived = 1.0f - std::min (1.0f, std::abs (dive) / 12.0f);
    sub = static_cast<float> (std::sin (gkp::dsp::twoPi * gkp::dsp::wrap (v.subPhase))) * level * arrived;

    // The click, over in a few times its decay.
    click = 0;
    const auto clickSeconds = std::max (0.001, s.clickDecay / 1000.0);

    if (t < clickSeconds * 8 && s.clickLevel > 0.0001f)
    {
        const auto fall = static_cast<float> (std::exp (-t / clickSeconds));
        const auto white = noise.nextFloat() * 2 - 1;

        switch (s.clickType)
        {
            case clickNoise:
                v.clickFilter.set (rate, s.clickTone, 0.35f);
                click = v.clickFilter.process (white, 2) * 2.2f * fall;
                break;

            case clickSnap:
                v.clickFilter.set (rate, s.clickTone, 0.2f);
                click = v.clickFilter.process (white, 1) * fall * fall;
                break;

            case clickZap:
            {
                const auto zapHz = 70.0 + s.clickTone * 3.0 * std::exp (-t / (clickSeconds * 0.7));
                v.clickPhase += zapHz / rate;
                click = static_cast<float> (std::sin (gkp::dsp::twoPi * gkp::dsp::wrap (v.clickPhase))) * fall;
                break;
            }

            default:     // a tok: a short pitched knock
                v.clickPhase += s.clickTone * 0.35 / rate;
                click = static_cast<float> (std::sin (gkp::dsp::twoPi * gkp::dsp::wrap (v.clickPhase))) * fall * 1.2f;
                break;
        }

        click *= s.clickLevel * velocityGain * v.fade;
    }

    v.t += 1.0 / rate;

    if (v.fadeStep > 0)
        v.fade = std::max (0.0f, v.fade - v.fadeStep);

    if (v.fade <= 0 || (t > holdSeconds + decaySeconds * 1.4 && t > clickSeconds * 8))
        v.active = false;
}

float Kick::shape (float x, int type, float driveDb)
{
    switch (type)
    {
        case driveHard:
            return juce::jlimit (-0.9f, 0.9f, x * 1.1f);

        case driveFold:
        {
            // A triangle fold: the signal reflects back down each time it passes full scale.
            const auto u = (x + 1) / 4;
            const auto folded = 1 - 4 * std::abs (u - std::floor (u) - 0.5f);
            return std::tanh (1.3f * folded) * 0.95f;
        }

        case driveFuzz:
            // Lopsided: pushes harder up than down, which brings even harmonics (a rectified buzz).
            return x > 0 ? std::tanh (1.8f * x) : std::tanh (0.55f * x) * 1.2f;

        case driveCrush:
        {
            // Held a while and stepped down to a few bits, harsher the more it's driven.
            const auto holdFor = 1 + static_cast<int> (driveDb / 8);
            const auto bits = std::max (2.0f, 9 - driveDb / 7);
            const auto steps = std::pow (2.0f, bits - 1);

            if (--crushHold <= 0)
            {
                crushHold = holdFor;
                crushValue = std::round (std::tanh (x) * steps) / steps;
            }

            return crushValue;
        }

        default:
            return std::tanh (x);
    }
}

void Kick::setRumble (const Settings& s)
{
    // The feedback for a decay of rumbleSize seconds (-60 dB), and damping that keeps it dark.
    for (size_t i = 0; i < lines; ++i)
        feedback[i] = static_cast<float> (std::pow (10.0, -3.0 * lengths[i] / (rate * std::max (0.1f, s.rumbleSize))));

    dampingCoefficient = static_cast<float> (1 - std::exp (-gkp::dsp::twoPi * (200.0 + 3.0 * s.rumbleTone) / rate));
    rumbleIn.set (rate, 900, 0);
    rumbleLow1.set (rate, s.rumbleTone, 0.25f);
    rumbleLow2.set (rate, s.rumbleTone, 0.25f);
    rumblePost.set (rate, s.rumbleTone * 3, 0);
    rumbleHigh.set (rate, 30, 0.3f);
}

float Kick::renderRumble (float in, const Settings& s)
{
    const auto x = rumbleIn.process (in, 0);
    std::array<float, lines> out {};

    for (size_t i = 0; i < lines; ++i)
    {
        auto& line = delays[i];
        out[i] = line[static_cast<size_t> (positions[i])];
    }

    // Householder-like mixing (a 4x4 Hadamard, halved), so every line feeds every other.
    const std::array<float, lines> mixed { 0.5f * (out[0] + out[1] + out[2] + out[3]), 0.5f * (out[0] - out[1] + out[2] - out[3]),
                                           0.5f * (out[0] + out[1] - out[2] - out[3]), 0.5f * (out[0] - out[1] - out[2] + out[3]) };

    for (size_t i = 0; i < lines; ++i)
    {
        damping[i] += dampingCoefficient * (mixed[i] - damping[i]);
        delays[i][static_cast<size_t> (positions[i])] = x + damping[i] * feedback[i];
        positions[i] = (positions[i] + 1) % static_cast<int> (delays[i].size());
    }

    auto wet = 0.5f * (out[0] + out[1] + out[2] + out[3]);

    // Low-passed steeply, driven, low-passed again to smooth what the drive added, and the very
    // lowest rumble taken away so it doesn't eat the sub.
    wet = rumbleLow2.process (rumbleLow1.process (wet, 0), 0);
    const auto push = 1 + s.rumbleDrive * 30;
    wet = std::tanh (wet * push * 3) / (0.6f + 0.4f * std::sqrt (push));
    wet = rumbleHigh.process (rumblePost.process (wet, 0), 1);

    // Ducked under the kick: down at once, back up as the kick's body fades.
    const auto holdSeconds = (s.hold + s.decay * 0.25) / 1000.0;
    const auto release = std::max (0.02, s.decay * 0.6 / 1000.0);
    const auto floor = 1.0 - s.rumbleDuck;
    const auto target = sinceTrigger < holdSeconds ? floor : 1.0 - (1.0 - floor) * std::exp (-(sinceTrigger - holdSeconds) / release);
    duck += static_cast<float> ((target - duck) * (1 - std::exp (-1.0 / (0.003 * rate))));

    return wet * duck * s.rumble * s.rumble * 1.7f;
}

void Kick::render (float* out, int numSamples, const Settings& s, float* rumbleOut)
{
    for (int done = 0; done < numSamples;)
    {
        const auto n = std::min (maxBlock, numSamples - done);
        crunch.set (rate, s.crunchFreq, s.crunch, 1.1f);
        const auto driveGain = decibels (s.drive);

        // 1. Each sample's body (with the crunch and the drive's gain), sub and click.
        for (int i = 0; i < n; ++i)
        {
            float body = 0, sub = 0, click = 0;

            for (auto& v : voices)
            {
                if (! v.active)
                    continue;

                float b, su, c;
                renderVoice (v, s, b, su, c);
                body += b, sub += su, click += c;
            }

            pre[static_cast<size_t> (i)] = crunch.process (body) * driveGain;
            clean[static_cast<size_t> (i)] = sub;
            clicks[static_cast<size_t> (i)] = click;
        }

        // 2. The drive, at four times the rate so its harmonics don't fold back down.
        float* channels[] { pre.data() };
        juce::dsp::AudioBlock<float> block (channels, 1, static_cast<size_t> (n));
        auto up = oversampling->processSamplesUp (block);
        auto* x = up.getChannelPointer (0);

        for (size_t i = 0; i < up.getNumSamples(); ++i)
            x[i] = shape (x[i], s.driveType, s.drive);

        oversampling->processSamplesDown (block);

        // 3. Tone, the sub and click, the rumble, then the compressor, clipper, low cut and volume.
        toneFilter.set (rate, s.tone, 0.1f);
        toneFilter2.set (rate, s.tone * 1.5f, 0);
        lowCut.set (rate, s.lowCut, 0.3f);
        setRumble (s);
        const auto threshold = -3.0f - 27.0f * s.compress;
        const auto ratio = 1.0f + 9.0f * s.compress;
        const auto makeup = -threshold * (1 - 1 / ratio) * 0.55f;
        const auto attack = static_cast<float> (1 - std::exp (-1.0 / (0.004 * rate)));
        const auto releaseCoefficient = static_cast<float> (1 - std::exp (-1.0 / (0.12 * rate)));
        const auto clipDrive = decibels (s.clip * 15);
        const auto knee = 4.0f + s.clip * 12;
        const auto volume = decibels (s.gain);
        float reduction = 0;

        for (int i = 0; i < n; ++i)
        {
            const auto index = static_cast<size_t> (i);
            auto driven = toneFilter2.process (toneFilter.process (pre[index], 0), 0);
            driven = driven - dc.lowpass (driven);
            const auto kick = driven + clean[index] * s.sub * 1.4f;
            const auto rumble = s.rumble > 0.0001f || duck < 0.999f ? renderRumble (kick, s) : 0.0f;
            auto mixed = lowCut.process (kick + clicks[index] + rumble, 1);

            if (rumbleOut != nullptr)
                rumbleOut[done + i] = rumble;

            sinceTrigger += 1.0 / rate;

            // Compression: peak level in, a fast attack that still lets the click through.
            const auto level = std::abs (mixed);
            envelope += (level > envelope ? attack : releaseCoefficient) * (level - envelope);
            const auto over = juce::Decibels::gainToDecibels (envelope, -100.0f) - threshold;
            const auto cut = over > 0 ? over * (1 - 1 / ratio) : 0.0f;
            reduction = std::max (reduction, cut);
            mixed *= decibels (makeup - cut);

            // Clipping: driven into a curve that's soft at none and close to a hard clip at full.
            const auto c = mixed * clipDrive;
            mixed = 0.98f * c / std::pow (1 + std::pow (std::abs (c), knee), 1 / knee);

            out[done + i] = mixed * volume;
        }

        gainReduction = reduction;
        done += n;
    }
}

} // namespace kessel
