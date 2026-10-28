#include "Stretch.h"

#include <tracktion_engine/tracktion_engine.h>


#include <rubberband/RubberBandStretcher.h>

namespace gk
{

namespace fs = std::filesystem;

namespace
{
    /** Bump when the algorithm or its settings change, so old cache entries aren't reused. */
    constexpr int stretchVersion = 7;

    std::string cacheKey (const fs::path& source, const ClipSource& how)
    {
        const auto canonical = fs::weakly_canonical (fs::absolute (source)).string();
        const auto modified = static_cast<long long> (fs::last_write_time (source).time_since_epoch().count());
        const auto description = canonical + "|" + std::to_string (fs::file_size (source)) + "|" + std::to_string (modified)
                               + "|" + std::to_string (how.start) + "|" + (how.end ? std::to_string (*how.end) : "-")
                               + "|" + (how.reverse ? "r" : "") + "|" + std::to_string (how.speed) + (how.resample ? "s" : "") + "|" + std::to_string (how.semitones)
                               + "|" + std::to_string (stretchVersion);

        // FNV-1a: the key only needs to be stable on this machine.
        uint64_t hash = 14695981039346656037ull;

        for (auto c : description)
            hash = (hash ^ static_cast<unsigned char> (c)) * 1099511628211ull;

        char hex[17];
        std::snprintf (hex, sizeof (hex), "%016llx", static_cast<unsigned long long> (hash));
        return std::filesystem::path (source).stem().string() + "-" + hex;
    }

    juce::AudioBuffer<float> readFile (const fs::path& source, double& sampleRate)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (fs::absolute (source).string())));

        if (reader == nullptr)
            throw std::runtime_error ("Can't read " + source.string());

        sampleRate = reader->sampleRate;
        juce::AudioBuffer<float> buffer (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        return buffer;
    }
}

juce::AudioBuffer<float> stretchAudio (const juce::AudioBuffer<float>& input, double sampleRate, double speed, double semitones)
{
    using RubberBand::RubberBandStretcher;

    const auto channels = input.getNumChannels();
    const auto inputLength = static_cast<size_t> (input.getNumSamples());
    const auto outputLength = static_cast<int> (std::llround (static_cast<double> (inputLength) / speed));

    // Offline mode studies the whole file first and keeps the output aligned with the input.
    RubberBandStretcher stretcher (static_cast<size_t> (sampleRate), static_cast<size_t> (channels),
                                   RubberBandStretcher::OptionProcessOffline | RubberBandStretcher::OptionEngineFiner | RubberBandStretcher::OptionWindowShort,
                                   1.0 / speed, std::pow (2.0, semitones / 12.0));
    stretcher.setExpectedInputDuration (inputLength);

    const auto block = std::min<size_t> (stretcher.getProcessSizeLimit(), 8192);
    std::vector<const float*> in (static_cast<size_t> (channels));

    auto pointersAt = [&] (size_t start)
    {
        for (int ch = 0; ch < channels; ++ch)
            in[static_cast<size_t> (ch)] = input.getReadPointer (ch, static_cast<int> (start));

        return in.data();
    };

    for (size_t start = 0; start < inputLength; start += block)
        stretcher.study (pointersAt (start), std::min (block, inputLength - start), start + block >= inputLength);

    juce::AudioBuffer<float> result (channels, outputLength);
    result.clear();
    int written = 0;

    auto collect = [&]
    {
        while (stretcher.available() > 0)
        {
            const auto count = static_cast<size_t> (stretcher.available());
            juce::AudioBuffer<float> chunk (channels, static_cast<int> (count));
            stretcher.retrieve (chunk.getArrayOfWritePointers(), count);

            const auto usable = std::min (static_cast<int> (count), outputLength - written);

            for (int ch = 0; ch < channels && usable > 0; ++ch)
                result.copyFrom (ch, written, chunk, ch, 0, usable);

            written += std::max (0, usable);
        }
    };

    for (size_t start = 0; start < inputLength; start += block)
    {
        stretcher.process (pointersAt (start), std::min (block, inputLength - start), start + block >= inputLength);
        collect();
    }

    collect();
    return result;
}

juce::AudioBuffer<float> resampleAudio (const juce::AudioBuffer<float>& input, double sampleRate, double speed)
{
    const auto outputLength = std::max (1, static_cast<int> (std::llround (input.getNumSamples() / speed)));
    juce::AudioBuffer<float> result (input.getNumChannels(), outputLength);
    result.clear();

    for (int ch = 0; ch < input.getNumChannels(); ++ch)
    {
        // Silence past the end, so the interpolator's last reads stay in bounds.
        std::vector<float> padded (static_cast<size_t> (input.getNumSamples() + 64), 0.0f);
        std::copy_n (input.getReadPointer (ch), input.getNumSamples(), padded.begin());

        // Played faster, what would land above the new Nyquist is taken out first so it doesn't
        // fold back down as aliasing.
        if (speed > 1)
            for (int stage = 0; stage < 4; ++stage)
            {
                juce::IIRFilter lowPass;
                lowPass.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, 0.45 * sampleRate / speed));
                lowPass.processSamples (padded.data(), static_cast<int> (padded.size()));
            }

        juce::WindowedSincInterpolator interpolator;
        interpolator.process (speed, padded.data(), result.getWritePointer (ch), outputLength,
                              static_cast<int> (padded.size()), 0);
    }

    return result;
}

fs::path getDefaultCacheDir()
{
    if (auto env = std::getenv ("GROOVEKIT_CACHE_DIR"))
        return env;

    // Not the system temp folder: Tracktion won't play files from there.
    auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toStdString();
    return fs::path (home) / "Library" / "Caches" / "GrooveKit";
}

namespace
{
    void writeWav (const juce::AudioBuffer<float>& audio, double sampleRate, const fs::path& target)
    {
        fs::create_directories (target.parent_path());
        auto temp = target;
        temp += ".tmp";

        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (juce::File (temp.string())),
                                                                                  sampleRate, static_cast<unsigned int> (audio.getNumChannels()),
                                                                                  24, {}, 0));
            if (writer == nullptr)
                throw std::runtime_error ("Can't write " + temp.string());

            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        }

        fs::rename (temp, target);
    }
}

fs::path getClipFile (const fs::path& source, const ClipSource& how, const fs::path& cacheDir)
{
    if (how.isWholeFile())
        return source;

    const auto target = cacheDir / "clips" / (cacheKey (source, how) + ".wav");

    if (fs::exists (target))
        return target;

    double sampleRate = 0;
    auto audio = readFile (source, sampleRate);
    const auto toFrame = [&] (double seconds) { return std::clamp (static_cast<int> (std::llround (seconds * sampleRate)), 0, audio.getNumSamples()); };
    const auto first = toFrame (how.start);
    const auto last = std::max (first, how.end ? toFrame (*how.end) : audio.getNumSamples());

    juce::AudioBuffer<float> part (audio.getNumChannels(), std::max (1, last - first));
    part.clear();

    for (int ch = 0; ch < audio.getNumChannels() && last > first; ++ch)
        part.copyFrom (ch, 0, audio, ch, first, last - first);

    if (how.reverse)
        part.reverse (0, part.getNumSamples());

    if (how.resample)
    {
        if (std::abs (how.speed - 1) > 1.0e-6)
            part = resampleAudio (part, sampleRate, how.speed);

        if (std::abs (how.semitones) > 1.0e-6)
            part = stretchAudio (part, sampleRate, 1, how.semitones);
    }
    else if (std::abs (how.speed - 1) > 1.0e-6 || std::abs (how.semitones) > 1.0e-6)
        part = stretchAudio (part, sampleRate, how.speed, how.semitones);

    writeWav (part, sampleRate, target);
    return target;
}

} // namespace gk
