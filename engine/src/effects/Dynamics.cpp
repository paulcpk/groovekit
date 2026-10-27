#include "Dynamics.h"

namespace gk::effects
{

namespace
{
    std::optional<int64_t> since (const model::Json& params)
    {
        if (auto it = params.find ("since"); it != params.end() && it->is_number())
            return it->get<int64_t>();

        return {};
    }

    float dbToGain (float db)   { return std::exp (db * 0.11512925f); }   // 10^(db/20)
}

//==============================================================================
void Biquad::setHighpass (double hz, double q, double sampleRate)
{
    const auto w0 = juce::MathConstants<double>::twoPi * std::min (hz, sampleRate * 0.49) / sampleRate;
    const auto cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), a0 = 1 + alpha;
    b0 = static_cast<float> ((1 + cosw) / 2 / a0);
    b1 = static_cast<float> (-(1 + cosw) / a0);
    b2 = b0;
    a1 = static_cast<float> (-2 * cosw / a0);
    a2 = static_cast<float> ((1 - alpha) / a0);
}

void Biquad::setLowpass (double hz, double q, double sampleRate)
{
    const auto w0 = juce::MathConstants<double>::twoPi * std::min (hz, sampleRate * 0.49) / sampleRate;
    const auto cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), a0 = 1 + alpha;
    b0 = static_cast<float> ((1 - cosw) / 2 / a0);
    b1 = static_cast<float> ((1 - cosw) / a0);
    b2 = b0;
    a1 = static_cast<float> (-2 * cosw / a0);
    a2 = static_cast<float> ((1 - alpha) / a0);
}

void Biquad::setBandpass (double hz, double q, double sampleRate)
{
    const auto w0 = juce::MathConstants<double>::twoPi * std::min (hz, sampleRate * 0.49) / sampleRate;
    const auto cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), a0 = 1 + alpha;
    b0 = static_cast<float> (alpha / a0);
    b1 = 0;
    b2 = static_cast<float> (-alpha / a0);
    a1 = static_cast<float> (-2 * cosw / a0);
    a2 = static_cast<float> ((1 - alpha) / a0);
}

//==============================================================================
// Compressor: thresholdDb, ratio, attackMs, releaseMs, outputDb, kneeDb, detectLowCut
const char* CompressorPlugin::xmlTypeName = "groovekitCompressor";

CompressorPlugin::CompressorPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::compressor)
{
}

void CompressorPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    smoother.gainDb = 0;
    detectorFilter.reset();
    lastLowCut = -1;
    hop = MeterHistory::hopSamples (info.sampleRate);
    hopCount = 0;
    hopIn = hopOut = hopGain = 0;
}

void CompressorPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.destBuffer->getNumChannels() < 2)
        return;

    const GainComputer curve { value (0), value (1), value (5) };
    smoother.setTimes (value (2), value (3), sampleRate);
    const auto makeup = value (4);

    if (const auto lowCut = value (6); lowCut != lastLowCut)
    {
        detectorFilter.setHighpass (lowCut, 0.71, sampleRate);
        lastLowCut = lowCut;
    }

    const bool filtered = lastLowCut > 20.5f;
    auto* left = fc.destBuffer->getWritePointer (0, fc.bufferStartSample);
    auto* right = fc.destBuffer->getWritePointer (1, fc.bufferStartSample);

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const auto in = std::max (std::abs (left[i]), std::abs (right[i]));
        auto level = in;

        if (filtered)
            level = std::max (std::abs (detectorFilter.process (left[i], 0)), std::abs (detectorFilter.process (right[i], 1)));

        const auto gainDb = smoother.process (curve.gainDb (toMeterDb (level)));
        const auto gain = dbToGain (gainDb + makeup);
        left[i] *= gain;
        right[i] *= gain;

        hopIn = std::max (hopIn, in);
        hopOut = std::max (hopOut, in * gain);
        hopGain = std::min (hopGain, gainDb);

        if (++hopCount >= hop)
        {
            const float frame[] { toMeterDb (hopIn), toMeterDb (hopOut), hopGain };
            meter.push (frame);
            hopCount = 0;
            hopIn = hopOut = hopGain = 0;
        }
    }
}

model::Json CompressorPlugin::readMeter (const model::Json& params) const
{
    return meter.read (since (params));
}

//==============================================================================
// Multiband: bands, lowFreq, midFreq, highFreq, inputDb, outputDb, kneeDb, detect, then per band
// thresholdDb, ratio, belowDb, belowRatio, attackMs, releaseMs, gainDb, mode
const char* MultibandPlugin::xmlTypeName = "groovekitMultiband";

const std::vector<const char*>& MultibandPlugin::modeNames()
{
    static const std::vector<const char*> names { "on", "bypass", "solo", "mute" };
    return names;
}

MultibandPlugin::MultibandPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::multiband)
{
}

void MultibandPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    const juce::dsp::ProcessSpec spec { info.sampleRate, static_cast<juce::uint32> (std::max (1, info.blockSizeSamples)), 2 };

    for (auto* f : { &splitMid, &splitLow, &splitHigh, &allpassLow, &allpassHigh })
    {
        f->prepare (spec);
        f->setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    }

    allpassLow.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    allpassHigh.setType (juce::dsp::LinkwitzRileyFilterType::allpass);

    for (auto& s : smoothers)
        s.gainDb = 0;

    std::fill (std::begin (rms), std::end (rms), 0.0f);
    rmsCoefficient = GainSmoother::coefficient (10.0f, info.sampleRate);
    hop = MeterHistory::hopSamples (info.sampleRate);
    hopCount = 0;
    resetHop();
}

void MultibandPlugin::resetHop()
{
    std::fill (std::begin (hopValues), std::end (hopValues), 0.0f);
}

void MultibandPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.destBuffer->getNumChannels() < 2)
        return;

    const int numBands = choice (0) == 1 ? 4 : 3;

    // Crossovers kept in order and apart.
    const auto nyquist = static_cast<float> (sampleRate * 0.45);
    const auto lowHz = juce::jlimit (20.0f, nyquist / 2, value (1));
    const auto highHz = juce::jlimit (lowHz * 1.25f, nyquist, value (3));
    const auto midHz = juce::jlimit (lowHz * 1.12f, highHz / 1.12f, value (2));

    splitLow.setCutoffFrequency (lowHz);
    allpassLow.setCutoffFrequency (lowHz);
    splitHigh.setCutoffFrequency (highHz);
    allpassHigh.setCutoffFrequency (highHz);
    splitMid.setCutoffFrequency (midHz);

    const auto input = dbToGain (value (4));
    const auto output = dbToGain (value (5));
    const auto knee = value (6);
    const bool rmsDetect = choice (7) == 1;

    GainComputer curves[maxBands];
    float makeups[maxBands] {};
    int modes[maxBands] {};
    bool anySolo = false;

    for (int b = 0; b < numBands; ++b)
    {
        curves[b] = { value (bandParam (b, threshold)), value (bandParam (b, ratio)), knee,
                      value (bandParam (b, belowThreshold)), value (bandParam (b, belowRatio)) };
        smoothers[b].setTimes (value (bandParam (b, attack)), value (bandParam (b, release)), sampleRate);
        makeups[b] = value (bandParam (b, gain));
        modes[b] = choice (bandParam (b, mode));
        anySolo = anySolo || modes[b] == 2;
    }

    auto* channels = fc.destBuffer->getArrayOfWritePointers();
    auto* left = channels[0] + fc.bufferStartSample;
    auto* right = channels[1] + fc.bufferStartSample;

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        float bands[maxBands][2] {};
        float in[2] { left[i] * input, right[i] * input };

        for (int ch = 0; ch < 2; ++ch)
        {
            if (numBands == 4)
            {
                float lows, highs;
                splitMid.processSample (ch, in[ch], lows, highs);
                splitLow.processSample (ch, allpassHigh.processSample (ch, lows), bands[0][ch], bands[1][ch]);
                splitHigh.processSample (ch, allpassLow.processSample (ch, highs), bands[2][ch], bands[3][ch]);
            }
            else
            {
                float rest;
                splitLow.processSample (ch, in[ch], bands[0][ch], rest);
                splitHigh.processSample (ch, rest, bands[1][ch], bands[2][ch]);
                bands[0][ch] = allpassHigh.processSample (ch, bands[0][ch]);
            }
        }

        float out[2] {};

        for (int b = 0; b < numBands; ++b)
        {
            const auto peak = std::max (std::abs (bands[b][0]), std::abs (bands[b][1]));
            auto level = peak;

            if (rmsDetect)
            {
                const auto square = 0.5f * (bands[b][0] * bands[b][0] + bands[b][1] * bands[b][1]);
                rms[b] = square + rmsCoefficient * (rms[b] - square);
                level = std::sqrt (2.0f * rms[b]);
            }

            auto gainDb = smoothers[b].process (curves[b].gainDb (toMeterDb (level)));
            auto gain = 1.0f;

            if (modes[b] == 1)
                gainDb = 0;
            else
                gain = dbToGain (gainDb + makeups[b]);

            const bool heard = modes[b] != 3 && (! anySolo || modes[b] == 2);

            if (heard)
            {
                out[0] += bands[b][0] * gain;
                out[1] += bands[b][1] * gain;
            }

            auto* v = hopValues + 2 + 3 * b;
            v[0] = std::max (v[0], peak);
            v[1] = std::max (v[1], heard ? peak * gain : 0.0f);
            v[2] = std::abs (gainDb) > std::abs (v[2]) ? gainDb : v[2];
        }

        hopValues[0] = std::max (hopValues[0], std::max (std::abs (in[0]), std::abs (in[1])));
        left[i] = out[0] * output;
        right[i] = out[1] * output;
        hopValues[1] = std::max (hopValues[1], std::max (std::abs (left[i]), std::abs (right[i])));

        if (++hopCount >= hop)
        {
            float frame[2 + 3 * maxBands];

            for (int k = 0; k < 2 + 3 * maxBands; ++k)
                frame[k] = (k >= 2 && (k - 2) % 3 == 2) ? hopValues[k] : toMeterDb (hopValues[k]);

            meter.push (frame);
            hopCount = 0;
            resetHop();
        }
    }
}

model::Json MultibandPlugin::readMeter (const model::Json& params) const
{
    return meter.read (since (params));
}

} // namespace gk::effects
