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

    for (auto& v : voices)
    {
        v.active = false;
        v.envelope.setSampleRate (sampleRate);
    }
}

void SamplerEngine::setConfig (std::shared_ptr<const Config> newConfig)
{
    {
        const juce::ScopedLock sl (configLock);
        std::swap (config, newConfig);

        for (auto& v : voices)
            v.active = false;
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
             && layer.sample != nullptr && layer.sample->audio.getNumSamples() > 0)
            startVoice (layer, note, velocity);
}

void SamplerEngine::noteOff (int note)
{
    for (auto& v : voices)
        if (v.active && v.note == note && ! v.layer->oneShot)
            v.envelope.noteOff();
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

void SamplerEngine::renderVoice (Voice& v, float* left, float* right, int numSamples)
{
    auto& audio = v.layer->sample->audio;
    const auto length = audio.getNumSamples();
    const auto* srcL = audio.getReadPointer (0);
    const auto* srcR = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);

    for (int i = 0; i < numSamples; ++i)
    {
        if (v.position >= length || ! v.envelope.isActive())
        {
            v.active = false;
            return;
        }

        auto gain = v.envelope.getNextSample();

        if (v.isStolen())
        {
            gain *= static_cast<float> (v.stealSamplesLeft) / static_cast<float> (stealFadeSamples);

            if (--v.stealSamplesLeft == 0)
            {
                v.active = false;
                return;
            }
        }

        left[i] += interpolate (srcL, length, v.position) * v.gainL * gain;
        right[i] += interpolate (srcR, length, v.position) * v.gainR * gain;
        v.position += v.increment;
    }
}

} // namespace gk::sampler
