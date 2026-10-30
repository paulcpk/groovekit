#include "Synth.h"

namespace sextet
{

using namespace gkp::dsp;

namespace
{
    constexpr float fmIndex = 1.5f;

    /** Phase distortion: warp bends the first half of the cycle shorter, the second longer. */
    double warped (double t, float warp)
    {
        if (warp <= 0.001f)
            return t;

        const auto w = 0.5 - 0.45 * warp;
        return t < w ? t / (2 * w) : 0.5 + (t - w) / (2 * (1 - w));
    }

    float shaped (float x, int type, float amount)
    {
        const auto gain = 1 + amount * 9;

        switch (type)
        {
            case 1:  return juce::jlimit (-1.0f, 1.0f, x * gain) * 0.8f;
            case 2:  return std::sin (x * gain * 1.2f) * 0.8f;
            default: return std::tanh (x * gain) / std::tanh (std::min (3.0f, gain)) * 0.9f;
        }
    }
}

//==============================================================================
void Synth::prepare (double rate)
{
    sampleRate = rate;

    for (auto& v : voices)
    {
        v.amp.prepare (rate);
        v.env2.prepare (rate);
        v.env3.prepare (rate);
    }

    const int combLengths[4] { 1116, 1188, 1277, 1356 };
    const int allpassLengths[2] { 556, 441 };

    for (int c = 0; c < 2; ++c)
    {
        chorusLines[c].prepare (static_cast<int> (rate * 0.04));
        delayLines[c].prepare (static_cast<int> (rate * 4.1));
        delayTone[c].set (rate, 5000);

        for (int i = 0; i < 4; ++i)
        {
            // The right side's lines a little longer, for width.
            combs[static_cast<size_t> (c)][static_cast<size_t> (i)].length = static_cast<int> ((combLengths[i] + c * 23) * rate / 44100 * 1.6);
            combs[static_cast<size_t> (c)][static_cast<size_t> (i)].line.prepare (combs[static_cast<size_t> (c)][static_cast<size_t> (i)].length + 2);
        }

        for (int i = 0; i < 2; ++i)
            allpasses[static_cast<size_t> (c)][static_cast<size_t> (i)].prepare (static_cast<int> ((allpassLengths[i] + c * 11) * rate / 44100), 0.5f);
    }

    reset();
}

void Synth::reset()
{
    for (auto& v : voices)
    {
        v.amp.reset();
        v.env2.reset();
        v.env3.reset();
        v.note = -1;
        v.held = false;

        for (auto& f : v.filters)
            for (auto& stage : f)
                for (auto& side : stage)
                    side.reset();
    }

    for (auto& l : chorusLines) l.clear();
    for (auto& l : delayLines) l.clear();
    for (auto& side : combs) for (auto& c : side) { c.line.clear(); c.store = 0; }
    for (auto& side : allpasses) for (auto& a : side) a.line.clear();
    for (auto& l : lfos) l.reset();

    heldNotes.clear();
    sustained.clear();
    sustainPedal = false;
    bend = 0;
}

int Synth::countActiveVoices() const
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [] (auto& v) { return v.isActive(); }));
}

//==============================================================================
void Synth::render (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Settings& settings)
{
    auto s = settings;

    for (auto& v : voices)
    {
        v.amp.set (s.amp.attack, s.amp.decay, s.amp.sustain, s.amp.release, s.amp.curve);
        v.env2.set (s.env2.attack, s.env2.decay, s.env2.sustain, s.env2.release, s.env2.curve);
        v.env3.set (s.env3.attack, s.env3.decay, s.env3.sustain, s.env3.release, s.env3.curve);
    }

    for (int c = 0; c < 2; ++c)
        delayTone[c].set (sampleRate, s.delayTone);

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    auto events = midi.cbegin();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        for (; events != midi.cend() && (*events).samplePosition <= i; ++events)
            handle ((*events).getMessage(), s);

        // The LFOs run free (or on the tempo), their rates moved by the matrix's global sources.
        for (size_t k = 0; k < numLfos; ++k)
        {
            const auto& l = s.lfos[k];
            auto hz = l.rate;

            if (l.sync > 0)
                hz = static_cast<float> (s.tempo / 60.0 / divisions()[static_cast<size_t> (l.sync - 1)].beats);

            hz *= std::exp2 (lfoRateMod[k] * 3);
            lfoValues[k] = lfos[k].next (hz / sampleRate, l.shape);
        }

        float l = 0, r = 0;

        for (auto& v : voices)
            if (v.isActive())
                renderVoice (v, s, l, r);

        effects (l, r, s);
        left[i] = l;
        right[i] = r;
    }

    for (; events != midi.cend(); ++events)
        handle ((*events).getMessage(), s);

    // Rate modulation from the global sources, for the next block.
    for (auto& m : lfoRateMod)
        m = 0;

    for (auto& m : s.mods)
    {
        if (m.target < targetLfo1Rate || m.target > targetLfo3Rate || m.amount == 0.0f)
            continue;

        float source = 0;

        switch (m.source)
        {
            case sourceLfo1: case sourceLfo2: case sourceLfo3: source = lfoValues[static_cast<size_t> (m.source - sourceLfo1)]; break;
            case sourceWheel: source = s.modWheel; break;
            case sourceAftertouch: source = s.aftertouch; break;
            default: break;
        }

        lfoRateMod[static_cast<size_t> (m.target - targetLfo1Rate)] += source * m.amount;
    }
}

void Synth::handle (const juce::MidiMessage& m, Settings& s)
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity(), s);
    else if (m.isNoteOff())
    {
        if (sustainPedal)
            sustained.push_back (m.getNoteNumber());
        else
            noteOff (m.getNoteNumber(), s);
    }
    else if (m.isPitchWheel())
        bend = static_cast<float> (m.getPitchWheelValue() - 8192) / 8192.0f * s.bendRange;
    else if (m.isController() && m.getControllerNumber() == 1)
        s.modWheel = m.getControllerValue() / 127.0f;
    else if (m.isChannelPressure())
        s.aftertouch = m.getChannelPressureValue() / 127.0f;
    else if (m.isSustainPedalOn())
        sustainPedal = true;
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
            v.env2.noteOff();
            v.env3.noteOff();
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
        if (v.note == note && v.held)
        {
            v.held = false;
            v.amp.noteOff();
            v.env2.noteOff();
            v.env3.noteOff();
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
        v.randomValue = v.random.nextFloat() * 2 - 1;

        for (auto& osc : v.phases)
            for (size_t u = 0; u < maxUnison; ++u)
                for (auto& side : osc[u])
                    side = u == 0 ? 0.0 : v.random.nextDouble();

        for (auto& o : v.outputs)
            o = 0;

        if (! v.isActive())
            for (auto& f : v.filters)
                for (auto& stage : f)
                    for (auto& side : stage)
                        side.reset();

        v.amp.noteOn();
        v.env2.noteOn();
        v.env3.noteOn();
    }
}

//==============================================================================
Synth::Mods Synth::modulation (const Voice& v, const Settings& s, float ampValue, float env2Value, float env3Value) const
{
    Mods m;

    for (auto& slot : s.mods)
    {
        if (slot.source == sourceOff || slot.target == targetOff || slot.amount == 0.0f)
            continue;

        float source = 0;

        switch (slot.source)
        {
            case sourceLfo1: case sourceLfo2: case sourceLfo3: source = lfoValues[static_cast<size_t> (slot.source - sourceLfo1)]; break;
            case sourceEnv2:        source = env2Value; break;
            case sourceEnv3:        source = env3Value; break;
            case sourceAmp:         source = ampValue; break;
            case sourceVelocity:    source = v.velocity; break;
            case sourceWheel:       source = s.modWheel; break;
            case sourceAftertouch:  source = s.aftertouch; break;
            case sourceKey:         source = (v.note - 60) / 36.0f; break;
            case sourceRandom:      source = v.randomValue; break;
            default: break;
        }

        const auto value = source * slot.amount;

        switch (slot.target)
        {
            case targetPitch:       m.pitch[0] += value * 12; break;
            case targetMorph:       m.morph += value * 2; break;
            case targetWarp:        m.warp += value; break;
            case targetFm:          m.fm += value; break;
            case targetRing:        m.ring += value; break;
            case targetPan:         m.pan += value; break;
            case targetF1Cutoff:    m.f1Cutoff += value * 5; break;
            case targetF1Res:       m.f1Res += value; break;
            case targetF2Cutoff:    m.f2Cutoff += value * 5; break;
            case targetF2Res:       m.f2Res += value; break;
            case targetDrive:       m.drive += value; break;
            case targetDetune:      m.detune += value * 40; break;
            default:
                if (slot.target >= targetPitch1 && slot.target <= targetPitch6)
                    m.pitch[static_cast<size_t> (slot.target - targetPitch1 + 1)] += value * 12;
                else if (slot.target >= targetLevel1 && slot.target <= targetLevel6)
                    m.level[static_cast<size_t> (slot.target - targetLevel1)] += value;
                break;
        }
    }

    return m;
}

void Synth::renderVoice (Voice& v, const Settings& s, float& left, float& right)
{
    if (std::abs (v.frequency - v.target) > 0.0)
    {
        const auto coefficient = s.glide > 0 ? std::exp (-4.6 / (s.glide * sampleRate)) : 0.0;
        v.frequency = v.target + (v.frequency - v.target) * coefficient;

        if (std::abs (v.frequency - v.target) < 1.0e-3)
            v.frequency = v.target;
    }

    const auto ampValue = v.amp.next();
    const auto env2Value = v.env2.next();
    const auto env3Value = v.env3.next();
    const auto m = modulation (v, s, ampValue, env2Value, env3Value);
    const auto base = v.frequency * std::pow (2.0, (bend + m.pitch[0]) / 12.0);

    // The oscillators, each feeding what it's told to; sync resets the others on osc 1's cycle.
    float bus[4][2] {};     // route, side
    bool wrapped1 = false;
    std::array<float, numOscs> outputs {};

    for (size_t n = 0; n < numOscs; ++n)
    {
        const auto& osc = s.oscs[n];

        if (! osc.on)
            continue;

        const auto hz = base * std::pow (2.0, (osc.coarse + osc.fine / 100.0 + m.pitch[n + 1]) / 12.0);
        const auto unison = juce::jlimit (1, maxUnison, osc.unison);
        const auto detune = std::max (0.0f, osc.detune + m.detune);
        const auto morphPosition = juce::jlimit (0.0, 4.0, static_cast<double> (osc.morph + m.morph));
        const auto warp = juce::jlimit (0.0f, 1.0f, osc.warp + m.warp);
        const auto fm = juce::jlimit (0.0f, 1.0f, osc.fm + m.fm);
        const auto ring = juce::jlimit (0.0f, 1.0f, osc.ring + m.ring);
        const auto level = juce::jlimit (0.0f, 1.0f, osc.level + m.level[n]);
        const auto modulator = osc.modSource > 0 ? v.outputs[static_cast<size_t> (osc.modSource - 1)] : 0.0f;
        const auto gain = level / std::sqrt (static_cast<float> (unison));
        float sum = 0, sides[2] {};

        for (int u = 0; u < unison; ++u)
        {
            const auto spread = unison > 1 ? 2.0 * u / (unison - 1) - 1 : 0.0;

            for (int side = 0; side < 2; ++side)
            {
                const auto cents = spread * detune / 2 + (unison > 1 ? (side == 0 ? -0.3 : 0.3) * detune * 0.25 : 0.0);
                const auto increment = hz * std::pow (2.0, cents / 1200.0) / sampleRate;
                auto& phase = v.phases[n][static_cast<size_t> (u)][static_cast<size_t> (side)];
                const auto t = warped (wrap (phase + modulator * fm * fmIndex), warp);
                auto out = morph (morphPosition, t, increment, 0.5 - 0.45 * warp);

                if (ring > 0)
                    out *= 1 - ring + ring * modulator;

                const auto next = phase + increment;

                if (n == 0 && u == 0 && side == 0 && next >= 1.0)
                    wrapped1 = true;

                phase = osc.sync && n > 0 && wrapped1 ? 0.0 : wrap (next);
                sides[side] += out;
                sum += out;
            }
        }

        outputs[n] = sum / static_cast<float> (2 * unison);
        const auto [gl, gr] = panGains (juce::jlimit (-1.0f, 1.0f, osc.pan + m.pan));
        const auto route = juce::jlimit (0, 3, osc.route);
        bus[route][0] += sides[0] * gain * gl;
        bus[route][1] += sides[1] * gain * gr;
    }

    v.outputs = outputs;

    // Through the filters: in series everything goes through both; in parallel each takes its
    // own bus and the balance mixes them.
    float out[2] {};
    const auto keyOctaves = (v.note - 60) / 12.0f;
    auto run = [&] (int k, float x, int side)
    {
        const auto& f = s.filters[static_cast<size_t> (k)];

        if (f.type == filterOff)
            return x;

        const auto octaves = (k == 0 ? env2Value * f.env + m.f1Cutoff : env3Value * f.env + m.f2Cutoff) * 6 + f.key * keyOctaves;
        const auto hz = juce::jlimit (20.0f, static_cast<float> (sampleRate * 0.45), f.cutoff * std::exp2 (octaves));
        const auto resonance = juce::jlimit (0.0f, 1.0f, f.resonance + (k == 0 ? m.f1Res : m.f2Res));
        const int type = f.type == highpass12 || f.type == highpass24 ? 1 : f.type == bandpass ? 2 : f.type == notch ? 3 : 0;
        const bool twoStages = f.type == lowpass24 || f.type == highpass24;

        if (f.drive > 0)
            x = std::tanh (x * (1 + f.drive * 4)) * (1 - 0.3f * f.drive);

        auto& stages = v.filters[static_cast<size_t> (k)];
        stages[0][static_cast<size_t> (side)].set (sampleRate, hz, resonance);
        x = stages[0][static_cast<size_t> (side)].process (x, type);

        if (twoStages)
        {
            stages[1][static_cast<size_t> (side)].set (sampleRate, hz, resonance);
            x = stages[1][static_cast<size_t> (side)].process (x, type);
        }

        return x;
    };

    for (int side = 0; side < 2; ++side)
    {
        if (s.filterRouting == 1)
        {
            const auto in = bus[routeFilter1][side] + bus[routeFilter2][side] + bus[routeBoth][side];
            out[side] = run (1, run (0, in, side), side);
        }
        else
        {
            const auto g1 = std::min (1.0f, 1 - s.filterBalance), g2 = std::min (1.0f, 1 + s.filterBalance);
            out[side] = run (0, bus[routeFilter1][side] + bus[routeBoth][side], side) * g1 + run (1, bus[routeFilter2][side] + bus[routeBoth][side], side) * g2;
        }

        out[side] += bus[routeDirect][side];
    }

    const auto gain = ampValue * ((1 - s.velocity) + s.velocity * v.velocity);
    left += out[0] * gain;
    right += out[1] * gain;

    if (! v.amp.isActive())
        v.note = -1;
}

//==============================================================================
void Synth::effects (float& left, float& right, const Settings& s)
{
    float x[2] { left, right };

    if (s.drive > 0)
        for (auto& c : x)
            c = shaped (c, s.driveType, s.drive);

    if (s.chorusMix > 0)
    {
        chorusPhase = wrap (chorusPhase + s.chorusRate / sampleRate);

        for (int c = 0; c < 2; ++c)
        {
            chorusLines[c].push (x[c]);
            const auto phase = wrap (chorusPhase + c * 0.25);
            const auto tri = phase < 0.5 ? 4 * phase - 1 : 3 - 4 * phase;
            const auto ms = 8 + 6 * s.chorusDepth * tri;
            const auto wet = chorusLines[c].read (static_cast<float> (ms * 0.001 * sampleRate));
            x[c] = x[c] * (1 - 0.5f * s.chorusMix) + wet * s.chorusMix * 0.7f;
        }
    }

    if (s.delayMix > 0)
    {
        const auto beats = divisions()[static_cast<size_t> (juce::jlimit (0, static_cast<int> (divisions().size()) - 1, s.delaySync))].beats;
        const auto samples = static_cast<float> (std::min (4.0, beats * 60.0 / s.tempo) * sampleRate);
        float wet[2];

        for (int c = 0; c < 2; ++c)
            wet[c] = delayTone[c].lowpass (delayLines[c].read (samples));

        for (int c = 0; c < 2; ++c)
        {
            const auto feed = s.delayPingPong ? wet[1 - c] : wet[c];
            delayLines[c].push (x[c] + feed * s.delayFeedback);
            x[c] += wet[c] * s.delayMix;
        }
    }

    if (s.reverbMix > 0)
    {
        const auto feedback = 0.7f + 0.28f * s.reverbSize;
        const auto damp = 0.1f + 0.8f * s.reverbDamp;
        const auto in = (x[0] + x[1]) * 0.5f * 0.3f;

        for (int c = 0; c < 2; ++c)
        {
            float sum = 0;

            for (auto& comb : combs[static_cast<size_t> (c)])
            {
                const auto delayed = comb.line.readInt (comb.length);
                comb.store = delayed * (1 - damp) + comb.store * damp;
                comb.line.push (in + comb.store * feedback);
                sum += delayed;
            }

            for (auto& a : allpasses[static_cast<size_t> (c)])
                sum = a.process (sum);

            x[c] += sum * s.reverbMix;
        }
    }

    left = dc[0].process (x[0]) * s.gain;
    right = dc[1].process (x[1]) * s.gain;
}

} // namespace sextet
