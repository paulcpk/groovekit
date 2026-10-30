#include "SamplerPlugin.h"

namespace gk::sampler
{

namespace
{
    const juce::Identifier instrumentProperty ("gkInstrument");
    const juce::Identifier projectDirProperty ("gkProjectDir");
}

Processing toProcessing (const model::SamplerLayer& l)
{
    const auto p = l.process.value_or (model::SampleProcessing {});
    Processing r;

    if (p.trimDb)
        r.trimDb = static_cast<float> (*p.trimDb);

    r.removeDc = p.removeDc;
    r.stretchSemitones = static_cast<float> (p.stretchSemitones);
    r.stretchLength = static_cast<float> (p.stretchLength);
    r.pitchBend = static_cast<float> (p.pitchBend);
    r.ringMix = static_cast<float> (p.ringMix);
    r.ringHz = static_cast<float> (p.ringHz);
    r.eq = static_cast<float> (p.eq);
    r.cutoff = static_cast<float> (p.cutoff);
    r.resonance = static_cast<float> (p.resonance);
    r.boostDb = static_cast<float> (p.boostDb);
    r.clip = p.clip;
    r.punch = static_cast<float> (l.punch);
    r.tail = static_cast<float> (l.tail);
    r.reverb = static_cast<float> (p.reverb);
    r.largeReverb = p.reverbSize == model::SampleProcessingReverbSize::large;
    r.stereoDelayMs = static_cast<float> (p.stereoDelayMs);
    r.fadeStereo = p.fadeStereo;
    return r;
}

std::shared_ptr<const Config> buildConfig (const model::SamplerInstrument& instrument, const std::filesystem::path& projectDir)
{
    auto config = std::make_shared<Config>();
    config->polyphony = instrument.polyphony;

    config->filter = toFilter (instrument.filter);
    config->modulation = toModulation (instrument.modulation);

    for (auto& l : instrument.layers)
    {
        auto path = std::filesystem::path (l.sample);

        if (path.is_relative())
            path = projectDir / path;

        Layer layer;
        auto& cache = SampleCache::getInstance();
        layer.sample = cache.processed (cache.load (path), toProcessing (l));
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
        layer.envelope = toEnvelope (l.envelope);

        const auto frames = layer.sample->audio.getNumSamples();
        // Start and end are where they were in the file, wherever processing moved that to.
        const auto toFrame = [&] (double seconds) { return std::clamp (static_cast<int> (std::llround (seconds * layer.sample->timeScale * layer.sample->sampleRate)), 0, frames); };
        layer.start = toFrame (l.start);
        layer.end = l.end ? toFrame (*l.end) : frames;
        layer.reverse = l.reverse;
        layer.loop = l.loop == model::SamplerLayerLoop::forward  ? Layer::Loop::forward
                   : l.loop == model::SamplerLayerLoop::pingPong ? Layer::Loop::pingPong
                                                                 : Layer::Loop::off;
        layer.fadeIn = toFrame (l.fadeIn);
        layer.fadeOut = toFrame (l.fadeOut);
        layer.swapStereo = l.swapStereo;
        layer.length = static_cast<float> (l.length);
        layer.crossfade = toFrame (l.crossfade);

        const auto& g = l.grains;
        layer.grains = { .on = g.on, .size = static_cast<float> (g.size), .density = static_cast<float> (g.density), .speed = static_cast<float> (g.speed),
                         .spray = static_cast<float> (g.spray), .detune = static_cast<float> (g.detune), .width = static_cast<float> (g.width),
                         .shape = g.shape == model::GrainsShape::flat ? Grains::Shape::flat
                                : g.shape == model::GrainsShape::pluck ? Grains::Shape::pluck
                                                                       : Grains::Shape::smooth };

        if (l.invert)
            layer.gain = -layer.gain;

        // Normalized: the part that plays peaks at 0 dB.
        if (l.normalize && layer.end > layer.start)
        {
            float peak = 0;

            for (int c = 0; c < layer.sample->audio.getNumChannels(); ++c)
                peak = std::max (peak, layer.sample->audio.getMagnitude (c, layer.start, layer.end - layer.start));

            if (peak > 1.0e-6f)
                layer.gain /= peak;
        }

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
    filterControls.set (config->filter);
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
    // One tempo per song (a change rebuilds the Edit): read here rather than per block on the
    // audio thread, where the tempo sequence could rebuild its map.
    bpm = edit.tempoSequence.getBpmAt ({});
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

    samplerEngine.setFilterControls (filterControls.cutoff->getCurrentValue(), filterControls.resonance->getCurrentValue(),
                                     filterControls.envelopeAmount->getCurrentValue());
    samplerEngine.setTempo (bpm);
    samplerEngine.process (*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, events);
}

} // namespace gk::sampler
