#include "SamplerPlugin.h"

namespace gk::sampler
{

namespace
{
    const juce::Identifier instrumentProperty ("gkInstrument");
    const juce::Identifier projectDirProperty ("gkProjectDir");
}

std::shared_ptr<const Config> buildConfig (const model::SamplerInstrument& instrument, const std::filesystem::path& projectDir)
{
    auto config = std::make_shared<Config>();
    config->polyphony = instrument.polyphony;

    auto& f = instrument.filter;
    config->filter.type = f.type == model::SamplerFilterType::lowpass  ? Filter::Type::lowpass
                        : f.type == model::SamplerFilterType::highpass ? Filter::Type::highpass
                        : f.type == model::SamplerFilterType::bandpass ? Filter::Type::bandpass
                                                                       : Filter::Type::off;
    config->filter.cutoff = static_cast<float> (f.cutoff);
    config->filter.resonance = static_cast<float> (f.resonance);
    config->filter.envelopeAmount = static_cast<float> (f.envelopeAmount);
    config->filter.envelope = { static_cast<float> (f.envelope.attack), static_cast<float> (f.envelope.decay),
                                static_cast<float> (f.envelope.sustain), static_cast<float> (f.envelope.release) };

    for (auto& l : instrument.layers)
    {
        auto path = std::filesystem::path (l.sample);

        if (path.is_relative())
            path = projectDir / path;

        Layer layer;
        layer.sample = SampleCache::getInstance().load (path);
        layer.rootKey = l.rootKey;
        layer.keyLow = l.keyRange.low;
        layer.keyHigh = l.keyRange.high;
        layer.velocityLow = l.velocityRange.low;
        layer.velocityHigh = l.velocityRange.high;
        layer.pitchTracking = l.pitchTracking;
        layer.tuneSemitones = l.transpose + l.fineTune / 100.0;
        layer.gain = juce::Decibels::decibelsToGain (static_cast<float> (l.gainDb), -150.0f);
        layer.pan = static_cast<float> (l.pan);
        layer.oneShot = l.mode == model::SamplerLayerMode::oneShot;
        layer.envelope = { static_cast<float> (l.envelope.attack), static_cast<float> (l.envelope.decay),
                           static_cast<float> (l.envelope.sustain), static_cast<float> (l.envelope.release) };

        const auto frames = layer.sample->audio.getNumSamples();
        const auto toFrame = [&] (double seconds) { return std::clamp (static_cast<int> (std::llround (seconds * layer.sample->sampleRate)), 0, frames); };
        layer.start = toFrame (l.start);
        layer.end = l.end ? toFrame (*l.end) : frames;
        layer.reverse = l.reverse;
        config->layers.push_back (std::move (layer));
    }

    return config;
}

//==============================================================================
const char* SamplerPlugin::xmlTypeName = "groovekitSampler";

SamplerPlugin::SamplerPlugin (tracktion::PluginCreationInfo info)
    : Plugin (info)
{
    events.reserve (1024);
    restoreFromState();
}

SamplerPlugin::~SamplerPlugin()
{
    notifyListenersOfDeletion();
}

void SamplerPlugin::setInstrument (const model::SamplerInstrument& instrument, const std::filesystem::path& projectDir)
{
    auto config = buildConfig (instrument, projectDir);

    tailSeconds = 0;

    for (auto& l : instrument.layers)
        tailSeconds = std::max (tailSeconds, l.envelope.release);

    if (instrument.filter.type != model::SamplerFilterType::off)
        tailSeconds = std::max (tailSeconds, instrument.filter.envelope.release);

    state.setProperty (instrumentProperty, juce::String (model::toJson (instrument).dump()), nullptr);
    state.setProperty (projectDirProperty, juce::String (projectDir.string()), nullptr);
    samplerEngine.setConfig (std::move (config));
}

void SamplerPlugin::restoreFromState()
{
    auto json = state[instrumentProperty].toString().toStdString();

    if (json.empty())
        return;

    try
    {
        model::Reader reader;
        model::SamplerInstrument instrument;
        readValue (reader, model::Json::parse (json), instrument);
        setInstrument (instrument, state[projectDirProperty].toString().toStdString());
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog (juce::String ("GrooveKit sampler: ") + e.what());
    }
}

double SamplerPlugin::getTailLength() const
{
    return tailSeconds;
}

void SamplerPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    samplerEngine.prepare (info.sampleRate, info.blockSizeSamples);
}

void SamplerPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    for (int ch = 0; ch < fc.destBuffer->getNumChannels(); ++ch)
        fc.destBuffer->clear (ch, fc.bufferStartSample, fc.bufferNumSamples);

    events.clear();

    if (auto midi = fc.bufferForMidiMessages)
    {
        if (midi->isAllNotesOff)
            samplerEngine.allNotesOff();

        for (auto& m : *midi)
            if (events.size() < events.capacity())
                events.push_back ({ static_cast<int> (std::floor (m.getTimeStamp() * sampleRate + 0.5)), m });
    }

    samplerEngine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, events);
}

} // namespace gk::sampler
