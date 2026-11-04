#include "Separator.h"

#include "Ort.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace gk::listening
{

namespace
{
    // The model takes 7.8 s at a time; chunks overlap by a quarter and cross-fade.
    constexpr int chunk = 343980, overlap = chunk / 4, stride = chunk - overlap;
    constexpr int numSources = 4; // drums, bass, other, vocals
}

Separator::Separator (const Models& m) : models (m) {}
Separator::~Separator() = default;

Separator::Stems Separator::separate (const juce::AudioBuffer<float>& input, const std::function<bool (double)>& progress)
{
    if (session == nullptr)
    {
        models.loadRuntime();
        session = std::make_unique<Ort::Session> (openSession (models.separator(),
                                                               std::clamp (static_cast<int> (std::thread::hardware_concurrency()) - 2, 1, 8)));
    }

    const auto total = input.getNumSamples();
    std::array<juce::AudioBuffer<float>, numSources> out;

    for (auto& b : out)
        b.setSize (2, total), b.clear();

    std::vector<float> weight (static_cast<size_t> (total), 0.0f);
    std::vector<float> fade (static_cast<size_t> (chunk), 1.0f);

    for (int i = 0; i < overlap; ++i)
        fade[static_cast<size_t> (i)] = fade[static_cast<size_t> (chunk - 1 - i)] = static_cast<float> (i) / (overlap - 1);

    std::vector<float> mix (2 * static_cast<size_t> (chunk));
    const std::array<int64_t, 3> shape { 1, 2, chunk };
    auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

    for (int start = 0; start < total; start += stride)
    {
        const auto length = std::min (chunk, total - start);
        std::fill (mix.begin(), mix.end(), 0.0f);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < length; ++i)
                mix[static_cast<size_t> (c * chunk + i)] = input.getSample (std::min (c, input.getNumChannels() - 1), start + i);

        auto in = Ort::Value::CreateTensor<float> (memory, mix.data(), mix.size(), shape.data(), shape.size());
        const char* inputNames[] = { "mix" };
        const char* outputNames[] = { "stems" };
        auto result = session->Run (Ort::RunOptions { nullptr }, inputNames, &in, 1, outputNames, 1);
        const auto* stems = result.front().GetTensorData<float>(); // [source][channel][sample]

        for (int s = 0; s < numSources; ++s)
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < length; ++i)
                    out[static_cast<size_t> (s)].getWritePointer (c)[start + i] += stems[(s * 2 + c) * chunk + i] * fade[static_cast<size_t> (i)];

        for (int i = 0; i < length; ++i)
            weight[static_cast<size_t> (start + i)] += fade[static_cast<size_t> (i)];

        if (start + length >= total)
            break;

        if (progress && ! progress (static_cast<double> (start + length) / total))
            throw std::runtime_error ("Stopped");
    }

    for (auto& b : out)
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < total; ++i)
                b.getWritePointer (c)[i] /= std::max (weight[static_cast<size_t> (i)], 1e-8f);

    Stems stems { std::move (out[0]), std::move (out[1]), std::move (out[2]), std::move (out[3]) };

    for (int c = 0; c < 2; ++c)
        stems.other.addFrom (c, 0, stems.vocals, c, 0, total);

    return stems;
}

//==============================================================================
void saveStems (const Separator::Stems& stems, const std::filesystem::path& folder)
{
    const auto temp = std::filesystem::path (folder.string() + ".writing");
    std::filesystem::remove_all (temp);
    std::filesystem::create_directories (temp);

    // The rest without the vocals: the separator's "other" has them in.
    juce::AudioBuffer<float> music (stems.other);

    for (int c = 0; c < music.getNumChannels() && c < stems.vocals.getNumChannels(); ++c)
        music.addFrom (c, 0, stems.vocals, c, 0, std::min (music.getNumSamples(), stems.vocals.getNumSamples()), -1.0f);

    const juce::AudioBuffer<float>* buffers[] = { &stems.drums, &stems.bass, &music, &stems.vocals };
    juce::FlacAudioFormat flac;

    for (size_t i = 0; i < std::size (stemNames); ++i)
    {
        const juce::File file ((temp / (std::string (stemNames[i]) + ".flac")).string());
        std::unique_ptr<juce::AudioFormatWriter> writer (flac.createWriterFor (file.createOutputStream().release(), Separator::sampleRate,
                                                                               2, 24, {}, 0));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (*buffers[i], 0, buffers[i]->getNumSamples()))
            throw std::runtime_error ("Couldn't write the stems in " + temp.string());
    }

    std::filesystem::remove_all (folder);
    std::filesystem::rename (temp, folder);
}

bool hasStems (const std::filesystem::path& folder)
{
    return std::all_of (std::begin (stemNames), std::end (stemNames), [&] (const char* name)
    {
        return std::filesystem::exists (folder / (std::string (name) + ".flac"));
    });
}

std::optional<Separator::Stems> loadStems (const std::filesystem::path& folder, double from, double to)
{
    if (! hasStems (folder))
        return {};

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::array<juce::AudioBuffer<float>, std::size (stemNames)> read;

    for (size_t i = 0; i < read.size(); ++i)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File ((folder / (std::string (stemNames[i]) + ".flac")).string())));

        if (reader == nullptr)
            return {};

        const auto start = std::clamp<juce::int64> (static_cast<juce::int64> (from * reader->sampleRate), 0, reader->lengthInSamples);
        const auto end = to > from ? std::min<juce::int64> (static_cast<juce::int64> (to * reader->sampleRate), reader->lengthInSamples) : reader->lengthInSamples;
        const auto count = static_cast<int> (std::max<juce::int64> (0, end - start));
        read[i].setSize (2, count);
        reader->read (&read[i], 0, count, start, true, true);
    }

    Separator::Stems stems;
    stems.drums = std::move (read[0]);
    stems.bass = std::move (read[1]);
    stems.vocals = std::move (read[3]);
    stems.other = std::move (read[2]);

    // Other with the vocals in, as the separator gives it.
    for (int c = 0; c < stems.other.getNumChannels() && c < stems.vocals.getNumChannels(); ++c)
        stems.other.addFrom (c, 0, stems.vocals, c, 0, std::min (stems.other.getNumSamples(), stems.vocals.getNumSamples()));

    return stems;
}

} // namespace gk::listening
