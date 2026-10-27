#include "Synth.h"

namespace triad
{

namespace
{
    constexpr auto twoPi = juce::MathConstants<double>::twoPi;

    /** PolyBLEP: the correction that rounds off a step at phase 0 so it doesn't alias. */
    double blep (double t, double dt)
    {
        if (t < dt)         { t /= dt; return t + t - t * t - 1; }
        if (t > 1 - dt)     { t = (t - 1) / dt; return t * t + t + t + 1; }
        return 0;
    }

    double wrap (double phase)   { return phase - std::floor (phase); }

    double noteHz (double note)  { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

    /** Equal-power gains for a pan position (-1 to 1), 1 each in the centre. */
    std::pair<float, float> panGains (float pan)
    {
        const auto angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1) * juce::MathConstants<float>::pi / 4;
        return { std::cos (angle) * juce::MathConstants<float>::sqrt2, std::sin (angle) * juce::MathConstants<float>::sqrt2 };
    }
}

float Synth::waveform (int shape, double phase, double increment, juce::Random& rng)
{
    const auto t = wrap (phase);
    const auto dt = std::min (0.5, std::abs (increment));

    switch (shape)
    {
        case sine:      return static_cast<float> (std::sin (twoPi * t));
        case triangle:  return static_cast<float> (t < 0.5 ? 4 * t - 1 : 3 - 4 * t);
        case square:    return static_cast<float> ((t < 0.5 ? 1 : -1) + blep (t, dt) - blep (wrap (t + 0.5), dt));
        case pulse:     return static_cast<float> ((t < 0.25 ? 1 : -1) + blep (t, dt) - blep (wrap (t + 0.75), dt));
        case softSaw:   return static_cast<float> (std::tanh (1.6 * (2 * t - 1 - blep (t, dt))) / std::tanh (1.6));
        case noise:     return rng.nextFloat() * 2 - 1;
        default:        return static_cast<float> (2 * t - 1 - blep (t, dt));
    }
}

//==============================================================================
void Synth::prepare (double rate)
{
    sampleRate = rate;

    for (auto& v : voices)
    {
        v.amp.setSampleRate (rate);
        v.filterEnvelope.setSampleRate (rate);
    }

    reset();
}

void Synth::reset()
{
    for (auto& v : voices)
    {
        v.amp.reset();
        v.filterEnvelope.reset();
        v.note = -1;
        v.held = false;

        for (auto& stage : v.filters)
            for (auto& f : stage)
                f = {};
    }

    heldNotes.clear();
    sustained.clear();
    sustainPedal = false;
    bend = 0;
}

int Synth::countActiveVoices() const
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [] (auto& v) { return v.isActive(); }));
}

void Synth::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Settings& s)
{
    for (auto& v : voices)
    {
        v.amp.setParameters (s.amp);
        v.filterEnvelope.setParameters (s.filter);
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    auto events = midi.cbegin();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        for (; events != midi.cend() && (*events).samplePosition <= i; ++events)
            handle ((*events).getMessage(), s);

        const auto lfo = nextLfo (s);
        float l = 0, r = 0;

        for (auto& v : voices)
            if (v.isActive())
                renderVoice (v, s, lfo, l, r);

        if (s.lfoTarget == lfoPan)
        {
            const auto [gl, gr] = panGains (lfo * s.lfoDepth);
            l *= gl;
            r *= gr;
        }

        left[i] = l * s.gain;
        right[i] = r * s.gain;
    }

    for (; events != midi.cend(); ++events)
        handle ((*events).getMessage(), s);
}

float Synth::nextLfo (const Settings& s)
{
    const auto previous = lfoPhase;
    lfoPhase = wrap (lfoPhase + s.lfoRate / sampleRate);

    if (lfoPhase < previous)
        lfoHeld = random.nextFloat() * 2 - 1;

    switch (s.lfoShape)
    {
        case 1:  return static_cast<float> (lfoPhase < 0.5 ? 4 * lfoPhase - 1 : 3 - 4 * lfoPhase);
        case 2:  return lfoPhase < 0.5 ? 1.0f : -1.0f;
        case 3:  return static_cast<float> (2 * lfoPhase - 1);
        case 4:  return lfoHeld;
        default: return static_cast<float> (std::sin (twoPi * lfoPhase));
    }
}

//==============================================================================
void Synth::handle (const juce::MidiMessage& m, const Settings& s)
{
    if (m.isNoteOn())
    {
        noteOn (m.getNoteNumber(), m.getFloatVelocity(), s);
    }
    else if (m.isNoteOff())
    {
        if (sustainPedal)
            sustained.push_back (m.getNoteNumber());
        else
            noteOff (m.getNoteNumber(), s);
    }
    else if (m.isPitchWheel())
    {
        bend = static_cast<float> (m.getPitchWheelValue() - 8192) / 8192.0f * s.bendRange;
    }
    else if (m.isSustainPedalOn())
    {
        sustainPedal = true;
    }
    else if (m.isSustainPedalOff())
    {
        sustainPedal = false;

        for (auto note : sustained)
            noteOff (note, s);

        sustained.clear();
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& v : voices)
        {
            v.held = false;
            v.amp.noteOff();
            v.filterEnvelope.noteOff();
        }

        heldNotes.clear();
        sustained.clear();
    }
}

void Synth::noteOn (int note, float velocity, const Settings& s)
{
    std::erase (sustained, note);

    if (s.voiceMode != poly)
    {
        auto& v = voices[0];
        const bool sounding = v.isActive() && ! heldNotes.empty();
        std::erase (heldNotes, note);
        heldNotes.push_back (note);
        startVoice (v, note, velocity, s, ! (s.voiceMode == legato && sounding));
        return;
    }

    // The same note again, else a free voice, else the oldest (released ones first).
    Voice* chosen = nullptr;

    for (auto& v : voices)
        if (v.note == note && v.isActive())
            chosen = &v;

    for (auto& v : voices)
        if (chosen == nullptr && ! v.isActive())
            chosen = &v;

    if (chosen == nullptr)
    {
        chosen = &voices[0];

        for (auto& v : voices)
            if (std::tie (v.held, v.started) < std::tie (chosen->held, chosen->started))
                chosen = &v;
    }

    startVoice (*chosen, note, velocity, s, true);
}

void Synth::noteOff (int note, const Settings& s)
{
    if (s.voiceMode != poly)
    {
        auto& v = voices[0];
        std::erase (heldNotes, note);

        if (v.note != note)
            return;

        // Back to the last note still held, gliding there without a new attack.
        if (! heldNotes.empty())
        {
            startVoice (v, heldNotes.back(), v.velocity, s, false);
            return;
        }
    }

    for (auto& v : voices)
    {
        if (v.note == note && v.held)
        {
            v.held = false;
            v.amp.noteOff();
            v.filterEnvelope.noteOff();
        }
    }
}

void Synth::startVoice (Voice& v, int note, float velocity, const Settings& s, bool retrigger)
{
    const auto from = v.isActive() && s.voiceMode != poly ? v.frequency : lastFrequency;
    v.note = note;
    v.velocity = velocity;
    v.held = true;
    v.started = ++noteCounter;
    v.target = noteHz (note);
    v.frequency = s.glide > 0 && from > 0 ? from : v.target;
    lastFrequency = v.target;

    if (retrigger)
    {
        // Each oscillator starts at its phase offset; unison copies and sides spread out from it.
        for (int o = 0; o < 3; ++o)
            for (int u = 0; u < maxUnison; ++u)
                for (int side = 0; side < 2; ++side)
                    v.phases[o][u][side] = s.osc[o].phase + (u == 0 ? 0.0 : v.random.nextDouble());

        if (! v.isActive())
            for (auto& stage : v.filters)
                for (auto& f : stage)
                    f = {};

        v.amp.noteOn();
        v.filterEnvelope.noteOn();
    }
}

//==============================================================================
float Synth::Filter::process (float x, float g, float k, int type)
{
    // Cytomic's trapezoidal state-variable filter.
    const auto a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const auto v3 = x - ic2;
    const auto v1 = a1 * ic1 + a2 * v3;
    const auto v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2 * v1 - ic1;
    ic2 = 2 * v2 - ic2;

    switch (type)
    {
        case highpass:  return x - k * v1 - v2;
        case bandpass:  return v1 * k;
        default:        return v2;
    }
}

void Synth::renderVoice (Voice& v, const Settings& s, float lfo, float& left, float& right)
{
    // Glide: a fixed time to get (nearly) there, whatever the distance.
    if (std::abs (v.frequency - v.target) > 0.0)
    {
        const auto coefficient = s.glide > 0 ? std::exp (-4.6 / (s.glide * sampleRate)) : 0.0;
        v.frequency = v.target + (v.frequency - v.target) * coefficient;

        if (std::abs (v.frequency - v.target) < 1.0e-3)
            v.frequency = v.target;
    }

    auto semitones = static_cast<double> (bend);

    if (s.lfoTarget == lfoPitch)
        semitones += lfo * s.lfoDepth * s.lfoDepth * 12;

    const auto base = v.frequency * std::pow (2.0, semitones / 12.0);
    const auto unison = juce::jlimit (1, maxUnison, s.unisonVoices);
    const auto unisonGain = 1.0f / std::sqrt (static_cast<float> (unison));

    // From the third oscillator up, as each may modulate the one before it.
    for (int o = 2; o >= 0; --o)
    {
        const auto& osc = s.osc[o];
        const auto* modulator = o < 2 ? &s.osc[o + 1] : nullptr;
        const bool stereo = osc.detune > 0 || (unison > 1 && s.unisonSpread > 0);
        const auto ratio = std::pow (2.0, (osc.coarse + osc.fine / 100.0) / 12.0);

        for (int u = 0; u < unison; ++u)
        {
            const auto spread = unison > 1 ? 2.0 * u / (unison - 1) - 1 : 0.0;

            for (int side = 0; side < (stereo ? 2 : 1); ++side)
            {
                const auto cents = spread * s.unisonDetune / 2 + (side == 0 ? -0.5 : 0.5) * osc.detune;
                const auto increment = base * ratio * std::pow (2.0, cents / 1200.0) / sampleRate;
                auto& phase = v.phases[o][u][side];
                auto offset = 0.0;

                if (modulator != nullptr && modulator->mod == fm)
                    offset = 1.5 * modulator->level * v.outputs[o + 1][u][side];

                auto out = waveform (osc.shape, phase + offset, increment, v.random);

                if (modulator != nullptr && modulator->mod == ring)
                    out *= 1 - modulator->level + modulator->level * v.outputs[o + 1][u][side];

                v.outputs[o][u][side] = out;
                phase = wrap (phase + increment);
            }

            if (! stereo)
                v.outputs[o][u][1] = v.outputs[o][u][0];
        }
    }

    // Mix the oscillators that are heard, each unison copy at its place across the field.
    float l = 0, r = 0;

    for (int o = 0; o < 3; ++o)
    {
        const auto& osc = s.osc[o];

        if (o > 0 && osc.mod != mix)
            continue;

        for (int u = 0; u < unison; ++u)
        {
            const auto spread = unison > 1 ? (2.0f * u / (unison - 1) - 1) * s.unisonSpread : 0.0f;
            const auto [gl, gr] = panGains (osc.pan + spread);
            l += v.outputs[o][u][0] * osc.level * gl * unisonGain;
            r += v.outputs[o][u][1] * osc.level * gr * unisonGain;
        }
    }

    // The filter, its cutoff moved by its envelope (up to six octaves), the key and the LFO.
    const auto filterEnvelope = v.filterEnvelope.getNextSample();

    if (s.filterType != filterOff)
    {
        auto octaves = filterEnvelope * s.filterEnv * 6 + s.keyTrack * (v.note - 60) / 12.0f;

        if (s.lfoTarget == lfoCutoff)
            octaves += lfo * s.lfoDepth * 4;

        const auto hz = juce::jlimit (20.0f, static_cast<float> (sampleRate * 0.45), s.cutoff * std::exp2 (octaves));
        const auto g = static_cast<float> (std::tan (juce::MathConstants<double>::pi * hz / sampleRate));
        const auto k = 2.0f - 1.96f * s.resonance;
        const auto type = s.filterType == lowpass24 || s.filterType == lowpass12 ? lowpass24 : s.filterType;
        l = v.filters[0][0].process (l, g, k, type);
        r = v.filters[0][1].process (r, g, k, type);

        if (s.filterType == lowpass24)
        {
            l = v.filters[1][0].process (l, g, k, type);
            r = v.filters[1][1].process (r, g, k, type);
        }
    }

    auto gain = v.amp.getNextSample() * ((1 - s.velocity) + s.velocity * v.velocity);

    if (s.lfoTarget == lfoVolume)
        gain *= 1 - s.lfoDepth * (0.5f - 0.5f * lfo);

    left += l * gain;
    right += r * gain;

    if (! v.amp.isActive())
        v.note = -1;
}

} // namespace triad
