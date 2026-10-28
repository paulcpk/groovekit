#include "Encode.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <lame.h>

#include <fstream>

namespace gk
{

std::string extensionFor (const std::string& format)
{
    if (format == "wav" || format == "flac" || format == "mp3")
        return format;

    throw std::invalid_argument ("format must be wav, flac or mp3");
}

namespace
{
    /** Triangular dither of one step at bits, so quiet tails fade into noise rather than
        breaking up when the samples are cut to that many bits. */
    juce::AudioBuffer<float> dithered (const juce::AudioBuffer<float>& audio, int bits)
    {
        juce::AudioBuffer<float> out (audio);
        juce::Random random (1);
        const auto step = 1.0f / static_cast<float> (1 << (bits - 1));

        for (int c = 0; c < out.getNumChannels(); ++c)
        {
            auto* x = out.getWritePointer (c);

            for (int i = 0; i < out.getNumSamples(); ++i)
                x[i] += (random.nextFloat() - random.nextFloat()) * step;
        }

        return out;
    }

    void writeWithJuce (juce::AudioFormat& format, const juce::AudioBuffer<float>& audio, double rate, const std::filesystem::path& file, int bits)
    {
        juce::File out (file.string());
        out.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (out.createOutputStream().release(), rate,
                                                                                 static_cast<unsigned int> (audio.getNumChannels()), bits, {}, 0));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            throw std::runtime_error ("Couldn't write " + file.string());
    }

    void writeMp3 (const juce::AudioBuffer<float>& audio, double rate, const std::filesystem::path& file, int kbps)
    {
        std::unique_ptr<lame_global_flags, decltype (&lame_close)> lame (lame_init(), &lame_close);

        if (lame == nullptr)
            throw std::runtime_error ("Couldn't start the MP3 encoder");

        const auto stereo = audio.getNumChannels() > 1;
        lame_set_in_samplerate (lame.get(), static_cast<int> (rate));
        lame_set_out_samplerate (lame.get(), static_cast<int> (rate));
        lame_set_num_channels (lame.get(), stereo ? 2 : 1);
        lame_set_mode (lame.get(), stereo ? JOINT_STEREO : MONO);
        lame_set_brate (lame.get(), std::clamp (kbps, 32, 320));
        lame_set_quality (lame.get(), 2);
        lame_set_write_id3tag_automatic (lame.get(), 0);

        if (lame_init_params (lame.get()) < 0)
            throw std::runtime_error ("The MP3 encoder can't take " + std::to_string (static_cast<int> (rate)) + " Hz; use 44.1 or 48 kHz");

        std::ofstream out (file, std::ios::binary | std::ios::trunc);

        if (! out)
            throw std::runtime_error ("Couldn't write " + file.string());

        constexpr int block = 8192;
        std::vector<unsigned char> encoded (static_cast<size_t> (1.25 * block + 7200));

        for (int start = 0; start < audio.getNumSamples(); start += block)
        {
            const auto n = std::min (block, audio.getNumSamples() - start);
            const auto bytes = lame_encode_buffer_ieee_float (lame.get(), audio.getReadPointer (0, start), audio.getReadPointer (stereo ? 1 : 0, start), n,
                                                              encoded.data(), static_cast<int> (encoded.size()));

            if (bytes < 0)
                throw std::runtime_error ("The MP3 encoder failed");

            out.write (reinterpret_cast<const char*> (encoded.data()), bytes);
        }

        const auto bytes = lame_encode_flush (lame.get(), encoded.data(), static_cast<int> (encoded.size()));
        out.write (reinterpret_cast<const char*> (encoded.data()), std::max (0, bytes));

        if (! out)
            throw std::runtime_error ("Couldn't write " + file.string());
    }
}

void writeAudio (const juce::AudioBuffer<float>& audio, double rate, const std::filesystem::path& file, const EncodeOptions& options)
{
    const auto& format = options.format;
    extensionFor (format);

    if (format == "mp3")
        return writeMp3 (audio, rate, file, options.bitrate);

    const auto bits = format == "flac" ? std::min (options.bitDepth, 24) : options.bitDepth;

    if (bits != 16 && bits != 24 && bits != 32)
        throw std::invalid_argument ("bitDepth must be 16, 24 or 32");

    const auto& samples = bits == 16 && options.dither ? dithered (audio, 16) : audio;

    if (format == "flac")
    {
        juce::FlacAudioFormat flac;
        return writeWithJuce (flac, samples, rate, file, bits);
    }

    juce::WavAudioFormat wav;
    writeWithJuce (wav, samples, rate, file, bits);
}

} // namespace gk
