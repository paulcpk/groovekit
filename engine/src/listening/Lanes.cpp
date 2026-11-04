#include "Lanes.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace gk::listening
{

namespace
{
    /** A 4th-order Linkwitz-Riley low- or high-pass (two Butterworth biquads), in place. */
    void linkwitzRiley (juce::AudioBuffer<float>& audio, double hz, bool highPass)
    {
        for (int c = 0; c < audio.getNumChannels(); ++c)
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                juce::IIRFilter f;
                f.setCoefficients (highPass ? juce::IIRCoefficients::makeHighPass (Separator::sampleRate, hz, 0.7071)
                                            : juce::IIRCoefficients::makeLowPass (Separator::sampleRate, hz, 0.7071));
                f.processSamples (audio.getWritePointer (c), audio.getNumSamples());
            }
        }
    }

    /** The music split by how long its sound holds: per frequency, the median level over about a
        second is what holds (pads, drones); what rises above it comes and goes (stabs, leads).
        Soft masks from the mono mix, applied to both channels, so the image stays. */
    juce::AudioBuffer<float> sustained (const juce::AudioBuffer<float>& music, bool keepSustained)
    {
        constexpr int order = 11, size = 1 << order, hop = size / 4, bins = size / 2 + 1;
        constexpr int reach = 43;           // frames either side: about a second in all
        constexpr int every = 4;            // medians every few frames, between them a straight line

        const auto n = music.getNumSamples();
        const auto channels = music.getNumChannels();
        const auto frames = n / hop + 1;
        juce::dsp::FFT fft (order);

        // A square-root Hann window both ways, a quarter apart: the windows squared sum to 2.
        std::vector<float> window (size);

        for (int i = 0; i < size; ++i)
            window[static_cast<size_t> (i)] = std::sqrt (0.5f - 0.5f * std::cos (2 * juce::MathConstants<float>::pi * static_cast<float> (i) / size));

        auto spectrum = [&] (const float* x, int frame, std::vector<float>& out)
        {
            std::fill (out.begin(), out.end(), 0.0f);
            const auto start = frame * hop - size / 2;

            for (int i = 0; i < size; ++i)
                if (start + i >= 0 && start + i < n)
                    out[static_cast<size_t> (i)] = x[start + i] * window[static_cast<size_t> (i)];

            fft.performRealOnlyForwardTransform (out.data(), true);
        };

        // Magnitudes of the mono mix, frame by frame.
        std::vector<float> mono (static_cast<size_t> (n));

        for (int i = 0; i < n; ++i)
        {
            float sum = 0;

            for (int c = 0; c < channels; ++c)
                sum += music.getSample (c, i);

            mono[static_cast<size_t> (i)] = sum / static_cast<float> (std::max (1, channels));
        }

        std::vector<float> buffer (2 * size);
        std::vector<float> magnitude (static_cast<size_t> (frames) * bins);

        for (int f = 0; f < frames; ++f)
        {
            spectrum (mono.data(), f, buffer);

            for (int b = 0; b < bins; ++b)
                magnitude[static_cast<size_t> (f) * bins + b] = std::hypot (buffer[static_cast<size_t> (2 * b)], buffer[static_cast<size_t> (2 * b + 1)]);
        }

        // What holds: the median over the second around each frame.
        const auto points = (frames + every - 1) / every;
        std::vector<float> held (static_cast<size_t> (points) * bins);
        std::vector<float> values;

        for (int p = 0; p < points; ++p)
        {
            const auto centre = p * every;
            const auto from = std::max (0, centre - reach), to = std::min (frames, centre + reach + 1);

            for (int b = 0; b < bins; ++b)
            {
                values.clear();

                for (int f = from; f < to; ++f)
                    values.push_back (magnitude[static_cast<size_t> (f) * bins + b]);

                std::nth_element (values.begin(), values.begin() + static_cast<long> (values.size() / 2), values.end());
                held[static_cast<size_t> (p) * bins + b] = values[values.size() / 2];
            }
        }

        // The masks, then each channel through them and back.
        juce::AudioBuffer<float> out (channels, n);
        out.clear();
        std::vector<float> mask (bins);

        for (int c = 0; c < channels; ++c)
        {
            const auto* x = music.getReadPointer (c);
            auto* y = out.getWritePointer (c);

            for (int f = 0; f < frames; ++f)
            {
                const auto p = f / every;
                const auto t = static_cast<float> (f % every) / every;

                for (int b = 0; b < bins; ++b)
                {
                    const auto a = held[static_cast<size_t> (p) * bins + b];
                    const auto next = p + 1 < points ? held[static_cast<size_t> (p + 1) * bins + b] : a;
                    const auto hold = std::min (a + (next - a) * t, magnitude[static_cast<size_t> (f) * bins + b]);
                    const auto rest = magnitude[static_cast<size_t> (f) * bins + b] - hold;
                    const auto m = hold * hold / (hold * hold + rest * rest + 1e-12f);
                    mask[static_cast<size_t> (b)] = keepSustained ? m : 1 - m;
                }

                spectrum (x, f, buffer);

                for (int b = 0; b < bins; ++b)
                {
                    buffer[static_cast<size_t> (2 * b)] *= mask[static_cast<size_t> (b)];
                    buffer[static_cast<size_t> (2 * b + 1)] *= mask[static_cast<size_t> (b)];
                }

                fft.performRealOnlyInverseTransform (buffer.data());
                const auto start = f * hop - size / 2;

                for (int i = 0; i < size; ++i)
                    if (start + i >= 0 && start + i < n)
                        y[start + i] += buffer[static_cast<size_t> (i)] * window[static_cast<size_t> (i)] * 0.5f;
            }
        }

        return out;
    }
}

bool isLane (const std::string& lane)
{
    return std::any_of (std::begin (laneNames), std::end (laneNames), [&] (const char* name) { return lane == name; });
}

std::string stemOfLane (const std::string& lane)
{
    return lane.starts_with ("drums-") ? "drums" : "music";
}

juce::AudioBuffer<float> laneOf (const juce::AudioBuffer<float>& stem, const std::string& lane)
{
    if (! isLane (lane))
        throw std::invalid_argument ("No lane \"" + lane + "\"");

    if (lane == "music-sustained" || lane == "music-short")
        return sustained (stem, lane == "music-sustained");

    juce::AudioBuffer<float> out (stem);

    if (lane == "drums-low")
    {
        linkwitzRiley (out, 150, false);
    }
    else if (lane == "drums-high")
    {
        linkwitzRiley (out, 5000, true);
    }
    else
    {
        linkwitzRiley (out, 150, true);
        linkwitzRiley (out, 5000, false);
    }

    return out;
}

std::optional<juce::AudioBuffer<float>> loadStem (const std::filesystem::path& folder, const std::string& name, double from, double to)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File ((folder / (name + ".flac")).string())));

    if (reader == nullptr)
        return {};

    const auto start = std::clamp<juce::int64> (static_cast<juce::int64> (from * reader->sampleRate), 0, reader->lengthInSamples);
    const auto end = to > from ? std::min<juce::int64> (static_cast<juce::int64> (to * reader->sampleRate), reader->lengthInSamples) : reader->lengthInSamples;
    juce::AudioBuffer<float> audio (2, static_cast<int> (std::max<juce::int64> (0, end - start)));
    reader->read (&audio, 0, audio.getNumSamples(), start, true, true);
    return audio;
}

void writeFlac (const juce::AudioBuffer<float>& audio, const std::filesystem::path& file)
{
    std::filesystem::create_directories (file.parent_path());
    const juce::File out (file.string());
    out.deleteFile();
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatWriter> writer (flac.createWriterFor (out.createOutputStream().release(), Separator::sampleRate, 2, 24, {}, 0));

    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
        throw std::runtime_error ("Couldn't write " + file.string());
}

} // namespace gk::listening
