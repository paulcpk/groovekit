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

float Grains::window (Shape shape, float x)
{
    x = juce::jlimit (0.0f, 1.0f, x);

    switch (shape)
    {
        case Shape::flat:   // full in the middle, tapered over a fifth at each end
        {
            const auto edge = std::min (x, 1.0f - x) / 0.2f;
            return edge >= 1 ? 1.0f : 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * edge);
        }

        case Shape::pluck:  // struck, then dying away
            return std::min (1.0f, x / 0.02f) * std::exp (-4.5f * x) * std::min (1.0f, (1.0f - x) / 0.05f);

        case Shape::smooth:
            break;
    }

    return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * x);
}

SamplerEngine::SamplerEngine()
    : voices (maxVoices)
{
}

void SamplerEngine::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    stealFadeSamples = std::max (1, juce::roundToInt (stealFadeSeconds * sampleRate));
    edgeFadeSamples = std::max (1, juce::roundToInt (0.001 * sampleRate));

    for (auto& v : voices)
    {
        v.active = false;

        for (auto* e : { &v.envelope, &v.filterEnvelope, &v.resonanceEnvelope, &v.panEnvelope, &v.pitchEnvelope })
            e->setSampleRate (sampleRate);
    }
}

void SamplerEngine::setEnvelopes (Voice& v)
{
    const auto& m = config->modulation;
    v.envelope.setShape (v.layer->envelope);
    v.filterEnvelope.setShape (config->filter.envelope);
    v.resonanceEnvelope.setShape (m.resonance.envelope);
    v.panEnvelope.setShape (m.pan.envelope);
    v.pitchEnvelope.setShape (m.pitch.envelope);
}

void SamplerEngine::setConfig (std::shared_ptr<const Config> newConfig)
{
    handoff.publish (std::move (newConfig));
}

void SamplerEngine::adopt (const Config& next)
{
    const auto* previous = config;
    config = &next;
    filterCutoff = config->filter.cutoff;
    filterResonance = config->filter.resonance;
    filterEnvelopeAmount = config->filter.envelopeAmount;

    const auto fromPrevious = [&] (const Layer* l)
    {
        return previous != nullptr && l >= previous->layers.data() && l < previous->layers.data() + previous->layers.size();
    };

    const auto samePart = [&] (const Voice& v)
    {
        if (v.layerIndex >= config->layers.size())
            return false;

        auto& a = *v.layer;
        auto& b = config->layers[v.layerIndex];
        return a.sample == b.sample && a.start == b.start && a.endFrame() == b.endFrame() && a.reverse == b.reverse && a.loop == b.loop;
    };

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        // Still fading out of the configuration retired before: it goes with it.
        if (! fromPrevious (v.layer))
        {
            v.active = false;
            continue;
        }

        if (v.isStolen() || ! samePart (v))
        {
            // Another sample or part: fade out what was playing rather than cut it off.
            if (! v.isStolen())
                v.stealSamplesLeft = stealFadeSamples;

            continue;
        }

        v.layer = &config->layers[v.layerIndex];
        setEnvelopes (v);
        v.samplesToControls = 0;
    }

    // No voice reads the configuration retired before this one any more.
    handoff.release (retired);
    retired = previous;
}

void SamplerEngine::allNotesOff()
{
    for (auto& v : voices)
        v.active = false;
}

void SamplerEngine::setFilterControls (float cutoff, float resonance, float envelopeAmount)
{
    filterCutoff = cutoff;
    filterResonance = resonance;
    filterEnvelopeAmount = envelopeAmount;
}

int SamplerEngine::getNumActiveVoices() const
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.active; }));
}

void SamplerEngine::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                             const std::vector<MidiEvent>& events)
{
    if (auto* next = handoff.take())
        adopt (*next);

    if (config == nullptr)
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
        if (v.active && v.note == note && v.layer->releasesOnNoteOff())
            for (auto* e : { &v.envelope, &v.filterEnvelope, &v.resonanceEnvelope, &v.panEnvelope, &v.pitchEnvelope })
                e->noteOff();
    }
}

void SamplerEngine::startVoice (const Layer& layer, int note, int velocity)
{
    // Whatever the voice played before, its filter is set up anew for this note.
    const auto sounding = std::count_if (voices.begin(), voices.end(),
                                         [] (const Voice& v) { return v.active && ! v.isStolen(); });

    if (sounding >= config->polyphony)
        stealOldestVoice();

    auto* v = findFreeVoice();

    const auto semitones = (layer.pitchTracking ? note - layer.rootKey : 0) + layer.tuneSemitones;

    v->layer = &layer;
    v->layerIndex = static_cast<size_t> (&layer - config->layers.data());
    v->note = note;
    v->age = nextAge++;
    v->position = 0;
    v->direction = 1;
    v->baseIncrement = std::pow (2.0, semitones / 12.0) * layer.sample->sampleRate / sampleRate;
    v->increment = v->baseIncrement;
    v->velocityGain = static_cast<float> (velocity) / 127.0f;
    v->stealSamplesLeft = 0;
    v->since = 0;
    setEnvelopes (*v);

    for (auto* e : { &v->envelope, &v->filterEnvelope, &v->resonanceEnvelope, &v->panEnvelope, &v->pitchEnvelope })
    {
        e->reset();
        e->noteOn();
    }

    std::fill (std::begin (v->lfoPhase), std::end (v->lfoPhase), 0.0);
    std::fill (&v->filterState[0][0], &v->filterState[0][0] + 8, 0.0f);
    v->samplesToControls = 0;
    v->controlsSet = false;

    for (auto& g : v->grains)
        g.active = false;

    v->activeGrains = 0;
    v->untilGrain = 0;
    v->seed = static_cast<uint32_t> (v->age * 2654435761u + static_cast<uint64_t> (note) * 40503u) | 1u;

    // Where the played part cuts into the file (rather than starting or ending with it), a 1 ms
    // fade keeps the cut from clicking.
    const auto fileLength = layer.sample->audio.getNumSamples();
    const bool cutAtStart = layer.reverse ? layer.endFrame() < fileLength : layer.start > 0;
    const bool cutAtEnd = layer.length < 1 || (layer.reverse ? layer.start > 0 : layer.endFrame() < fileLength);
    v->edgeFadeIn = cutAtStart ? edgeFadeSamples : 0;
    v->edgeFadeOut = cutAtEnd ? edgeFadeSamples : 0;
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

    // LFOs that every note shares keep running whether notes play or not.
    const auto& m = config->modulation;
    const LfoSettings* lfos[numLfos] { &m.volume, &m.cutoff, &m.resonance.lfo, &m.pan.lfo, &m.pitch.lfo };

    for (int i = 0; i < numLfos; ++i)
        sharedPhase[i] = std::fmod (sharedPhase[i] + lfos[i]->cyclesPerSecond (tempo) * numSamples / sampleRate, 1.0e6);
}

void SamplerEngine::updateControls (Voice& v)
{
    const auto& m = config->modulation;
    const auto& layer = *v.layer;
    const LfoSettings* lfos[numLfos] { &m.volume, &m.cutoff, &m.resonance.lfo, &m.pan.lfo, &m.pitch.lfo };
    float lfo[numLfos] {};

    for (int i = 0; i < numLfos; ++i)
    {
        const auto& settings = *lfos[i];
        const auto step = settings.cyclesPerSecond (tempo) * controlInterval / sampleRate;

        if (settings.isOn())
            lfo[i] = settings.amount * settings.fadeIn (v.since) * settings.wave (settings.retrigger ? v.lfoPhase[i] : sharedPhase[i]);

        v.lfoPhase[i] += step;
    }

    const auto filterLevel = v.filterEnvelope.advance (controlInterval);
    const auto resonanceLevel = v.resonanceEnvelope.advance (controlInterval);
    const auto panLevel = v.panEnvelope.advance (controlInterval);
    const auto pitchLevel = v.pitchEnvelope.advance (controlInterval);

    if (config->filter.type != Filter::Type::off)
    {
        const auto octaves = filterEnvelopeAmount * filterLevel + lfo[cutoffLfo] * Modulation::cutoffOctaves;
        const auto resonance = juce::jlimit (0.0f, 1.0f, filterResonance + m.resonance.amount * resonanceLevel + lfo[resonanceLfo]);
        const auto cutoff = octaves == 0.0f ? filterCutoff : filterCutoff * std::exp2 (octaves);

        // Coefficients only when something moved: without a filter envelope or LFO, never after the first time.
        if (cutoff != v.filterCutoffSet || resonance != v.filterResonanceSet || config->filter.type != v.filterTypeSet)
        {
            v.filter.setup (config->filter.type, cutoff, resonance, sampleRate);
            v.filterCutoffSet = cutoff;
            v.filterResonanceSet = resonance;
            v.filterTypeSet = config->filter.type;
        }
    }

    const auto semitones = (m.pitch.amount * pitchLevel + lfo[pitchLfo]) * Modulation::pitchSemitones;
    v.increment = semitones == 0.0f ? v.baseIncrement : v.baseIncrement * std::exp2 (semitones / 12.0);

    // Tremolo dips from full level by up to the amount, at the wave's low point (a negative
    // amount turns the wave over).
    const auto tremolo = 1.0f - 0.5f * (std::abs (m.volume.amount) * m.volume.fadeIn (v.since) - lfo[volumeLfo]);

    const auto pan = juce::jlimit (-1.0f, 1.0f, layer.pan + m.pan.amount * panLevel + lfo[panLfo]);
    const auto gain = layer.gain * v.velocityGain * tremolo;
    const auto targetL = gain * std::min (1.0f, 1.0f - pan), targetR = gain * std::min (1.0f, 1.0f + pan);

    if (! v.controlsSet)
    {
        v.gainL = targetL;
        v.gainR = targetR;
        v.controlsSet = true;
    }

    v.gainStepL = (targetL - v.gainL) / controlInterval;
    v.gainStepR = (targetR - v.gainR) / controlInterval;
}

void SamplerEngine::startGrain (Voice& v, double length)
{
    auto* g = std::find_if (std::begin (v.grains), std::end (v.grains), [] (auto& x) { return ! x.active; });

    if (g == std::end (v.grains))
        return;

    const auto& grains = v.layer->grains;
    const bool looping = v.layer->loop != Layer::Loop::off;

    // From around the playhead, scattered by the spray.
    auto read = v.position + grains.spray * v.layer->sample->sampleRate * (2 * v.random() - 1);

    if (! looping)
        read = juce::jlimit (0.0, std::max (0.0, length - 1), read);

    g->read = read;
    g->step = std::exp2 (grains.detune / 1200.0f * (2 * v.random() - 1));
    g->length = std::max (8, static_cast<int> (grains.size * sampleRate));
    g->age = 0;

    const auto pan = grains.width * (2 * v.random() - 1);
    g->left = std::min (1.0f, 1.0f - pan);
    g->right = std::min (1.0f, 1.0f + pan);
    g->active = true;
    ++v.activeGrains;
}

void SamplerEngine::renderVoice (Voice& v, float* left, float* right, int numSamples)
{
    const auto& layer = *v.layer;
    auto& audio = layer.sample->audio;
    const auto fileLength = audio.getNumSamples();
    const auto end = layer.endFrame();
    const auto length = layer.playedFrames();
    const auto* srcL = audio.getReadPointer (layer.swapStereo && audio.getNumChannels() > 1 ? 1 : 0);
    const auto* srcR = audio.getReadPointer (audio.getNumChannels() > 1 && ! layer.swapStereo ? 1 : 0);
    const auto filterType = config->filter.type;
    const bool looping = layer.loop != Layer::Loop::off;
    const auto secondsPerSample = 1.0 / sampleRate;
    const auto& grains = layer.grains;
    const auto crossfade = std::min (static_cast<double> (layer.crossfade), length / 2);

    // Granular: the playhead moves at the grains' speed, and the grains read at the note's pitch.
    const auto playIncrement = grains.on ? grains.speed * layer.sample->sampleRate / sampleRate : 0.0;

    // A point in the part, in the order it plays, to a frame of the file: round again if it
    // loops; outside it, nothing.
    const auto frameAt = [&] (double position) -> std::optional<double>
    {
        if (layer.loop == Layer::Loop::forward)
            position = std::fmod (std::fmod (position, length) + length, length);
        else if (layer.loop == Layer::Loop::pingPong && length > 1)
        {
            const auto period = 2 * (length - 1);
            position = std::fmod (std::fmod (position, period) + period, period);
            position = position > length - 1 ? period - position : position;
        }
        else if (position < 0 || position >= length)
            return {};

        return layer.reverse ? end - 1 - position : layer.start + position;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        const bool finished = ! looping && v.position >= length && (! grains.on || v.activeGrains == 0);

        if (finished || ! v.envelope.isActive())
        {
            v.active = false;
            return;
        }

        if (--v.samplesToControls <= 0)
        {
            updateControls (v);
            v.samplesToControls = controlInterval;
        }

        auto gain = v.envelope.next();

        if (v.isStolen())
        {
            gain *= static_cast<float> (v.stealSamplesLeft) / static_cast<float> (stealFadeSamples);

            if (--v.stealSamplesLeft == 0)
            {
                v.active = false;
                return;
            }
        }

        // Fades over the part as it plays: the 1 ms ones where it cuts into the file, and the layer's own.
        const auto position = std::min (v.position, length);
        const auto remaining = length - position;

        if (v.edgeFadeIn > 0 && position < edgeFadeSamples)
            gain *= static_cast<float> (position / edgeFadeSamples);

        if (layer.fadeIn > 0 && position < layer.fadeIn)
            gain *= static_cast<float> (position / layer.fadeIn);

        if (! looping)
        {
            if (v.edgeFadeOut > 0 && remaining < edgeFadeSamples)
                gain *= static_cast<float> (remaining / edgeFadeSamples);

            if (layer.fadeOut > 0 && remaining < layer.fadeOut)
                gain *= static_cast<float> (remaining / layer.fadeOut);
        }

        float l = 0, r = 0;

        if (grains.on)
        {
            // A new grain now and then, a little early or late, until the playhead runs out.
            if ((v.untilGrain -= 1) <= 0 && (looping || v.position < length))
            {
                startGrain (v, length);
                v.untilGrain += sampleRate / grains.density * (0.85 + 0.3 * v.random());
            }

            // Where grains overlap, their average; alone, each with its own rise and fall, so few
            // short grains leave space between them.
            float weight = 0;

            for (auto& g : v.grains)
            {
                if (! g.active)
                    continue;

                const auto w = Grains::window (grains.shape, static_cast<float> (g.age) / static_cast<float> (g.length));

                if (const auto at = frameAt (g.read))
                {
                    // Near the ends of a part that doesn't loop, a 1 ms fade keeps grains from clicking.
                    const auto edge = looping ? 1.0f : static_cast<float> (std::min (1.0, std::min (g.read, length - g.read) / edgeFadeSamples));
                    l += interpolate (srcL, fileLength, *at) * w * edge * g.left;
                    r += interpolate (srcR, fileLength, *at) * w * edge * g.right;
                }

                weight += w;
                g.read += v.increment * g.step;

                if (++g.age >= g.length)
                {
                    g.active = false;
                    --v.activeGrains;
                }
            }

            const auto norm = 1.0f / std::max (1.0f, weight);
            l *= norm * v.gainL * gain;
            r *= norm * v.gainR * gain;
        }
        else
        {
            const auto at = layer.reverse ? end - 1 - position : layer.start + position;
            l = interpolate (srcL, fileLength, at);
            r = interpolate (srcR, fileLength, at);

            // Coming round a crossfaded loop: into the start of the loop, which then goes on from
            // where the crossfade ended.
            if (layer.loop == Layer::Loop::forward && crossfade > 0 && position >= length - crossfade)
            {
                const auto into = position - (length - crossfade);
                const auto w = static_cast<float> (into / crossfade) * juce::MathConstants<float>::halfPi;
                const auto from = layer.reverse ? end - 1 - into : layer.start + into;
                l = l * std::cos (w) + interpolate (srcL, fileLength, from) * std::sin (w);
                r = r * std::cos (w) + interpolate (srcR, fileLength, from) * std::sin (w);
            }

            l *= v.gainL * gain;
            r *= v.gainR * gain;
        }

        v.gainL += v.gainStepL;
        v.gainR += v.gainStepR;

        if (filterType != Filter::Type::off)
        {
            l = v.filter.process (l, v.filterState[0], filterType);
            r = v.filter.process (r, v.filterState[1], filterType);
        }

        left[i] += l;
        right[i] += r;
        v.since += secondsPerSample;

        // On through the part, round again if it loops (ping-pong turning at either end).
        v.position += (grains.on ? playIncrement : v.increment) * v.direction;

        if (layer.loop == Layer::Loop::forward && v.position >= length)
        {
            // After a crossfade, the loop goes on from where it faded in.
            v.position = crossfade + std::fmod (v.position - length, std::max (1.0, length - crossfade));
        }
        else if (layer.loop == Layer::Loop::pingPong && (v.position >= length - 1 || v.position < 0))
        {
            v.direction = -v.direction;
            v.position = v.position < 0 ? -v.position : 2 * (length - 1) - v.position;
            v.position = juce::jlimit (0.0, std::max (0.0, length - 1), v.position);
        }
    }
}

} // namespace gk::sampler
