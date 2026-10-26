#include "AudioChannelPlugin.h"

namespace gk::sampler
{

Filter toFilter (const model::SamplerFilter& f)
{
    Filter filter;
    filter.type = f.type == model::SamplerFilterType::lowpass  ? Filter::Type::lowpass
                : f.type == model::SamplerFilterType::highpass ? Filter::Type::highpass
                : f.type == model::SamplerFilterType::bandpass ? Filter::Type::bandpass
                                                               : Filter::Type::off;
    filter.cutoff = static_cast<float> (f.cutoff);
    filter.resonance = static_cast<float> (f.resonance);
    filter.envelopeAmount = static_cast<float> (f.envelopeAmount);
    filter.envelope = toEnvelope (f.envelope);
    return filter;
}

juce::ADSR::Parameters toEnvelope (const model::Envelope& e)
{
    return { static_cast<float> (e.attack), static_cast<float> (e.decay), static_cast<float> (e.sustain), static_cast<float> (e.release) };
}

//==============================================================================
const char* AudioChannelPlugin::xmlTypeName = "groovekitAudioChannel";

AudioChannelPlugin::AudioChannelPlugin (tracktion::PluginCreationInfo info)
    : Plugin (info)
{
}

AudioChannelPlugin::~AudioChannelPlugin()
{
    notifyListenersOfDeletion();
}

void AudioChannelPlugin::setSettings (const model::AudioInstrument& instrument, std::vector<Clip> clips)
{
    auto made = std::make_shared<Settings>();
    made->envelope = toEnvelope (instrument.envelope);
    made->filter = toFilter (instrument.filter);
    made->clips = std::move (clips);

    // The old settings are released after the lock, off the audio thread.
    std::shared_ptr<const Settings> next = std::move (made);
    const juce::SpinLock::ScopedLockType sl (settingsLock);
    std::swap (settings, next);
}

void AudioChannelPlugin::updateInstrument (const model::AudioInstrument& instrument)
{
    std::vector<Clip> clips;
    {
        const juce::SpinLock::ScopedLockType sl (settingsLock);

        if (settings != nullptr)
            clips = settings->clips;
    }

    setSettings (instrument, std::move (clips));
}

float AudioChannelPlugin::envelopeLevel (const juce::ADSR::Parameters& e, double since, double untilEnd)
{
    double level = e.sustain;

    if (since < e.attack)
        level = since / e.attack;
    else if (since < e.attack + e.decay)
        level = 1.0 - (1.0 - e.sustain) * (since - e.attack) / e.decay;

    if (untilEnd < e.release)
        level *= std::max (0.0, untilEnd) / e.release;

    return static_cast<float> (level);
}

void AudioChannelPlugin::initialise (const tracktion::PluginInitialisationInfo&)
{
    std::fill (&filterState[0][0], &filterState[0][0] + 4, 0.0f);
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
    const auto& f = s->filter;
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
        const auto gain = envelopeLevel (s->envelope, since, untilEnd);
        auto l = left[i] * gain;
        auto r = stereo ? right[i] * gain : l;

        if (filtering)
        {
            if (i % 16 == 0)
                svf.setup (f.cutoff * std::exp2 (f.envelopeAmount * envelopeLevel (f.envelope, since, untilEnd)), f.resonance, sampleRate);

            l = svf.process (l, filterState[0], f.type);
            r = svf.process (r, filterState[1], f.type);
        }

        left[i] = l;

        if (stereo)
            right[i] = r;
    }
}

} // namespace gk::sampler
