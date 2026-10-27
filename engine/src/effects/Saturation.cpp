#include "Saturation.h"

namespace gk::effects
{

namespace
{
    float dbToGain (float db)   { return std::exp (db * 0.11512925f); }   // 10^(db/20)

    std::optional<int64_t> since (const model::Json& params)
    {
        if (auto it = params.find ("since"); it != params.end() && it->is_number())
            return it->get<int64_t>();

        return {};
    }

    enum Model { triode, pentode, tape, transformer, console, transistor };
}

//==============================================================================
// Exciter: model, driveDb, warmth, harmonics, harmonicsHz, body, outputDb
const char* ExciterPlugin::xmlTypeName = "groovekitExciter";

const std::vector<const char*>& ExciterPlugin::modelNames()
{
    static const std::vector<const char*> names { "triode", "pentode", "tape", "transformer", "console", "transistor" };
    return names;
}

float ExciterPlugin::stage (int model, float x, float drive, float warmth)
{
    const auto u = x * drive;
    const auto bias = 0.35f * warmth;   // the asymmetry that makes even harmonics

    switch (model)
    {
        case pentode:
        {
            // A firmer knee: mostly odd harmonics, a little compression.
            auto f = [] (float v) { return v / std::pow (1.0f + std::pow (std::abs (v), 2.5f), 0.4f); };
            return f (u + 0.5f * bias) - f (0.5f * bias);
        }

        case tape:
        {
            // Smooth and nearly symmetric.
            constexpr auto scale = 2.0f / juce::MathConstants<float>::pi;
            return scale * (std::atan (u + 0.3f * bias) - std::atan (0.3f * bias));
        }

        case console:
        {
            // A cubic that flattens out at full scale: the softest.
            auto f = [] (float v) { v = juce::jlimit (-1.5f, 1.5f, v); return v - v * v * v / 6.75f; };
            return f (u + 0.2f * bias) - f (0.2f * bias);
        }

        case transistor:
        {
            // Algebraic: approaches full scale more gradually than tanh. The negative side gives way later.
            auto f = [] (float v) { return v / std::sqrt (1 + (v >= 0 ? 1.0f : 0.5f) * v * v); };
            return f (u + 0.5f * bias) - f (0.5f * bias);
        }

        default:
            // Triode (and the transformer's core): tanh, biased for the second harmonic.
            return std::tanh (u + bias) - std::tanh (bias);
    }
}

float ExciterPlugin::stageGain (int model, float drive, float warmth)
{
    constexpr auto level = 0.316f;
    return std::max (1.0e-3f, (stage (model, level, drive, warmth) - stage (model, -level, drive, warmth)) / (2 * level));
}

ExciterPlugin::ExciterPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::exciter)
{
}

void ExciterPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    maxBlock = std::max (1, info.blockSizeSamples);
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing (static_cast<size_t> (maxBlock));
    inputSquares.assign (static_cast<size_t> (maxBlock), 0.0f);

    const auto rate = info.sampleRate * 2;
    bodyLow.setLowpass (120, 0.7, rate);
    bodyBand.setBandpass (240, 0.8, rate);

    for (auto* f : { &presence, &presenceAfter, &bodyLow, &bodyBand })
        f->reset();

    presenceLevel[0] = presenceLevel[1] = 0;
    levelAttack = GainSmoother::coefficient (0.5f, rate);
    levelRelease = GainSmoother::coefficient (30.0f, rate);

    for (int ch = 0; ch < 2; ++ch)
    {
        transformerLow[ch].setCutoff (180, rate);
        transformerLow[ch].state = 0;
        tapeLoss[ch].state = 0;
        dcBlock[ch].setCutoff (8, info.sampleRate);
        dcBlock[ch].state = 0;
    }

    lastHarmonicsHz = -1;
    hop = MeterHistory::hopSamples (info.sampleRate);
    hopCount = 0;
    hopIn = hopOut = 0;
}

double ExciterPlugin::getLatencySeconds()
{
    return oversampling != nullptr && sampleRate > 0 ? oversampling->getLatencyInSamples() / sampleRate : 0.0;
}

void ExciterPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || oversampling == nullptr || fc.destBuffer->getNumChannels() < 2)
        return;

    const auto model = choice (0);
    const auto drive = dbToGain (value (1));
    const auto warmth = value (2) / 100.0f;
    const auto harmonics = value (3) / 100.0f;
    const auto body = value (5) / 100.0f;
    const auto output = dbToGain (value (6));
    const auto rate = sampleRate * 2;

    if (const auto hz = value (4); hz != lastHarmonicsHz)
    {
        presence.setHighpass (hz, 0.7, rate);
        presenceAfter.setHighpass (hz, 0.7, rate);
        lastHarmonicsHz = hz;
    }

    // Tape loses a little top end as it's driven.
    for (auto& t : tapeLoss)
        t.setCutoff (20000.0f - 250.0f * value (1), rate);

    // Level-matched, so turning up the drive adds character rather than loudness.
    const auto core = model == transformer ? triode : model;
    const auto lowDrive = model == transformer ? drive * 1.8f : drive;
    const auto match = 1.0f / stageGain (core, lowDrive, warmth);
    const auto highDrive = drive * 0.6f;
    const auto highMatch = 1.0f / stageGain (console, highDrive, warmth);

    for (int start = 0; start < fc.bufferNumSamples; start += maxBlock)
    {
        const auto n = std::min (maxBlock, fc.bufferNumSamples - start);
        float* channels[] { fc.destBuffer->getWritePointer (0, fc.bufferStartSample + start),
                            fc.destBuffer->getWritePointer (1, fc.bufferStartSample + start) };

        for (int i = 0; i < n; ++i)
            inputSquares[static_cast<size_t> (i)] = 0.5f * (channels[0][i] * channels[0][i] + channels[1][i] * channels[1][i]);

        juce::dsp::AudioBlock<float> block (channels, 2, static_cast<size_t> (n));
        auto up = oversampling->processSamplesUp (block);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = up.getChannelPointer (static_cast<size_t> (ch));

            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const auto x = d[i];
                float main;

                if (model == transformer)
                {
                    // The iron saturates the lows; the highs pass nearly clean.
                    const auto low = transformerLow[ch].lowpass (x);
                    main = stage (core, low, lowDrive, warmth) * match + stage (console, x - low, highDrive, warmth) * highMatch;
                }
                else
                {
                    main = stage (core, x, drive, warmth) * match;

                    if (model == tape)
                        main = tapeLoss[ch].lowpass (main);
                }

                // Presence: the highs' own second and third harmonics (Chebyshev polynomials of the
                // highs scaled to their level, so the amount doesn't depend on how loud they are),
                // warmth tipping it towards the second, and a little of the highs themselves.
                const auto p = presence.process (x, ch);
                auto& level = presenceLevel[ch];
                const auto target = std::abs (p);
                level = target + (target > level ? levelAttack : levelRelease) * (level - target);
                const auto n = juce::jlimit (-1.0f, 1.0f, p / std::max (level, 1.0e-5f));
                const auto overtones = warmth * (2 * n * n - 1) + (1 - warmth) * (4 * n * n * n - 3 * n);
                const auto excited = presenceAfter.process (0.6f * level * overtones + 0.3f * p, ch);

                // Body: the lows rectified, an octave up, band-passed.
                const auto low = bodyLow.process (x, ch);
                const auto weight = bodyBand.process (std::abs (low), ch);

                d[i] = main + 1.2f * harmonics * excited + 1.5f * body * weight;
            }
        }

        oversampling->processSamplesDown (block);

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
                channels[ch][i] = dcBlock[ch].highpass (channels[ch][i]) * output;

            hopIn += inputSquares[static_cast<size_t> (i)];
            hopOut += 0.5f * (channels[0][i] * channels[0][i] + channels[1][i] * channels[1][i]);

            if (++hopCount >= hop)
            {
                const float frame[] { toMeterDb (std::sqrt (hopIn / static_cast<float> (hop))), toMeterDb (std::sqrt (hopOut / static_cast<float> (hop))) };
                meter.push (frame);
                hopCount = 0;
                hopIn = hopOut = 0;
            }
        }
    }
}

model::Json ExciterPlugin::readMeter (const model::Json& params) const
{
    return meter.read (since (params));
}

//==============================================================================
// Bitcrusher: driveDb, bits, rateHz, jitter, tone, outputDb
const char* BitcrusherPlugin::xmlTypeName = "groovekitBitcrusher";

BitcrusherPlugin::BitcrusherPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::bitcrusher)
{
}

void BitcrusherPlugin::initialise (const tracktion::PluginInitialisationInfo&)
{
    phase = 1;
    held[0] = held[1] = 0;
    tone.reset();
    lastTone = -1;
}

void BitcrusherPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.destBuffer->getNumChannels() < 2)
        return;

    const auto drive = dbToGain (value (0));
    const auto levels = std::exp2 (value (1) - 1.0f);
    const auto step = value (2) / sampleRate;
    const auto jitter = value (3) / 100.0f;
    const auto output = dbToGain (value (5));

    if (const auto hz = value (4); hz != lastTone)
    {
        tone.setLowpass (hz, 0.71, sampleRate);
        lastTone = hz;
    }

    const bool filtered = lastTone < 19500 && lastTone < sampleRate * 0.45;
    auto* left = fc.destBuffer->getWritePointer (0, fc.bufferStartSample);
    auto* right = fc.destBuffer->getWritePointer (1, fc.bufferStartSample);
    auto quantise = [levels] (float x) { return std::round (juce::jlimit (-1.0f, 1.0f, x) * levels) / levels; };

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        phase += step;

        if (phase >= 1)
        {
            phase -= std::floor (phase);

            // Jitter: the next sample comes up to half a period late.
            if (jitter > 0)
                phase -= 0.5 * jitter * random.nextFloat();

            held[0] = quantise (left[i] * drive);
            held[1] = quantise (right[i] * drive);
        }

        left[i] = (filtered ? tone.process (held[0], 0) : held[0]) * output;
        right[i] = (filtered ? tone.process (held[1], 1) : held[1]) * output;
    }
}

} // namespace gk::effects
