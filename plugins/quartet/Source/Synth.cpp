#include "Synth.h"

namespace quartet
{

using namespace gkp::dsp;

namespace
{
    /** How far a modulator at full level pushes a carrier's phase, in cycles: about the reach
        of the classic machines' top index. */
    constexpr float fmIndex = 1.3f;
    constexpr float feedbackIndex = 0.6f;
}

float Synth::opWave (int wave, double phase)
{
    const auto t = wrap (phase);
    const auto s = static_cast<float> (std::sin (twoPi * t));

    switch (wave)
    {
        case half:      return t < 0.5 ? s : 0.0f;
        case dbl:       return std::abs (s) * 2 - 1;
        case soft:      return s * s * s;
        case bright:    return (s + 0.5f * static_cast<float> (std::sin (2 * twoPi * t)) + 0.33f * static_cast<float> (std::sin (3 * twoPi * t))) / 1.5f;
        case hollow:    return (s + 0.33f * static_cast<float> (std::sin (3 * twoPi * t)) + 0.2f * static_cast<float> (std::sin (5 * twoPi * t))) / 1.2f;
        case fold:      return static_cast<float> (std::sin (1.5 * juce::MathConstants<double>::pi * s));
        case grit:      return std::tanh (3 * s) / std::tanh (3.0f);
        default:        return s;
    }
}

//==============================================================================
void Synth::prepare (double rate)
{
    sampleRate = rate;

    for (auto& v : voices)
    {
        v.amp.prepare (rate);
        v.filterEnvelope.prepare (rate);

        for (auto& line : v.lines)
            for (auto& e : line.envelopes)
                e.prepare (rate);
    }

    for (int c = 0; c < 2; ++c)
    {
        chorusLines[c].prepare (static_cast<int> (rate * 0.03));
        toneLow[c].set (rate, 400);
        toneHigh[c].set (rate, 2500);
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
                f.reset();

        for (auto& line : v.lines)
            for (auto& e : line.envelopes)
                e.reset();
    }

    for (auto& c : chorusLines)
        c.clear();

    heldNotes.clear();
    sustained.clear();
    sustainPedal = false;
    bend = 0;
    lfo.reset();
}

int Synth::countActiveVoices() const
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [] (auto& v) { return v.isActive(); }));
}

void Synth::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Settings& s)
{
    for (auto& v : voices)
    {
        v.amp.set (s.ampAttack, s.ampDecay, s.ampSustain, s.ampRelease);
        v.filterEnvelope.set (s.filterAttack, s.filterDecay, s.filterSustain, s.filterRelease);

        for (auto& line : v.lines)
            for (size_t o = 0; o < 4; ++o)
                line.envelopes[o].set (s.ops[o].attack, s.ops[o].decay, s.ops[o].sustain, s.ops[o].release, 0.7f);
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    auto events = midi.cbegin();
    const auto driveGain = 1 + s.drive * 8;
    const auto driveMakeup = 1 / std::tanh (std::min (3.0f, driveGain * 0.6f)) * 0.6f;
    const auto chorusRate = s.chorusMode == 1 ? 0.513 : s.chorusMode == 2 ? 0.863 : 0.6;
    const auto chorusDepthMs = (s.chorusMode == 2 ? 2.2 : 1.6) * s.chorusDepth;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        for (; events != midi.cend() && (*events).samplePosition <= i; ++events)
            handle ((*events).getMessage(), s);

        const auto lfoValue = lfo.next (s.lfoRate / sampleRate, s.lfoShape);
        float l = 0, r = 0;

        for (auto& v : voices)
            if (v.isActive())
                renderVoice (v, s, lfoValue, l, r);

        float out[2] { l, r };

        for (int c = 0; c < 2; ++c)
        {
            auto x = out[c];

            if (s.drive > 0)
                x = std::tanh (x * driveGain) * driveMakeup;

            // Tone: a tilt, darker one way and brighter the other.
            if (std::abs (s.tone) > 0.001f)
            {
                const auto low = toneLow[c].lowpass (x);
                const auto high = toneHigh[c].highpass (x);
                x += s.tone * (high - low) * 0.7f;
            }

            // The chorus: the signal and a copy swaying a couple of milliseconds, as a machine
            // of the time did with a bucket brigade; I+II has both rates, one per side.
            if (s.chorusMode > 0)
            {
                chorusLines[c].push (x);
                const auto ratePerSide = s.chorusMode == 3 ? (c == 0 ? 0.513 : 0.863) : chorusRate;
                chorusPhase[c] = wrap (chorusPhase[c] + ratePerSide / sampleRate);
                const auto tri = chorusPhase[c] < 0.5 ? 4 * chorusPhase[c] - 1 : 3 - 4 * chorusPhase[c];
                const auto delayMs = 3.5 + chorusDepthMs * tri;
                const auto wet = chorusLines[c].read (static_cast<float> (delayMs * 0.001 * sampleRate));
                x = 0.5f * x + 0.5f * wet;
            }

            out[c] = dc[c].process (x) * s.gain;
        }

        left[i] = out[0];
        right[i] = out[1];
    }

    for (; events != midi.cend(); ++events)
        handle ((*events).getMessage(), s);
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

            for (auto& line : v.lines)
                for (auto& e : line.envelopes)
                    e.noteOff();
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

            for (auto& line : v.lines)
                for (auto& e : line.envelopes)
                    e.noteOff();
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

    // The chord memory's notes: spread sends the top ones up an octave and the root down;
    // detune and pan set each a little apart.
    auto intervals = chordIntervals (s.chord);
    const auto n = static_cast<int> (intervals.size());
    v.lineCount = std::min (maxNotes, n);

    for (int k = 0; k < v.lineCount; ++k)
    {
        auto& line = v.lines[static_cast<size_t> (k)];
        line.semitones = intervals[static_cast<size_t> (k)];

        if (n > 1)
        {
            if (k == n - 1 && s.chordSpread >= 0.34f) line.semitones += 12;
            if (k == n - 2 && n > 2 && s.chordSpread >= 0.67f) line.semitones += 12;
            if (k == 0 && s.chordSpread >= 0.99f) line.semitones -= 12;
        }

        const auto place = n > 1 ? (2.0f * k / (n - 1) - 1) : 0.0f;
        line.cents = place * s.chordDetune;
        line.pan = place * 0.35f;
    }

    if (retrigger)
    {
        for (auto& line : v.lines)
        {
            for (auto& p : line.phases)
                p = 0;

            line.feedbackOut = 0;

            for (auto& u : line.vaPhases)
                for (auto& p : u)
                    p = v.random.nextDouble();

            line.subPhase = 0;

            for (auto& e : line.envelopes)
                e.noteOn();
        }

        if (! v.isActive())
            for (auto& stage : v.filters)
                for (auto& f : stage)
                    f.reset();

        v.amp.noteOn();
        v.filterEnvelope.noteOn();
    }
}

//==============================================================================
void Synth::renderLine (Voice& v, Line& line, const Settings& s, double baseHz, float lfoValue, float& left, float& right)
{
    const auto hz = baseHz * std::pow (2.0, (line.semitones + line.cents / 100.0) / 12.0);
    const auto& algorithm = algorithms[static_cast<size_t> (juce::jlimit (0, 7, s.algorithm))];
    const auto fmDepth = s.lfoTarget == lfoFm ? 1 + lfoValue * s.lfoDepth : 1.0f;
    const auto keyOffset = (v.note - 60) / 24.0f;

    // The operators, modulators first: each adds to the phase of the ones it feeds.
    for (int o = 3; o >= 0; --o)
    {
        const auto& op = s.ops[static_cast<size_t> (o)];
        const auto increment = hz * (op.ratio * std::pow (2.0, op.fine / 1200.0)) / sampleRate;
        auto offset = 0.0;

        for (int m = 0; m < 4; ++m)
            if (algorithm.modulators[static_cast<size_t> (o)] & (1 << m))
                offset += line.outputs[static_cast<size_t> (m)] * fmIndex * fmDepth;

        if (o == 3)
            offset += line.feedbackOut * s.feedback * feedbackIndex;

        auto& phase = line.phases[static_cast<size_t> (o)];
        auto level = op.level * ((1 - op.velocity) + op.velocity * v.velocity);
        level *= std::exp2 (op.keyScale * keyOffset);
        const auto out = opWave (op.wave, phase + offset) * line.envelopes[static_cast<size_t> (o)].next() * level;
        line.outputs[static_cast<size_t> (o)] = out;

        if (o == 3)
            line.feedbackOut = 0.5f * (line.feedbackOut + out);

        phase = wrap (phase + increment);
    }

    float fm = 0;
    int carriers = 0;

    for (int o = 0; o < 4; ++o)
        if (algorithm.carriers & (1 << o))
        {
            fm += line.outputs[static_cast<size_t> (o)];
            ++carriers;
        }

    fm /= std::sqrt (static_cast<float> (std::max (1, carriers)));

    // The analogue layer: the shape in unison copies across the field, and a sub an octave down.
    float vaL = 0, vaR = 0;

    if (s.vaLevel > 0)
    {
        const auto vaHz = hz * std::pow (2.0, (s.vaCoarse + s.vaFine / 100.0) / 12.0);
        const auto unison = juce::jlimit (1, maxUnison, s.vaUnison);
        const auto width = juce::jlimit (0.05f, 0.95f, s.vaWidth + (s.lfoTarget == lfoWidth ? lfoValue * s.lfoDepth * 0.4f : 0.0f));
        const int shapeIndex = s.vaShape == vaSaw ? 2 : s.vaShape == vaSquare ? 3 : s.vaShape == vaPulse ? 4 : 1;

        for (int u = 0; u < unison; ++u)
        {
            const auto spread = unison > 1 ? 2.0 * u / (unison - 1) - 1 : 0.0;

            for (int side = 0; side < 2; ++side)
            {
                const auto cents = spread * s.vaDetune / 2 + (side == 0 ? -0.5 : 0.5) * (unison > 1 ? s.vaDetune * 0.3 : 0.0);
                const auto increment = vaHz * std::pow (2.0, cents / 1200.0) / sampleRate;
                auto& phase = line.vaPhases[static_cast<size_t> (u)][static_cast<size_t> (side)];
                const auto out = waveform (shapeIndex, phase, increment, width) / std::sqrt (static_cast<float> (unison));
                (side == 0 ? vaL : vaR) += out;
                phase = wrap (phase + increment);
            }
        }

        if (s.vaSub > 0)
        {
            const auto increment = vaHz * 0.5 / sampleRate;
            const auto sub = waveform (3, line.subPhase, increment) * s.vaSub;
            line.subPhase = wrap (line.subPhase + increment);
            vaL += sub;
            vaR += sub;
        }
    }

    const auto [gl, gr] = panGains (line.pan);
    left += (fm * s.fmLevel + vaL * s.vaLevel) * gl;
    right += (fm * s.fmLevel + vaR * s.vaLevel) * gr;
}

void Synth::renderVoice (Voice& v, const Settings& s, float lfoValue, float& left, float& right)
{
    if (std::abs (v.frequency - v.target) > 0.0)
    {
        const auto coefficient = s.glide > 0 ? std::exp (-4.6 / (s.glide * sampleRate)) : 0.0;
        v.frequency = v.target + (v.frequency - v.target) * coefficient;

        if (std::abs (v.frequency - v.target) < 1.0e-3)
            v.frequency = v.target;
    }

    auto semitones = static_cast<double> (bend);

    if (s.lfoTarget == lfoVibrato)
        semitones += lfoValue * s.lfoDepth * s.lfoDepth * 2;

    const auto base = v.frequency * std::pow (2.0, semitones / 12.0);
    float l = 0, r = 0;

    for (int k = 0; k < v.lineCount; ++k)
        renderLine (v, v.lines[static_cast<size_t> (k)], s, base, lfoValue, l, r);

    const auto scale = 1 / std::sqrt (static_cast<float> (v.lineCount));
    l *= scale;
    r *= scale;

    const auto filterEnvelope = v.filterEnvelope.next();

    if (s.filterType != filterOff)
    {
        auto octaves = filterEnvelope * s.filterEnv * 6 + s.keyTrack * (v.note - 60) / 12.0f;

        if (s.lfoTarget == lfoCutoff)
            octaves += lfoValue * s.lfoDepth * 4;

        const auto hz = juce::jlimit (20.0f, static_cast<float> (sampleRate * 0.45), s.cutoff * std::exp2 (octaves));
        const int type = s.filterType == highpass ? 1 : s.filterType == bandpass ? 2 : 0;

        for (int side = 0; side < 2; ++side)
        {
            auto& x = side == 0 ? l : r;
            v.filters[0][static_cast<size_t> (side)].set (sampleRate, hz, s.resonance);
            x = v.filters[0][static_cast<size_t> (side)].process (x, type);

            if (s.filterType == lowpass24)
            {
                v.filters[1][static_cast<size_t> (side)].set (sampleRate, hz, s.resonance);
                x = v.filters[1][static_cast<size_t> (side)].process (x, type);
            }
        }
    }

    auto gain = v.amp.next() * ((1 - s.velocity) + s.velocity * v.velocity);

    if (s.lfoTarget == lfoTremolo)
        gain *= 1 - s.lfoDepth * (0.5f - 0.5f * lfoValue);

    left += l * gain;
    right += r * gain;

    if (! v.amp.isActive())
        v.note = -1;
}

} // namespace quartet
