#include "SampleFeatures.h"

#include <juce_dsp/juce_dsp.h>

namespace gk::library
{

using analysis::Audio;

namespace
{
    std::vector<float> monoMix (const Audio& audio, int numSamples)
    {
        std::vector<float> mono (static_cast<size_t> (numSamples), 0.0f);
        const auto channels = audio.buffer.getNumChannels();

        for (int ch = 0; ch < channels; ++ch)
        {
            auto* d = audio.buffer.getReadPointer (ch);

            for (int i = 0; i < numSamples; ++i)
                mono[static_cast<size_t> (i)] += d[i] / static_cast<float> (channels);
        }

        return mono;
    }

    /** RMS in consecutive windows. */
    std::vector<float> envelope (const std::vector<float>& mono, int window)
    {
        std::vector<float> env;

        for (size_t start = 0; start < mono.size(); start += static_cast<size_t> (window))
        {
            const auto end = std::min (mono.size(), start + static_cast<size_t> (window));
            double sum = 0;

            for (auto i = start; i < end; ++i)
                sum += static_cast<double> (mono[i]) * mono[i];

            env.push_back (static_cast<float> (std::sqrt (sum / static_cast<double> (end - start))));
        }

        return env;
    }

    void measureSpectrum (const std::vector<float>& mono, double sampleRate, SampleFeatures& f)
    {
        constexpr int order = 11, size = 1 << order, hop = size / 2;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> window (size, juce::dsp::WindowingFunction<float>::hann, false);

        std::vector<double> power (size / 2 + 1, 0.0);
        std::vector<float> frame (size * 2);

        for (size_t start = 0; start == 0 || start + size <= mono.size(); start += hop)
        {
            std::fill (frame.begin(), frame.end(), 0.0f);
            std::copy_n (mono.begin() + static_cast<std::ptrdiff_t> (start), std::min<size_t> (size, mono.size() - start), frame.begin());
            window.multiplyWithWindowingTable (frame.data(), size);
            fft.performFrequencyOnlyForwardTransform (frame.data(), true);

            for (size_t bin = 0; bin < power.size(); ++bin)
                power[bin] += static_cast<double> (frame[bin]) * frame[bin];

            if (mono.size() < size)
                break;
        }

        double total = 0, weighted = 0, sub = 0, low = 0, mid = 0, high = 0;

        for (size_t bin = 1; bin < power.size(); ++bin)
        {
            const auto hz = static_cast<double> (bin) * sampleRate / size;
            const auto p = power[bin];

            if (hz < 20)
                continue;

            total += p;
            weighted += p * hz;

            if (hz < 60)        sub += p;
            else if (hz < 250)  low += p;
            else if (hz < 2000) mid += p;
            else                high += p;
        }

        if (total <= 0)
            return;

        f.centroidHz = weighted / total;
        f.bands = { static_cast<float> (sub / total), static_cast<float> (low / total),
                    static_cast<float> (mid / total), static_cast<float> (high / total) };
    }

    /** Positive changes in log energy per 10 ms hop: peaks mark onsets. */
    std::vector<float> onsetStrength (const std::vector<float>& mono, int hop)
    {
        auto env = envelope (mono, hop);
        std::vector<float> flux (env.size(), 0.0f);

        // The file starts from silence, so a sound at the very beginning counts as an onset.
        for (size_t i = 0; i < env.size(); ++i)
            flux[i] = std::max (0.0f, std::log (env[i] + 1.0e-5f) - std::log ((i > 0 ? env[i - 1] : 0.0f) + 1.0e-5f));

        return flux;
    }

    int countOnsets (const std::vector<float>& flux, int minGapHops)
    {
        if (flux.empty())
            return 0;

        double mean = 0, var = 0;

        for (auto v : flux) mean += v;
        mean /= static_cast<double> (flux.size());

        for (auto v : flux) var += (v - mean) * (v - mean);
        const auto threshold = std::max (0.5, mean + 2.0 * std::sqrt (var / static_cast<double> (flux.size())));

        int count = 0, last = -minGapHops;

        for (int i = 0; i + 1 < static_cast<int> (flux.size()); ++i)
        {
            const auto v = flux[static_cast<size_t> (i)];
            const auto previous = i > 0 ? flux[static_cast<size_t> (i - 1)] : 0.0f;

            if (v > threshold && v >= previous && v >= flux[static_cast<size_t> (i + 1)] && i - last >= minGapHops)
            {
                ++count;
                last = i;
            }
        }

        return count;
    }

    std::optional<double> estimateTempo (const std::vector<float>& flux, double hopSeconds, double durationSeconds)
    {
        constexpr double minBpm = 70, maxBpm = 180;

        if (durationSeconds < 1.5 || flux.size() < 50)
            return {};

        double mean = 0;
        for (auto v : flux) mean += v;
        mean /= static_cast<double> (flux.size());

        auto autocorrelation = [&] (double lag)
        {
            const auto l = static_cast<size_t> (lag);
            const auto frac = lag - static_cast<double> (l);
            double sum = 0;

            for (size_t i = 0; i + l + 1 < flux.size(); ++i)
            {
                const auto shifted = (1.0 - frac) * flux[i + l] + frac * flux[i + l + 1];
                sum += (flux[i] - mean) * (shifted - mean);
            }

            return sum;
        };

        // A beat period also shows at twice its lag, so score both.
        auto score = [&] (double bpm)
        {
            const auto lag = 60.0 / bpm / hopSeconds;
            return autocorrelation (lag) + 0.5 * autocorrelation (lag * 2);
        };

        std::vector<double> candidates;

        // Loops usually span whole bars: prefer tempos that make the file an exact number of beats.
        for (int beats : { 2, 4, 8, 16, 32, 64, 128 })
            if (auto bpm = beats * 60.0 / durationSeconds; bpm >= minBpm && bpm <= maxBpm)
                candidates.push_back (bpm);

        if (candidates.empty())
            for (double bpm = minBpm; bpm <= maxBpm; bpm += 0.5)
                candidates.push_back (bpm);

        auto best = *std::max_element (candidates.begin(), candidates.end(),
                                       [&] (double a, double b) { return score (a) < score (b); });

        if (score (best) <= 0)
            return {};

        return std::round (best * 100.0) / 100.0;
    }
}

std::optional<SampleFeatures::Pitch> detectPitch (const Audio& audio, int startSample, int windowSamples)
{
    const auto sr = audio.sampleRate;
    const auto total = audio.buffer.getNumSamples();
    const auto tauMin = static_cast<int> (sr / 1500.0);
    auto tauMax = static_cast<int> (sr / 30.0);

    startSample = juce::jlimit (0, std::max (0, total - 1), startSample);
    windowSamples = std::min (windowSamples, total - startSample);
    tauMax = std::min (tauMax, windowSamples / 2);
    const auto length = windowSamples - tauMax;

    if (tauMax <= tauMin + 2 || length < 256)
        return {};

    auto mono = monoMix (audio, startSample + windowSamples);
    const float* x = mono.data() + startSample;

    double energy = 0;
    for (int j = 0; j < windowSamples; ++j) energy += static_cast<double> (x[j]) * x[j];

    if (energy / windowSamples < 1.0e-6) // below -60 dB
        return {};

    std::vector<double> d (static_cast<size_t> (tauMax + 1), 0.0);

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double sum = 0;

        for (int j = 0; j < length; ++j)
        {
            const double diff = x[j] - x[j + tau];
            sum += diff * diff;
        }

        d[static_cast<size_t> (tau)] = sum;
    }

    // Cumulative mean normalised difference
    std::vector<double> dn (d.size(), 1.0);
    double running = 0;

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += d[static_cast<size_t> (tau)];
        dn[static_cast<size_t> (tau)] = running > 0 ? d[static_cast<size_t> (tau)] * tau / running : 1.0;
    }

    int best = -1;

    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (dn[static_cast<size_t> (tau)] < 0.15)
        {
            while (tau + 1 < tauMax && dn[static_cast<size_t> (tau + 1)] < dn[static_cast<size_t> (tau)])
                ++tau;

            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        best = static_cast<int> (std::min_element (dn.begin() + tauMin, dn.begin() + tauMax) - dn.begin());

        if (dn[static_cast<size_t> (best)] > 0.35)
            return {};
    }

    // Parabolic interpolation around the minimum
    double tau = best;

    if (best > tauMin && best + 1 < tauMax)
    {
        const auto a = dn[static_cast<size_t> (best - 1)], b = dn[static_cast<size_t> (best)], c = dn[static_cast<size_t> (best + 1)];
        const auto denom = a - 2 * b + c;

        if (std::abs (denom) > 1.0e-12)
            tau += 0.5 * (a - c) / denom;
    }

    const auto hz = sr / tau;
    const auto note = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const auto key = static_cast<int> (std::lround (note));

    if (key < 0 || key > 127)
        return {};

    return SampleFeatures::Pitch { hz, key, (note - key) * 100.0, static_cast<float> (1.0 - dn[static_cast<size_t> (best)]) };
}

SampleFeatures analyseSample (const Audio& audio, double maxSeconds)
{
    SampleFeatures f;
    f.sampleRate = audio.sampleRate;
    f.channels = audio.buffer.getNumChannels();
    f.durationSeconds = audio.getDurationSeconds();

    const auto numSamples = std::min (audio.buffer.getNumSamples(), static_cast<int> (maxSeconds * audio.sampleRate));

    if (numSamples <= 0 || f.channels == 0)
        return f;

    const auto range = analysis::Range { 0, numSamples / audio.sampleRate, {} };
    f.peakDb = analysis::peakDb (audio, range);
    f.rmsDb = analysis::rmsDb (audio, range);

    if (f.durationSeconds >= 0.4)
        if (auto lufs = analysis::integratedLufs (analysis::slice (audio, range)); lufs > analysis::silenceDb)
            f.integratedLufs = lufs;

    auto mono = monoMix (audio, numSamples);

    const auto window = std::max (1, static_cast<int> (audio.sampleRate * 0.005));
    auto env = envelope (mono, window);
    const auto peak = static_cast<size_t> (std::max_element (env.begin(), env.end()) - env.begin());
    const auto floor = env[peak] * 0.0316f; // -30 dB
    auto decayEnd = peak;

    while (decayEnd < env.size() && env[decayEnd] > floor)
        ++decayEnd;

    f.attackMs = static_cast<double> (peak) * 5.0;
    f.decayMs = static_cast<double> (decayEnd - peak) * 5.0;

    measureSpectrum (mono, audio.sampleRate, f);

    // Pitch from the body of the sound, just after the attack.
    const auto pitchStart = static_cast<int> ((peak + 2) * static_cast<size_t> (window));
    f.pitch = detectPitch (audio, pitchStart, static_cast<int> (audio.sampleRate * 0.1));

    const auto hop = static_cast<int> (audio.sampleRate * 0.01);
    auto flux = onsetStrength (mono, hop);
    f.onsetCount = countOnsets (flux, 5);
    f.estimatedTempo = estimateTempo (flux, 0.01, numSamples / audio.sampleRate);

    return f;
}

model::Json toJson (const SampleFeatures& f)
{
    auto round = [] (double v, double step) { const auto k = std::round (1.0 / step); return std::round (v * k) / k; };

    model::Json j {
        { "durationSeconds", round (f.durationSeconds, 0.001) },
        { "sampleRate", f.sampleRate },
        { "channels", f.channels },
        { "peakDb", round (f.peakDb, 0.1) },
        { "rmsDb", round (f.rmsDb, 0.1) },
        { "attackMs", round (f.attackMs, 1) },
        { "decayMs", round (f.decayMs, 1) },
        { "centroidHz", round (f.centroidHz, 1) },
        { "bands", { { "sub", round (f.bands.sub, 0.01) }, { "low", round (f.bands.low, 0.01) },
                     { "mid", round (f.bands.mid, 0.01) }, { "high", round (f.bands.high, 0.01) } } },
        { "onsetCount", f.onsetCount },
    };

    if (f.integratedLufs)
        j["integratedLufs"] = round (*f.integratedLufs, 0.1);

    if (f.pitch)
        j["pitch"] = { { "hz", round (f.pitch->hz, 0.01) }, { "key", f.pitch->key },
                       { "cents", round (f.pitch->cents, 1) }, { "confidence", round (f.pitch->confidence, 0.01) } };

    if (f.estimatedTempo)
        j["estimatedTempo"] = *f.estimatedTempo;

    return j;
}

} // namespace gk::library
