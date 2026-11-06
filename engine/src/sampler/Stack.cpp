#include "Stack.h"
#include "SampleCache.h"

#include <mutex>

namespace gk::sampler
{

using namespace model;

namespace
{
    /** A layer's sample as it plays (its part, reversed, polarity), folded to mono for summing. */
    struct LayerSound
    {
        std::vector<float> mono;
        double sampleRate = 44100;
        float peak = 0;             // of either channel
        double hitDb = -100;        // the loudest 50 ms
        double levelDb = -100;      // over the whole part
        double gainDb = 0;          // the sample layer's own gain
    };

    std::optional<LayerSound> soundOf (const SamplerInstrument& sampler, const std::filesystem::path& projectDir)
    {
        if (sampler.layers.empty())
            return {};

        auto& layer = sampler.layers.front();
        std::filesystem::path path = layer.sample;

        if (path.is_relative())
            path = projectDir / path;

        std::shared_ptr<const Sample> sample;

        try { sample = SampleCache::getInstance().load (path); }
        catch (const std::exception&) { return {}; }

        auto& audio = sample->audio;
        const auto rate = sample->sampleRate > 0 ? sample->sampleRate : 44100.0;
        const auto total = audio.getNumSamples();
        const auto from = std::clamp (static_cast<int> (layer.start * rate), 0, total);
        const auto to = std::clamp (layer.end ? static_cast<int> (*layer.end * rate) : total, from, total);

        LayerSound s;
        s.sampleRate = rate;
        s.gainDb = layer.gainDb;
        s.mono.resize (static_cast<size_t> (to - from));
        const auto channels = audio.getNumChannels();
        const float sign = layer.invert ? -1.0f : 1.0f;
        double sum = 0;

        for (int i = from; i < to; ++i)
        {
            float m = 0;

            for (int c = 0; c < channels; ++c)
            {
                const auto v = audio.getSample (c, i);
                m += v;
                s.peak = std::max (s.peak, std::abs (v));
            }

            m = sign * m / static_cast<float> (std::max (1, channels));
            s.mono[static_cast<size_t> (layer.reverse ? to - 1 - i : i - from)] = m;
            sum += static_cast<double> (m) * m;
        }

        if (s.mono.empty())
            return {};

        s.levelDb = 10 * std::log10 (std::max (1.0e-12, sum / static_cast<double> (s.mono.size())));

        // The loudest 50 ms: how loud a hit sounds.
        const auto window = std::max<size_t> (1, static_cast<size_t> (0.05 * rate));
        double run = 0, best = 0;

        for (size_t i = 0; i < s.mono.size(); ++i)
        {
            run += static_cast<double> (s.mono[i]) * s.mono[i];

            if (i >= window)
                run -= static_cast<double> (s.mono[i - window]) * s.mono[i - window];

            best = std::max (best, run);
        }

        s.hitDb = 10 * std::log10 (std::max (1.0e-12, best / static_cast<double> (std::min (window, s.mono.size()))));
        return s;
    }

    bool sounds (const StackInstrument& stack, const StackLayer& layer)
    {
        const auto anySolo = std::any_of (stack.layers.begin(), stack.layers.end(), [] (auto& l) { return l.solo; });
        return ! layer.mute && (! anySolo || layer.solo) && layer.mix > 0;
    }

    std::mutex cacheMutex;
    std::map<std::string, StackMix> cache;
}

std::map<Id, LoopTiming> loopTimings (const StackInstrument& stack, const std::filesystem::path& projectDir, double songTempo)
{
    std::map<Id, LoopTiming> timings;
    auto duration = [&] (const StackLayer& l) -> std::optional<double>
    {
        if (l.sampler.layers.empty())
            return {};

        auto& layer = l.sampler.layers.front();
        std::filesystem::path path = layer.sample;

        if (path.is_relative())
            path = projectDir / path;

        try
        {
            auto sample = SampleCache::getInstance().load (path);
            const auto rate = sample->sampleRate > 0 ? sample->sampleRate : 44100.0;
            const auto total = sample->audio.getNumSamples() / rate;
            return std::max (0.01, std::min (layer.end.value_or (total), total) - layer.start);
        }
        catch (const std::exception&)
        {
            return {};
        }
    };

    auto ownRate = [&] (const StackLayer& l) -> std::optional<double>
    {
        if (l.sampler.layers.empty() || ! l.sampler.layers.front().sourceTempo || *l.sampler.layers.front().sourceTempo <= 0)
            return {};

        return songTempo / *l.sampler.layers.front().sourceTempo * l.speed;
    };

    auto* master = getStackMaster (stack);
    const auto masterLength = master != nullptr ? duration (*master) : std::nullopt;

    if (! masterLength)
        return timings;

    const auto masterRate = std::clamp (ownRate (*master).value_or (master->speed), 0.125, 8.0);
    const auto masterCycle = *masterLength / masterRate;

    for (auto& l : stack.layers)
    {
        const auto length = duration (l);

        if (! length)
            continue;

        auto rate = ownRate (l).value_or (*length / (masterCycle / l.speed));
        rate = std::clamp (rate, 0.125, 8.0);
        timings[l.id] = { rate, *length / rate };
    }

    return timings;
}

double stackLayerDb (const StackMix::Layer& layer, double mix)
{
    return mix <= 0.001 ? -100.0 : layer.matchDb + 20 * std::log10 (mix);
}

StackMix mixStack (const StackInstrument& stack, const std::filesystem::path& projectDir, double songTempo)
{
    // What the result depends on: the samples (and their times on disk), how each plays, its mix.
    std::string key = std::to_string (static_cast<int> (stack.mode)) + "|" + std::to_string (songTempo) + "|" + projectDir.string();

    for (auto& l : stack.layers)
    {
        key += "\n" + l.id + "|" + std::to_string (l.mix) + "|" + (l.mute ? "m" : "") + (l.solo ? "s" : "") + (l.master ? "M" : "")
             + "|" + std::to_string (l.offsetMs) + "|" + std::to_string (l.speed);

        if (! l.sampler.layers.empty())
        {
            auto& s = l.sampler.layers.front();
            std::error_code ec;
            std::filesystem::path path = s.sample;
            const auto time = std::filesystem::last_write_time (path.is_relative() ? projectDir / path : path, ec);
            key += "|" + s.sample + "|" + std::to_string (ec ? 0LL : static_cast<long long> (time.time_since_epoch().count())) + "|" + std::to_string (s.start) + "|"
                 + std::to_string (s.end.value_or (-1)) + (s.reverse ? "r" : "") + (s.invert ? "i" : "") + "|" + std::to_string (s.gainDb)
                 + "|" + std::to_string (s.sourceTempo.value_or (0));
        }
    }

    {
        std::lock_guard lock (cacheMutex);

        if (auto it = cache.find (key); it != cache.end())
            return it->second;
    }

    StackMix mix;
    const bool loops = stack.mode == StackInstrumentMode::loops;
    std::map<Id, LayerSound> heard;
    double loudest = -100;

    for (auto& l : stack.layers)
        if (auto s = soundOf (l.sampler, projectDir))
        {
            const auto loudness = loops ? s->levelDb : s->hitDb;
            mix.layers[l.id].loudnessDb = loudness;

            if (sounds (stack, l))
                loudest = std::max (loudest, loudness);

            heard[l.id] = std::move (*s);
        }

    for (auto& l : stack.layers)
    {
        auto& m = mix.layers[l.id];

        if (! heard.contains (l.id))
            continue;

        // Up to the loudest; a sample far quieter than the rest is brought up by at most 24 dB.
        m.matchDb = std::clamp (loudest - m.loudnessDb, 0.0, 24.0);
        m.gainDb = sounds (stack, l) ? stackLayerDb (m, l.mix) : -100.0;
    }

    // The layers summed as they play, against the loudest of them alone. Loops are summed over
    // the longest time round (at most 16 s) at half the rate.
    const auto timings = loops ? loopTimings (stack, projectDir, songTempo) : std::map<Id, LoopTiming> {};
    double single = 0, summed = 0;
    double span = 0;

    for (auto& l : stack.layers)
        if (heard.contains (l.id) && mix.layers[l.id].gainDb > -100)
        {
            const auto& s = heard[l.id];
            const auto gain = std::pow (10.0, (mix.layers[l.id].gainDb + s.gainDb) / 20.0);
            single = std::max (single, gain * s.peak);

            if (loops)
            {
                if (auto t = timings.find (l.id); t != timings.end())
                    span = std::max (span, t->second.cycle);
            }
            else
                span = std::max (span, s.mono.size() / s.sampleRate + std::max (0.0, l.offsetMs / 1000.0));
        }

    if (single > 0)
    {
        span = std::min (span, loops ? 16.0 : 3.0);
        const double rate = loops ? 22050.0 : 44100.0;
        std::vector<float> sum (static_cast<size_t> (span * rate) + 1, 0.0f);

        for (auto& l : stack.layers)
        {
            if (! heard.contains (l.id) || mix.layers[l.id].gainDb <= -100)
                continue;

            const auto& s = heard[l.id];
            const auto gain = static_cast<float> (std::pow (10.0, (mix.layers[l.id].gainDb + s.gainDb) / 20.0));
            const auto offset = l.offsetMs / 1000.0;
            const auto timing = timings.find (l.id);

            for (size_t i = 0; i < sum.size(); ++i)
            {
                const auto t = static_cast<double> (i) / rate - offset;
                double at;

                if (loops)
                {
                    if (timing == timings.end())
                        break;

                    const auto cycle = timing->second.cycle;
                    at = std::fmod (std::fmod (t, cycle) + cycle, cycle) * timing->second.rate;
                }
                else
                {
                    if (t < 0)
                        continue;

                    at = t;
                }

                const auto index = static_cast<size_t> (at * s.sampleRate);

                if (index < s.mono.size())
                    sum[i] += gain * s.mono[index];
            }
        }

        for (auto v : sum)
            summed = std::max (summed, static_cast<double> (std::abs (v)));

        // No louder than the loudest layer alone, and under -1 dBFS whatever the matching brought
        // up. (The mono fold can't be louder than the louder channel: its peak against the layers'
        // channel peaks errs on the safe side by at most a little.)
        const auto target = std::min (single, std::pow (10.0, -1.0 / 20.0));
        const auto loudest = std::max (summed, single);

        if (loudest > target)
            mix.trimDb = 20 * std::log10 (target / loudest);

        mix.peak = target;
    }

    std::lock_guard lock (cacheMutex);

    if (cache.size() > 64)
        cache.clear();

    cache[key] = mix;
    return mix;
}

std::map<Id, double> alignStack (const StackInstrument& stack, const std::filesystem::path& projectDir)
{
    // Each attack: where the sound first reaches a quarter of its peak.
    std::map<Id, double> onsets;

    for (auto& l : stack.layers)
        if (auto s = soundOf (l.sampler, projectDir))
        {
            float peak = 0;

            for (auto v : s->mono)
                peak = std::max (peak, std::abs (v));

            for (size_t i = 0; i < s->mono.size(); ++i)
                if (std::abs (s->mono[i]) >= 0.25f * peak)
                {
                    onsets[l.id] = 1000.0 * static_cast<double> (i) / s->sampleRate;
                    break;
                }
        }

    double latest = 0;

    for (auto& [id, ms] : onsets)
        latest = std::max (latest, ms);

    std::map<Id, double> offsets;

    for (auto& [id, ms] : onsets)
        offsets[id] = std::round ((latest - ms) * 10) / 10;

    return offsets;
}

//==============================================================================
const char* StackOutputPlugin::xmlTypeName = "groovekitStackOutput";

StackOutputPlugin::StackOutputPlugin (tracktion::PluginCreationInfo info) : Plugin (info) {}

StackOutputPlugin::~StackOutputPlugin()
{
    notifyListenersOfDeletion();
}

void StackOutputPlugin::setMix (double trimDb, double peakLevel, double driveAmount)
{
    trim = juce::Decibels::decibelsToGain (static_cast<float> (trimDb));
    peak = std::clamp (static_cast<float> (peakLevel), 0.05f, 1.0f);
    drive = std::clamp (static_cast<float> (driveAmount), 0.0f, 1.0f);
}

void StackOutputPlugin::initialise (const tracktion::PluginInitialisationInfo& info)
{
    trimGain.reset (info.sampleRate, 0.03);
    trimGain.setCurrentAndTargetValue (trim);
}

void StackOutputPlugin::applyToBuffer (const tracktion::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    auto& buffer = *fc.destBuffer;
    trimGain.setTargetValue (trim);

    // Drive: a tanh curve scaled so the stack's peak stays where it was, so the hits get denser,
    // not louder. Then a soft ceiling: untouched below -1 dB, never past full scale.
    const auto d = drive.load();
    const auto k = 1.0f + 15.0f * d * d;
    const auto p = peak.load();
    const auto scale = p / std::tanh (k * p);
    constexpr float knee = 0.891f;  // -1 dB

    for (int i = 0; i < fc.bufferNumSamples; ++i)
    {
        const auto g = trimGain.getNextValue();

        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            auto* x = buffer.getWritePointer (c, fc.bufferStartSample + i);
            auto v = *x * g;

            if (d > 0.001f)
                v = scale * std::tanh (k * v);

            const auto a = std::abs (v);

            if (a > knee)
                v = std::copysign (knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee)), v);

            *x = v;
        }
    }
}

} // namespace gk::sampler
