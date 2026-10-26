#include "EqPlugin.h"

namespace gk::effects
{

const char* EqPlugin::xmlTypeName = "groovekitEq";

const std::vector<const char*>& EqPlugin::bandTypeNames()
{
    static const std::vector<const char*> names { "off", "lowCut", "lowShelf", "peak", "notch", "highShelf", "highCut" };
    return names;
}

const EqPlugin::BandDefaults EqPlugin::bandDefaults[numBands] {
    { BandType::lowShelf, 80, 0.71 },
    { BandType::peak, 200, 1 },
    { BandType::peak, 500, 1 },
    { BandType::peak, 1200, 1 },
    { BandType::peak, 3000, 1 },
    { BandType::peak, 7000, 1 },
    { BandType::highShelf, 12000, 0.71 },
};

EqPlugin::EqPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::eq)
{
}

void EqPlugin::initialise (const tracktion::PluginInitialisationInfo&)
{
    for (int b = 0; b < numBands; ++b)
    {
        bands[b].freq = -1;   // forces new coefficients
        updateBand (b);

        for (auto& stage : bands[b].stages)
            stage = Biquad { stage.b0, stage.b1, stage.b2, stage.a1, stage.a2 };
    }
}

void EqPlugin::updateBand (int index)
{
    auto& band = bands[index];
    const auto base = static_cast<size_t> (index * 4);
    const auto type = static_cast<BandType> (juce::jlimit (0, 6, choice (base)));
    const auto freq = juce::jlimit (10.0f, static_cast<float> (sampleRate * 0.49), value (base + 1));
    const auto gainDb = value (base + 2);
    const auto q = std::max (0.05f, value (base + 3));

    if (type == band.type && freq == band.freq && gainDb == band.gainDb && q == band.q)
        return;

    band.type = type;
    band.freq = freq;
    band.gainDb = gainDb;
    band.q = q;

    // RBJ Audio EQ Cookbook.
    const auto w0 = juce::MathConstants<double>::twoPi * freq / sampleRate;
    const auto cosw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const auto A = std::pow (10.0, gainDb / 40.0), rootA = std::sqrt (A);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type)
    {
        case BandType::off:
            break;
        case BandType::lowCut:
            b0 = (1 + cosw) / 2; b1 = -(1 + cosw); b2 = b0; a0 = 1 + alpha; a1 = -2 * cosw; a2 = 1 - alpha;
            break;
        case BandType::highCut:
            b0 = (1 - cosw) / 2; b1 = 1 - cosw; b2 = b0; a0 = 1 + alpha; a1 = -2 * cosw; a2 = 1 - alpha;
            break;
        case BandType::peak:
            b0 = 1 + alpha * A; b1 = -2 * cosw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cosw; a2 = 1 - alpha / A;
            break;
        case BandType::notch:
            b0 = 1; b1 = -2 * cosw; b2 = 1; a0 = 1 + alpha; a1 = -2 * cosw; a2 = 1 - alpha;
            break;
        case BandType::lowShelf:
            b0 = A * ((A + 1) - (A - 1) * cosw + 2 * rootA * alpha);
            b1 = 2 * A * ((A - 1) - (A + 1) * cosw);
            b2 = A * ((A + 1) - (A - 1) * cosw - 2 * rootA * alpha);
            a0 = (A + 1) + (A - 1) * cosw + 2 * rootA * alpha;
            a1 = -2 * ((A - 1) + (A + 1) * cosw);
            a2 = (A + 1) + (A - 1) * cosw - 2 * rootA * alpha;
            break;
        case BandType::highShelf:
            b0 = A * ((A + 1) + (A - 1) * cosw + 2 * rootA * alpha);
            b1 = -2 * A * ((A - 1) + (A + 1) * cosw);
            b2 = A * ((A + 1) + (A - 1) * cosw - 2 * rootA * alpha);
            a0 = (A + 1) - (A - 1) * cosw + 2 * rootA * alpha;
            a1 = 2 * ((A - 1) - (A + 1) * cosw);
            a2 = (A + 1) - (A - 1) * cosw - 2 * rootA * alpha;
            break;
    }

    // New coefficients, same state: a parameter that moves doesn't restart the filter.
    for (auto& stage : band.stages)
    {
        stage.b0 = static_cast<float> (b0 / a0);
        stage.b1 = static_cast<float> (b1 / a0);
        stage.b2 = static_cast<float> (b2 / a0);
        stage.a1 = static_cast<float> (a1 / a0);
        stage.a2 = static_cast<float> (a2 / a0);
    }
}

void EqPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& buffer = *fc.destBuffer;
    const auto channels = std::min (2, buffer.getNumChannels());

    for (int b = 0; b < numBands; ++b)
    {
        updateBand (b);
        auto& band = bands[b];

        if (band.type == BandType::off)
            continue;

        const auto stages = band.type == BandType::lowCut || band.type == BandType::highCut ? 2 : 1;

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* d = buffer.getWritePointer (ch, fc.bufferStartSample);

            for (int s = 0; s < stages; ++s)
                for (int i = 0; i < fc.bufferNumSamples; ++i)
                    d[i] = band.stages[s].process (d[i], ch);
        }
    }
}

} // namespace gk::effects
