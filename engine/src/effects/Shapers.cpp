#include "Shapers.h"

namespace gk::effects
{

namespace
{
    model::Json readSince (const MeterHistory& meter, const model::Json& params)
    {
        std::optional<int64_t> since;

        if (auto it = params.find ("since"); it != params.end() && it->is_number_integer())
            since = it->get<int64_t>();

        return meter.read (since);
    }
}

//==============================================================================
// Duck: sync, depth, attackMs, holdMs, release, curve, offset
const char* DuckPlugin::xmlTypeName = "groovekitDuck";

const std::vector<const char*>& DuckPlugin::syncNames()
{
    static const std::vector<const char*> names { "1/16", "1/8", "1/4", "1/2", "1 bar" };
    return names;
}

float DuckPlugin::shape (float phase, double periodMs, float depth, float attackMs, float holdMs, float release, float curve)
{
    const auto t = static_cast<float> (phase * periodMs);
    const auto attack = std::min (attackMs, static_cast<float> (periodMs) * 0.5f);
    const auto hold = std::min (holdMs, static_cast<float> (periodMs) - attack);
    const auto back = std::max (1.0f, release * static_cast<float> (periodMs));

    float dip;

    if (t < attack)
        dip = attack > 0 ? t / attack : 1.0f;
    else if (t < attack + hold)
        dip = 1;
    else
    {
        // Back up again: straight at curve 0, staying down longer and then swooping up at 1.
        const auto u = std::min (1.0f, (t - attack - hold) / back);
        dip = 1 - std::pow (u, 1 + 2 * curve);
    }

    return 1 - depth * dip;
}

DuckPlugin::DuckPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::duck)
{
}

void DuckPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    freePhase = 0;
    hop = MeterHistory::hopSamples (info.sampleRate);
    hopCount = 0;
}

void DuckPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.destBuffer->getNumChannels() < 2)
        return;

    static const double quarters[] { 0.25, 0.5, 1, 2, 4 };
    const auto period = quarters[juce::jlimit (0, 4, choice (0))];
    const auto depth = value (1) / 100.0f, attack = value (2), hold = value (3), release = value (4) / 100.0f, curve = value (5) / 100.0f;
    const auto offset = value (6) / 100.0;
    const auto bpm = edit.tempoSequence.getBpmAt (fc.editTime.getStart());
    const auto periodMs = period * 60000.0 / bpm;
    const auto step = 1.0 / (periodMs * 0.001 * sampleRate);

    // On the song's beats while it plays; on its own while stopped.
    auto phase = fc.isPlaying ? std::fmod (edit.tempoSequence.toBeats (fc.editTime.getStart()).inBeats() / period, 1.0) : freePhase;

    auto* left = fc.destBuffer->getWritePointer (0, fc.bufferStartSample);
    auto* right = fc.destBuffer->getWritePointer (1, fc.bufferStartSample);

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        auto p = phase - offset;
        p -= std::floor (p);
        const auto gain = shape (static_cast<float> (p), periodMs, depth, attack, hold, release, curve);
        left[i] *= gain;
        right[i] *= gain;

        if (++hopCount >= hop)
        {
            const float frame[] { static_cast<float> (p * 100), toMeterDb (gain) };
            meter.push (frame);
            hopCount = 0;
        }

        phase += step;
        phase -= std::floor (phase);
    }

    freePhase = phase;
}

model::Json DuckPlugin::readMeter (const model::Json& params) const
{
    return readSince (meter, params);
}

//==============================================================================
// Clipper: driveDb, ceilingDb, softness, outputDb
const char* ClipperPlugin::xmlTypeName = "groovekitClipper";

float ClipperPlugin::clip (float x, float ceiling, float softness)
{
    const auto knee = 0.5f * juce::jlimit (0.0f, 1.0f, softness) * ceiling;
    const auto linear = ceiling - knee;
    const auto a = std::abs (x);

    if (a <= linear)
        return x;

    const auto y = knee > 0 ? linear + knee * std::tanh ((a - linear) / knee) : ceiling;
    return x < 0 ? -y : y;
}

ClipperPlugin::ClipperPlugin (tracktion::PluginCreationInfo creation)
    : EffectPlugin (creation, model::BuiltinEffectKind::clipper)
{
}

ClipperPlugin::~ClipperPlugin() = default;

double ClipperPlugin::getLatencySeconds()
{
    return oversampling != nullptr && sampleRate > 0 ? oversampling->getLatencyInSamples() / sampleRate : 0.0;
}

void ClipperPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampling->initProcessing (static_cast<size_t> (std::max (1, info.blockSizeSamples)));
    inputPeaks.reserve (static_cast<size_t> (std::max (1, info.blockSizeSamples)));
    hop = MeterHistory::hopSamples (info.sampleRate);
    hopCount = 0;
    hopIn = hopOut = 0;
}

void ClipperPlugin::deinitialise()
{
    oversampling.reset();
}

void ClipperPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.destBuffer->getNumChannels() < 2 || oversampling == nullptr)
        return;

    const auto drive = juce::Decibels::decibelsToGain (value (0));
    const auto ceiling = juce::Decibels::decibelsToGain (value (1));
    const auto softness = value (2) / 100.0f;
    const auto output = juce::Decibels::decibelsToGain (value (3));

    auto* channels = fc.destBuffer->getArrayOfWritePointers();
    float* two[] { channels[0] + fc.bufferStartSample, channels[1] + fc.bufferStartSample };
    juce::dsp::AudioBlock<float> block (two, 2, static_cast<size_t> (fc.bufferNumSamples));

    // What comes in, driven, for the meters.
    inputPeaks.resize (static_cast<size_t> (fc.bufferNumSamples));

    for (int i = 0; i < fc.bufferNumSamples; ++i)
        inputPeaks[static_cast<size_t> (i)] = drive * std::max (std::abs (two[0][i]), std::abs (two[1][i]));

    // Up four times, clipped there, and back down.
    auto up = oversampling->processSamplesUp (block);

    for (size_t c = 0; c < up.getNumChannels(); ++c)
    {
        auto* d = up.getChannelPointer (c);

        for (size_t i = 0; i < up.getNumSamples(); ++i)
            d[i] = clip (d[i] * drive, ceiling, softness);
    }

    oversampling->processSamplesDown (block);

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        hopIn = std::max (hopIn, inputPeaks[static_cast<size_t> (i)]);
        two[0][i] *= output;
        two[1][i] *= output;
        hopOut = std::max (hopOut, std::max (std::abs (two[0][i]), std::abs (two[1][i])));

        if (++hopCount >= hop)
        {
            // How much was taken off the peaks: the input as driven against the ceiling.
            const float frame[] { toMeterDb (hopIn), toMeterDb (hopOut), std::min (0.0f, toMeterDb (ceiling) - toMeterDb (hopIn)) };
            meter.push (frame);
            hopCount = 0;
            hopIn = hopOut = 0;
        }
    }
}

model::Json ClipperPlugin::readMeter (const model::Json& params) const
{
    return readSince (meter, params);
}

} // namespace gk::effects
