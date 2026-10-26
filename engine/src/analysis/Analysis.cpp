#include "Analysis.h"

#include <juce_dsp/juce_dsp.h>

#include <array>

namespace gk::analysis
{

Audio loadAudio (const std::filesystem::path& path, std::optional<double> maxSeconds)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (std::filesystem::absolute (path).string())));

    if (reader == nullptr)
        throw std::runtime_error ("Can't read audio file " + path.string());

    auto length = reader->lengthInSamples;

    if (maxSeconds)
        length = std::min (length, static_cast<juce::int64> (*maxSeconds * reader->sampleRate));

    if (length > std::numeric_limits<int>::max())
        throw std::runtime_error ("Audio file too long: " + path.string());

    Audio audio;
    audio.sampleRate = reader->sampleRate;
    audio.buffer.setSize (static_cast<int> (reader->numChannels), static_cast<int> (length));
    reader->read (&audio.buffer, 0, static_cast<int> (length), 0, true, true);
    return audio;
}

namespace
{
    juce::Range<int> toSamples (const Audio& audio, Range range)
    {
        const auto total = audio.buffer.getNumSamples();
        const auto start = juce::jlimit (0, total, static_cast<int> (std::llround (range.start * audio.sampleRate)));
        const auto end = range.end ? juce::jlimit (start, total, static_cast<int> (std::llround (*range.end * audio.sampleRate))) : total;
        return { start, end };
    }

    std::pair<int, int> toChannels (const Audio& audio, Range range)
    {
        if (! range.channel)
            return { 0, audio.buffer.getNumChannels() };

        if (*range.channel < 0 || *range.channel >= audio.buffer.getNumChannels())
            throw std::runtime_error ("No channel " + std::to_string (*range.channel));

        return { *range.channel, *range.channel + 1 };
    }

    float toDb (double gain)
    {
        return gain > 0 ? static_cast<float> (20.0 * std::log10 (gain)) : silenceDb;
    }

    /** Biquad in direct form I, double precision. */
    struct Biquad
    {
        double b0, b1, b2, a1, a2;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        double process (double x)
        {
            const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    /** The two K-weighting stages from BS.1770, designed for any sample rate. */
    std::pair<Biquad, Biquad> makeKWeighting (double sampleRate)
    {
        const double pi = juce::MathConstants<double>::pi;

        // Stage 1: high shelf modelling the head
        double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        double k = std::tan (pi * f0 / sampleRate);
        double vh = std::pow (10.0, gainDb / 20.0);
        double vb = std::pow (vh, 0.4996667741545416);
        double a0 = 1.0 + k / q + k * k;
        Biquad shelf { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                       2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };

        // Stage 2: RLB high-pass
        f0 = 38.13547087602444; q = 0.5003270373238773;
        k = std::tan (pi * f0 / sampleRate);
        a0 = 1.0 + k / q + k * k;
        Biquad highPass { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };

        return { shelf, highPass };
    }
}

Audio slice (const Audio& audio, Range range)
{
    const auto r = toSamples (audio, range);
    const auto [firstChannel, endChannel] = toChannels (audio, range);

    Audio result;
    result.sampleRate = audio.sampleRate;
    result.buffer.setSize (endChannel - firstChannel, r.getLength());

    for (int ch = firstChannel; ch < endChannel; ++ch)
        result.buffer.copyFrom (ch - firstChannel, 0, audio.buffer, ch, r.getStart(), r.getLength());

    return result;
}

float peakDb (const Audio& audio, Range range)
{
    const auto r = toSamples (audio, range);
    const auto [firstChannel, endChannel] = toChannels (audio, range);
    float peak = 0;

    for (int ch = firstChannel; ch < endChannel; ++ch)
        peak = std::max (peak, audio.buffer.getMagnitude (ch, r.getStart(), r.getLength()));

    return toDb (peak);
}

float rmsDb (const Audio& audio, Range range)
{
    const auto r = toSamples (audio, range);
    const auto [firstChannel, endChannel] = toChannels (audio, range);
    const auto channels = endChannel - firstChannel;

    if (r.isEmpty() || channels <= 0)
        return silenceDb;

    double sum = 0;

    for (int ch = firstChannel; ch < endChannel; ++ch)
    {
        auto* d = audio.buffer.getReadPointer (ch);

        for (int i = r.getStart(); i < r.getEnd(); ++i)
            sum += static_cast<double> (d[i]) * d[i];
    }

    return toDb (std::sqrt (sum / (static_cast<double> (r.getLength()) * channels)));
}

LoudnessStats measureLoudness (const Audio& audio)
{
    LoudnessStats stats;
    const auto channels = audio.buffer.getNumChannels();
    const auto numSamples = audio.buffer.getNumSamples();
    const auto sr = audio.sampleRate;

    if (channels == 0 || numSamples < static_cast<int> (0.4 * sr))
        return stats;

    // Running sum of the K-weighted power over channels (all weights are 1 for up to stereo).
    std::vector<double> cumulative (static_cast<size_t> (numSamples) + 1, 0.0);
    std::vector<double> power (static_cast<size_t> (numSamples), 0.0);

    for (int ch = 0; ch < std::min (channels, 2); ++ch)
    {
        auto [shelf, highPass] = makeKWeighting (sr);
        auto* d = audio.buffer.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto y = highPass.process (shelf.process (d[i]));
            power[static_cast<size_t> (i)] += y * y;
        }
    }

    for (size_t i = 0; i < power.size(); ++i)
        cumulative[i + 1] = cumulative[i] + power[i];

    auto meanSquares = [&] (double windowSeconds)
    {
        const auto window = static_cast<size_t> (std::llround (windowSeconds * sr));
        const auto hop = static_cast<size_t> (std::llround (0.1 * sr));
        std::vector<double> values;

        for (size_t start = 0; start + window <= power.size(); start += hop)
            values.push_back ((cumulative[start + window] - cumulative[start]) / static_cast<double> (window));

        return values;
    };

    auto loudness = [] (double meanSquare) { return meanSquare > 0 ? -0.691 + 10.0 * std::log10 (meanSquare) : -1000.0; };

    auto gatedMean = [&] (const std::vector<double>& blocks, double threshold) -> std::optional<double>
    {
        double sum = 0;
        int count = 0;

        for (auto z : blocks)
            if (loudness (z) > threshold)
            {
                sum += z;
                ++count;
            }

        return count > 0 ? std::optional (sum / count) : std::nullopt;
    };

    // Integrated (BS.1770-4): 400 ms blocks, absolute gate -70 LUFS, relative gate -10 LU.
    const auto momentary = meanSquares (0.4);

    for (auto z : momentary)
        stats.momentaryMax = std::max (stats.momentaryMax, static_cast<float> (loudness (z)));

    if (auto absolute = gatedMean (momentary, -70.0))
        if (auto relative = gatedMean (momentary, loudness (*absolute) - 10.0))
            stats.integrated = static_cast<float> (loudness (*relative));

    // Short-term (3 s) and loudness range (EBU Tech 3342): relative gate -20 LU, 10th to 95th percentile.
    const auto shortTerm = meanSquares (3.0);
    std::vector<double> gated;

    for (auto z : shortTerm)
        stats.shortTermMax = std::max (stats.shortTermMax, static_cast<float> (loudness (z)));

    if (auto absolute = gatedMean (shortTerm, -70.0))
    {
        const auto threshold = loudness (*absolute) - 20.0;

        for (auto z : shortTerm)
            if (loudness (z) > threshold)
                gated.push_back (loudness (z));
    }

    if (gated.size() >= 2)
    {
        std::sort (gated.begin(), gated.end());
        auto percentile = [&] (double p) { return gated[static_cast<size_t> (std::lround (p * static_cast<double> (gated.size() - 1)))]; };
        stats.range = static_cast<float> (percentile (0.95) - percentile (0.10));
    }

    if (stats.momentaryMax < -200) stats.momentaryMax = silenceDb;
    if (stats.shortTermMax < -200) stats.shortTermMax = silenceDb;

    return stats;
}

float integratedLufs (const Audio& audio)
{
    return measureLoudness (audio).integrated;
}

float truePeakDb (const Audio& audio)
{
    // 4x oversampling with a windowed-sinc interpolator, per BS.1770-4 Annex 2.
    constexpr int taps = 12, phases = 4;
    static const auto kernels = []
    {
        std::array<std::array<float, 2 * taps>, phases> k {};

        for (int p = 1; p < phases; ++p)
            for (int n = -taps + 1; n <= taps; ++n)
            {
                const double x = n - static_cast<double> (p) / phases;
                const double sinc = std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double window = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * x / (taps + 1));
                k[static_cast<size_t> (p)][static_cast<size_t> (n + taps - 1)] = static_cast<float> (sinc * window);
            }

        return k;
    }();

    float peak = 0;

    for (int ch = 0; ch < audio.buffer.getNumChannels(); ++ch)
        peak = std::max (peak, audio.buffer.getMagnitude (ch, 0, audio.buffer.getNumSamples()));

    // Inter-sample peaks only matter next to loud samples.
    const auto interesting = peak * 0.5f;

    for (int ch = 0; ch < audio.buffer.getNumChannels(); ++ch)
    {
        auto* d = audio.buffer.getReadPointer (ch);
        const auto n = audio.buffer.getNumSamples();

        for (int i = taps; i + taps < n; ++i)
        {
            if (std::abs (d[i]) < interesting && std::abs (d[i + 1]) < interesting)
                continue;

            for (int p = 1; p < phases; ++p)
            {
                auto& k = kernels[static_cast<size_t> (p)];
                float sum = 0;

                for (int t = 0; t < 2 * taps; ++t)
                    sum += d[i - taps + 1 + t] * k[static_cast<size_t> (t)];

                peak = std::max (peak, std::abs (sum));
            }
        }
    }

    return toDb (peak);
}

namespace
{
    /** Band power summed over the whole file: absolute, or as a share of the total. */
    std::vector<SpectrumBand> measureBands (const Audio& audio, bool relative)
    {
    static const std::vector<std::pair<double, double>> edges {
        { 20, 60 }, { 60, 120 }, { 120, 250 }, { 250, 500 }, { 500, 1000 },
        { 1000, 2000 }, { 2000, 4000 }, { 4000, 8000 }, { 8000, 12000 }, { 12000, 20000 },
    };

    constexpr int order = 12, size = 1 << order, hop = size / 2;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window (size, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<double> power (size / 2 + 1, 0.0);
    std::vector<float> frame (size * 2);

    const auto channels = audio.buffer.getNumChannels();
    const auto n = audio.buffer.getNumSamples();

    for (int start = 0; start + size <= n || (start == 0 && n > 0); start += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);

        for (int i = 0; i < std::min (size, n - start); ++i)
            for (int ch = 0; ch < channels; ++ch)
                frame[static_cast<size_t> (i)] += audio.buffer.getSample (ch, start + i) / static_cast<float> (channels);

        window.multiplyWithWindowingTable (frame.data(), size);
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);

        for (size_t bin = 0; bin < power.size(); ++bin)
            power[bin] += static_cast<double> (frame[bin]) * frame[bin];

        if (start + size > n)
            break;
    }

    std::vector<double> bandPower (edges.size(), 0.0);
    double total = 0;

    for (size_t bin = 1; bin < power.size(); ++bin)
    {
        const auto hz = static_cast<double> (bin) * audio.sampleRate / size;

        for (size_t b = 0; b < edges.size(); ++b)
            if (hz >= edges[b].first && hz < edges[b].second)
            {
                bandPower[b] += power[bin];
                total += power[bin];
            }
    }

    std::vector<SpectrumBand> bands;
    const auto frames = std::max (1.0, static_cast<double> (n) / hop);

    for (size_t b = 0; b < edges.size(); ++b)
    {
        const auto reference = relative ? total : frames * size;
        bands.push_back ({ edges[b].first, edges[b].second,
                           reference > 0 && bandPower[b] > 0 ? static_cast<float> (10.0 * std::log10 (bandPower[b] / reference)) : silenceDb });
    }

    return bands;
    }
}

std::vector<SpectrumBand> spectralBalance (const Audio& audio)
{
    return measureBands (audio, true);
}

std::vector<SpectrumBand> bandEnergies (const Audio& audio)
{
    return measureBands (audio, false);
}

StereoStats measureStereo (const Audio& audio)
{
    StereoStats stats;

    if (audio.buffer.getNumChannels() < 2)
        return stats;

    auto* l = audio.buffer.getReadPointer (0);
    auto* r = audio.buffer.getReadPointer (1);

    // Below 150 Hz, where a mix should stay mono.
    const auto k = std::tan (juce::MathConstants<double>::pi * 150.0 / audio.sampleRate);
    const auto norm = 1.0 / (1.0 + std::sqrt (2.0) * k + k * k);
    Biquad lowL { k * k * norm, 2 * k * k * norm, k * k * norm, 2 * (k * k - 1) * norm, (1 - std::sqrt (2.0) * k + k * k) * norm };
    Biquad lowR = lowL;

    double ll = 0, rr = 0, lr = 0, mid = 0, side = 0, lowLL = 0, lowRR = 0, lowLR = 0;

    for (int i = 0; i < audio.buffer.getNumSamples(); ++i)
    {
        const double a = l[i], b = r[i];
        ll += a * a; rr += b * b; lr += a * b;
        mid += (a + b) * (a + b) * 0.25;
        side += (a - b) * (a - b) * 0.25;

        const auto la = lowL.process (a), lb = lowR.process (b);
        lowLL += la * la; lowRR += lb * lb; lowLR += la * lb;
    }

    stats.correlation = ll > 0 && rr > 0 ? static_cast<float> (lr / std::sqrt (ll * rr)) : 1.0f;
    stats.lowCorrelation = lowLL > 0 && lowRR > 0 ? static_cast<float> (lowLR / std::sqrt (lowLL * lowRR)) : 1.0f;
    stats.sideToMidDb = mid > 0 && side > 0 ? static_cast<float> (10.0 * std::log10 (side / mid)) : silenceDb;
    return stats;
}

std::vector<double> detectOnsets (const Audio& audio, float thresholdDb, double minGapSeconds)
{
    const auto threshold = juce::Decibels::decibelsToGain (thresholdDb);
    const auto minGap = static_cast<int> (std::llround (minGapSeconds * audio.sampleRate));
    const auto channels = audio.buffer.getNumChannels();

    std::vector<double> onsets;
    int quietSamples = minGap; // the start of the file counts as quiet

    for (int i = 0; i < audio.buffer.getNumSamples(); ++i)
    {
        float level = 0;

        for (int ch = 0; ch < channels; ++ch)
            level = std::max (level, std::abs (audio.buffer.getSample (ch, i)));

        if (level >= threshold)
        {
            if (quietSamples >= minGap)
                onsets.push_back (i / audio.sampleRate);

            quietSamples = 0;
        }
        else
        {
            ++quietSamples;
        }
    }

    return onsets;
}

model::Json analyse (const Audio& audio, bool detailed)
{
    // Dividing by a whole number gives the closest double to the rounded decimal (e.g. -6.02, not -6.0200000000000005).
    auto round = [] (double v, double step) { const auto k = std::round (1.0 / step); return std::round (v * k) / k; };

    auto onsets = model::Json::array();

    for (auto t : detectOnsets (audio))
        onsets.push_back (round (t, 0.0001));

    const auto loudness = measureLoudness (audio);
    const auto peak = peakDb (audio);
    const auto rms = rmsDb (audio);

    model::Json result {
        { "sampleRate", audio.sampleRate },
        { "channels", audio.buffer.getNumChannels() },
        { "durationSeconds", round (audio.getDurationSeconds(), 0.0001) },
        { "peakDb", round (peak, 0.01) },
        { "rmsDb", round (rms, 0.01) },
        { "integratedLufs", round (loudness.integrated, 0.01) },
        { "onsets", onsets },
    };

    if (! detailed)
        return result;

    result["truePeakDb"] = round (truePeakDb (audio), 0.01);
    result["crestDb"] = round (peak - rms, 0.1);
    result["momentaryMaxLufs"] = round (loudness.momentaryMax, 0.1);
    result["shortTermMaxLufs"] = round (loudness.shortTermMax, 0.1);
    result["loudnessRangeLu"] = round (loudness.range, 0.1);

    auto spectrum = model::Json::array();

    for (auto& band : spectralBalance (audio))
        spectrum.push_back ({ { "fromHz", band.fromHz }, { "toHz", band.toHz }, { "db", round (band.db, 0.1) } });

    result["spectrum"] = spectrum;

    if (audio.buffer.getNumChannels() >= 2)
    {
        auto stereo = measureStereo (audio);
        result["stereo"] = { { "correlation", round (stereo.correlation, 0.01) },
                             { "lowCorrelation", round (stereo.lowCorrelation, 0.01) },
                             { "sideToMidDb", round (stereo.sideToMidDb, 0.1) } };
    }

    return result;
}

} // namespace gk::analysis
