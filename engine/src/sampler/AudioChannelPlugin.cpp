#include "AudioChannelPlugin.h"

namespace gk::sampler
{

Filter toFilter (const model::SamplerFilter& f)
{
    Filter filter;
    switch (f.type)
    {
        case model::SamplerFilterType::lowpass:     filter.type = Filter::Type::lowpass; break;
        case model::SamplerFilterType::lowpass24:   filter.type = Filter::Type::lowpass24; break;
        case model::SamplerFilterType::highpass:    filter.type = Filter::Type::highpass; break;
        case model::SamplerFilterType::highpass24:  filter.type = Filter::Type::highpass24; break;
        case model::SamplerFilterType::bandpass:    filter.type = Filter::Type::bandpass; break;
        case model::SamplerFilterType::notch:       filter.type = Filter::Type::notch; break;
        case model::SamplerFilterType::off:         filter.type = Filter::Type::off; break;
    }

    filter.cutoff = static_cast<float> (f.cutoff);
    filter.resonance = static_cast<float> (f.resonance);
    filter.envelopeAmount = static_cast<float> (f.envelopeAmount);
    filter.envelope = toEnvelope (f.envelope);
    return filter;
}

EnvelopeShape toEnvelope (const model::Envelope& e)
{
    const auto f = [] (double v) { return static_cast<float> (v); };
    return { f (e.delay), f (e.attack), f (e.hold), f (e.decay), f (e.sustain), f (e.release), f (e.attackCurve), f (e.decayCurve), f (e.releaseCurve) };
}

namespace
{
    LfoSettings toLfo (const std::optional<model::Lfo>& from)
    {
        LfoSettings lfo;

        if (! from)
            return lfo;

        using S = LfoSettings::Shape;
        lfo.shape = from->shape == model::LfoShape::triangle ? S::triangle
                  : from->shape == model::LfoShape::square   ? S::square
                  : from->shape == model::LfoShape::saw      ? S::saw
                  : from->shape == model::LfoShape::random   ? S::random
                                                             : S::sine;
        lfo.amount = static_cast<float> (from->amount);
        lfo.speed = static_cast<float> (from->speed);
        lfo.syncBeats = static_cast<float> (from->sync.value_or (0.0));
        lfo.delay = static_cast<float> (from->delay);
        lfo.attack = static_cast<float> (from->attack);
        lfo.retrigger = from->retrigger;
        return lfo;
    }

    Modulation::Target toTarget (const std::optional<model::ModulationTarget>& from)
    {
        Modulation::Target target;

        if (from)
        {
            target.envelope = toEnvelope (from->envelope);
            target.amount = static_cast<float> (from->amount);
            target.lfo = toLfo (from->lfo);
        }

        return target;
    }
}

Modulation toModulation (const std::optional<model::SamplerModulation>& from)
{
    Modulation m;

    if (! from)
        return m;

    if (from->volume)   m.volume = toLfo (from->volume->lfo);
    if (from->cutoff)   m.cutoff = toLfo (from->cutoff->lfo);
    m.resonance = toTarget (from->resonance);
    m.pan = toTarget (from->pan);
    m.pitch = toTarget (from->pitch);
    return m;
}

//==============================================================================
FilterControls::FilterControls (tracktion::Plugin& plugin)
{
    const struct { const char* id; float min, max, initial; } specs[] {
        { "filterCutoff", 20.0f, 20000.0f, 20000.0f },
        { "filterResonance", 0.0f, 1.0f, 0.0f },
        { "filterEnvelopeAmount", -10.0f, 10.0f, 0.0f },
    };

    tracktion::AutomatableParameter::Ptr* params[] { &cutoff, &resonance, &envelopeAmount };

    for (size_t i = 0; i < 3; ++i)
    {
        values[i].referTo (plugin.state, specs[i].id, nullptr, specs[i].initial);
        *params[i] = plugin.addParam (specs[i].id, specs[i].id, { specs[i].min, specs[i].max });
        (*params[i])->attachToCurrentValue (values[i]);
    }
}

FilterControls::~FilterControls()
{
    for (auto p : { cutoff, resonance, envelopeAmount })
        p->detachFromCurrentValue();
}

void FilterControls::set (const Filter& filter)
{
    cutoff->setParameter (filter.cutoff, juce::dontSendNotification);
    resonance->setParameter (filter.resonance, juce::dontSendNotification);
    envelopeAmount->setParameter (filter.envelopeAmount, juce::dontSendNotification);
}

Filter FilterControls::apply (Filter filter) const
{
    filter.cutoff = cutoff->getCurrentValue();
    filter.resonance = resonance->getCurrentValue();
    filter.envelopeAmount = envelopeAmount->getCurrentValue();
    return filter;
}

//==============================================================================
const char* AudioChannelPlugin::xmlTypeName = "groovekitAudioClips";

AudioChannelPlugin::AudioChannelPlugin (tracktion::PluginCreationInfo info)
    : Plugin (info)
{
}

AudioChannelPlugin::~AudioChannelPlugin()
{
    notifyListenersOfDeletion();
}

void AudioChannelPlugin::setSettings (const model::SamplerInstrument& instrument, std::vector<Clip> clips)
{
    auto made = std::make_shared<Settings>();

    if (! instrument.layers.empty())
    {
        auto& layer = instrument.layers.front();
        const auto gain = juce::Decibels::decibelsToGain (static_cast<float> (layer.gainDb), -150.0f);
        const auto pan = static_cast<float> (layer.pan);
        made->gainL = gain * std::min (1.0f, 1.0f - pan);
        made->gainR = gain * std::min (1.0f, 1.0f + pan);
        made->envelope = toEnvelope (layer.envelope);
    }

    made->filter = toFilter (instrument.filter);
    made->clips = std::move (clips);
    filterControls.set (made->filter);

    // The old settings are released after the lock, off the audio thread.
    std::shared_ptr<const Settings> next = std::move (made);
    const juce::SpinLock::ScopedLockType sl (settingsLock);
    std::swap (settings, next);
}

void AudioChannelPlugin::updateInstrument (const model::SamplerInstrument& instrument)
{
    std::vector<Clip> clips;
    {
        const juce::SpinLock::ScopedLockType sl (settingsLock);

        if (settings != nullptr)
            clips = settings->clips;
    }

    setSettings (instrument, std::move (clips));
}

void AudioChannelPlugin::initialise (const tracktion::PluginInitialisationInfo&)
{
    std::fill (&filterState[0][0], &filterState[0][0] + 8, 0.0f);
}

void AudioChannelPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || fc.bufferNumSamples == 0)
        return;

    std::shared_ptr<const Settings> s;
    {
        const juce::SpinLock::ScopedTryLockType sl (settingsLock);

        if (! sl.isLocked())
            return;

        s = settings;
    }

    if (s == nullptr || s->clips.empty())
        return;

    auto& buffer = *fc.destBuffer;
    auto* left = buffer.getWritePointer (0, fc.bufferStartSample);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0, fc.bufferStartSample);
    const bool stereo = buffer.getNumChannels() > 1;
    const auto start = fc.editTime.getStart().inSeconds();
    const auto f = filterControls.apply (s->filter);
    const bool filtering = f.type != Filter::Type::off;

    // The clip under the block's start; later samples move on from there.
    auto clip = std::upper_bound (s->clips.begin(), s->clips.end(), start, [] (double t, const Clip& c) { return t < c.end; });

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const auto t = start + i / sampleRate;

        while (clip != s->clips.end() && t >= clip->end)
            ++clip;

        if (clip == s->clips.end() || t < clip->start)
            continue;

        const auto since = t - clip->start, untilEnd = clip->end - t;
        const auto gain = s->envelope.levelAt (since, untilEnd);
        auto l = left[i] * gain * s->gainL;
        auto r = (stereo ? right[i] : left[i]) * gain * s->gainR;

        if (filtering)
        {
            if (i % 16 == 0)
                filterStages.setup (f.type, f.cutoff * std::exp2 (f.envelopeAmount * f.envelope.levelAt (since, untilEnd)), f.resonance, sampleRate);

            l = filterStages.process (l, filterState[0], f.type);
            r = filterStages.process (r, filterState[1], f.type);
        }

        left[i] = l;

        if (stereo)
            right[i] = r;
    }
}

} // namespace gk::sampler
