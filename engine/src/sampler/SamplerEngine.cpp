#include "SamplerEngine.h"

namespace gk::sampler
{

namespace
{
    /** 4-point Hermite interpolation; returns d[pos] exactly when pos is a whole number. */
    float interpolate (const float* d, int numSamples, double pos)
    {
        const auto i = static_cast<int> (pos);
        const auto f = static_cast<float> (pos - i);
        auto at = [&] (int k) { return k >= 0 && k < numSamples ? d[k] : 0.0f; };

        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

        return ((c3 * f + c2) * f + c1) * f + x0;
    }
}

SamplerEngine::SamplerEngine()
    : voices (maxVoices)
{
}

void SamplerEngine::prepare (double newSampleRate, int)
{
    const juce::ScopedLock sl (configLock);
    sampleRate = newSampleRate;
    stealFadeSamples = std::max (1, juce::roundToInt (stealFadeSeconds * sampleRate));
    edgeFadeSamples = std::max (1, juce::roundToInt (0.001 * sampleRate));

    for (auto& v : voices)
    {
        v.active = false;
        v.envelope.setSampleRate (sampleRate);
        v.filterEnvelope.setSampleRate (sampleRate);
    }
}

void SamplerEngine::setConfig (std::shared_ptr<const Config> newConfig)
{
    {
        const juce::ScopedLock sl (configLock);
        std::swap (config, newConfig);

        // newConfig now holds the previous one.
        const bool sameSamples = newConfig != nullptr && config != nullptr && newConfig->layers.size() == config->layers.size()
            && std::equal (config->layers.begin(), config->layers.end(), newConfig->layers.begin(),
                           [] (const Layer& a, const Layer& b) { return a.sample == b.sample && a.start == b.start && a.endFrame() == b.endFrame() && a.reverse == b.reverse; });

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            if (! sameSamples)
            {
                v.active = false;
                continue;
            }

            v.layer = &config->layers[v.layerIndex];
            v.envelope.setParameters (v.layer->envelope);
            v.filterEnvelope.setParameters (config->filter.envelope);
            v.samplesToCoefficients = 0;
        }
    }

    // The previous config is released here, off the audio thread.
}

void SamplerEngine::allNotesOff()
{
    for (auto& v : voices)
        v.active = false;
}

int SamplerEngine::getNumActiveVoices() const
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.active; }));
}

void SamplerEngine::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                             const std::vector<MidiEvent>& events)
{
    const juce::ScopedTryLock sl (configLock);

    if (! sl.isLocked() || config == nullptr)
        return;

    int position = 0;

    for (auto& e : events)
    {
        const auto offset = juce::jlimit (0, numSamples, e.sampleOffset);

        if (offset > position)
        {
            renderVoices (buffer, startSample + position, offset - position);
            position = offset;
        }

        if (e.message.isNoteOn())
            noteOn (e.message.getNoteNumber(), e.message.getVelocity());
        else if (e.message.isNoteOff())
            noteOff (e.message.getNoteNumber());
        else if (e.message.isAllNotesOff() || e.message.isAllSoundOff())
            allNotesOff();
    }

    if (position < numSamples)
        renderVoices (buffer, startSample + position, numSamples - position);
}

void SamplerEngine::noteOn (int note, int velocity)
{
    for (auto& layer : config->layers)
        if (note >= layer.keyLow && note <= layer.keyHigh
             && velocity >= layer.velocityLow && velocity <= layer.velocityHigh
             && layer.sample != nullptr && layer.endFrame() > layer.start)
            startVoice (layer, note, velocity);
}

void SamplerEngine::noteOff (int note)
{
    for (auto& v : voices)
    {
        if (v.active && v.note == note && ! v.layer->oneShot)
        {
            v.envelope.noteOff();
            v.filterEnvelope.noteOff();
        }
    }
}

void SamplerEngine::startVoice (const Layer& layer, int note, int velocity)
{
    const auto sounding = std::count_if (voices.begin(), voices.end(),
                                         [] (const Voice& v) { return v.active && ! v.isStolen(); });

    if (sounding >= config->polyphony)
        stealOldestVoice();

    auto* v = findFreeVoice();

    const auto semitones = (layer.pitchTracking ? note - layer.rootKey : 0) + layer.tuneSemitones;
    const auto velocityGain = static_cast<float> (velocity) / 127.0f;

    v->layer = &layer;
    v->layerIndex = static_cast<size_t> (&layer - config->layers.data());
    v->note = note;
    v->age = nextAge++;
    v->position = 0;
    v->increment = std::pow (2.0, semitones / 12.0) * layer.sample->sampleRate / sampleRate;
    v->gainL = layer.gain * velocityGain * std::min (1.0f, 1.0f - layer.pan);
    v->gainR = layer.gain * velocityGain * std::min (1.0f, 1.0f + layer.pan);
    v->stealSamplesLeft = 0;
    v->envelope.setParameters (layer.envelope);
    v->envelope.reset();
    v->envelope.noteOn();
    v->filterEnvelope.setParameters (config->filter.envelope);
    v->filterEnvelope.reset();
    v->filterEnvelope.noteOn();
    std::fill (&v->filterState[0][0], &v->filterState[0][0] + 4, 0.0f);
    v->samplesToCoefficients = 0;

    // Where the played part cuts into the file (rather than starting or ending with it), a 1 ms
    // fade keeps the cut from clicking.
    const auto fileLength = layer.sample->audio.getNumSamples();
    const bool cutAtStart = layer.reverse ? layer.endFrame() < fileLength : layer.start > 0;
    const bool cutAtEnd = layer.reverse ? layer.start > 0 : layer.endFrame() < fileLength;
    v->fadeInSamples = cutAtStart ? edgeFadeSamples : 0;
    v->fadeOutSamples = cutAtEnd ? edgeFadeSamples : 0;
    v->active = true;
}

SamplerEngine::Voice* SamplerEngine::findFreeVoice()
{
    Voice* oldest = nullptr;

    for (auto& v : voices)
    {
        if (! v.active)
            return &v;

        if (oldest == nullptr || v.age < oldest->age)
            oldest = &v;
    }

    // Every slot is busy (only possible with many voices fading out): cut the oldest.
    return oldest;
}

void SamplerEngine::stealOldestVoice()
{
    Voice* oldest = nullptr;

    for (auto& v : voices)
        if (v.active && ! v.isStolen() && (oldest == nullptr || v.age < oldest->age))
            oldest = &v;

    if (oldest != nullptr)
        oldest->stealSamplesLeft = stealFadeSamples;
}

void SamplerEngine::renderVoices (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    auto* left = buffer.getWritePointer (0, startSample);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0, startSample);

    for (auto& v : voices)
        if (v.active)
            renderVoice (v, left, right, numSamples);
}

void SamplerEngine::updateFilter (Voice& v, float envelopeLevel)
{
    const auto& f = config->filter;
    const auto cutoff = juce::jlimit (16.0f, static_cast<float> (sampleRate * 0.45),
                                      f.cutoff * std::exp2 (f.envelopeAmount * envelopeLevel));
    const auto g = std::tan (juce::MathConstants<float>::pi * cutoff / static_cast<float> (sampleRate));

    v.k = 1.0f / Filter::qFor (f.resonance);
    v.a1 = 1.0f / (1.0f + g * (g + v.k));
    v.a2 = g * v.a1;
    v.a3 = g * v.a2;
}

void SamplerEngine::renderVoice (Voice& v, float* left, float* right, int numSamples)
{
    const auto& layer = *v.layer;
    auto& audio = layer.sample->audio;
    const auto fileLength = audio.getNumSamples();
    const auto end = layer.endFrame();
    const auto length = end - layer.start;
    const auto* srcL = audio.getReadPointer (0);
    const auto* srcR = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);
    const auto filterType = config->filter.type;

    // The TPT state-variable filter (Zavalishin), with coefficients refreshed every 16 samples.
    auto filter = [&v, filterType] (float in, float* state)
    {
        const auto v3 = in - state[1];
        const auto v1 = v.a1 * state[0] + v.a2 * v3;
        const auto v2 = state[1] + v.a2 * state[0] + v.a3 * v3;
        state[0] = 2.0f * v1 - state[0];
        state[1] = 2.0f * v2 - state[1];

        switch (filterType)
        {
            case Filter::Type::lowpass:   return v2;
            case Filter::Type::highpass:  return in - v.k * v1 - v2;
            case Filter::Type::bandpass:  return v.k * v1;
            case Filter::Type::off:       break;
        }

        return in;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.position >= length || ! v.envelope.isActive())
        {
            v.active = false;
            return;
        }

        auto gain = v.envelope.getNextSample();
        const auto filterLevel = v.filterEnvelope.getNextSample();

        if (v.isStolen())
        {
            gain *= static_cast<float> (v.stealSamplesLeft) / static_cast<float> (stealFadeSamples);

            if (--v.stealSamplesLeft == 0)
            {
                v.active = false;
                return;
            }
        }

        if (v.fadeInSamples > 0 && v.position < edgeFadeSamples)
            gain *= static_cast<float> (v.position) / static_cast<float> (edgeFadeSamples);

        if (v.fadeOutSamples > 0 && length - v.position < edgeFadeSamples)
            gain *= static_cast<float> ((length - v.position) / edgeFadeSamples);

        const auto at = layer.reverse ? end - 1 - v.position : layer.start + v.position;
        auto l = interpolate (srcL, fileLength, at) * v.gainL * gain;
        auto r = interpolate (srcR, fileLength, at) * v.gainR * gain;

        if (filterType != Filter::Type::off)
        {
            if (--v.samplesToCoefficients <= 0)
            {
                updateFilter (v, filterLevel);
                v.samplesToCoefficients = 16;
            }

            l = filter (l, v.filterState[0]);
            r = filter (r, v.filterState[1]);
        }

        left[i] += l;
        right[i] += r;
        v.position += v.increment;
    }
}

} // namespace gk::sampler
