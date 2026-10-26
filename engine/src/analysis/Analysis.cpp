#include "Analysis.h"

namespace gk::analysis
{

Audio loadAudio (const std::filesystem::path& path)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (std::filesystem::absolute (path).string())));

    if (reader == nullptr)
        throw std::runtime_error ("Can't read audio file " + path.string());

    Audio audio;
    audio.sampleRate = reader->sampleRate;
    audio.buffer.setSize (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
    reader->read (&audio.buffer, 0, static_cast<int> (reader->lengthInSamples), 0, true, true);
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

float integratedLufs (const Audio& audio)
{
    const auto channels = audio.buffer.getNumChannels();
    const auto numSamples = audio.buffer.getNumSamples();
    const auto blockSize = static_cast<int> (std::llround (0.4 * audio.sampleRate));
    const auto hop = static_cast<int> (std::llround (0.1 * audio.sampleRate));

    if (channels == 0 || numSamples < blockSize)
        return silenceDb;

    // K-weighted squared signal, summed over channels (all weights are 1 for up to stereo).
    std::vector<double> power (static_cast<size_t> (numSamples), 0.0);

    for (int ch = 0; ch < std::min (channels, 2); ++ch)
    {
        auto [shelf, highPass] = makeKWeighting (audio.sampleRate);
        auto* d = audio.buffer.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto y = highPass.process (shelf.process (d[i]));
            power[static_cast<size_t> (i)] += y * y;
        }
    }

    std::vector<double> blocks;

    for (int start = 0; start + blockSize <= numSamples; start += hop)
    {
        double sum = 0;

        for (int i = start; i < start + blockSize; ++i)
            sum += power[static_cast<size_t> (i)];

        blocks.push_back (sum / blockSize);
    }

    auto loudness = [] (double meanSquare) { return -0.691 + 10.0 * std::log10 (meanSquare); };

    auto gatedMean = [&] (double threshold)
    {
        double sum = 0;
        int count = 0;

        for (auto z : blocks)
        {
            if (z > 0 && loudness (z) > threshold)
            {
                sum += z;
                ++count;
            }
        }

        return count > 0 ? std::optional<double> (sum / count) : std::nullopt;
    };

    auto absoluteGated = gatedMean (-70.0);

    if (! absoluteGated)
        return silenceDb;

    auto relativeGated = gatedMean (loudness (*absoluteGated) - 10.0);
    return relativeGated ? static_cast<float> (loudness (*relativeGated)) : silenceDb;
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

model::Json analyse (const Audio& audio)
{
    // Dividing by a whole number gives the closest double to the rounded decimal (e.g. -6.02, not -6.0200000000000005).
    auto round = [] (double v, double step) { const auto k = std::round (1.0 / step); return std::round (v * k) / k; };

    auto onsets = model::Json::array();

    for (auto t : detectOnsets (audio))
        onsets.push_back (round (t, 0.0001));

    return {
        { "sampleRate", audio.sampleRate },
        { "channels", audio.buffer.getNumChannels() },
        { "durationSeconds", round (audio.getDurationSeconds(), 0.0001) },
        { "peakDb", round (peakDb (audio), 0.01) },
        { "rmsDb", round (rmsDb (audio), 0.01) },
        { "integratedLufs", round (integratedLufs (audio), 0.01) },
        { "onsets", onsets },
    };
}

} // namespace gk::analysis
