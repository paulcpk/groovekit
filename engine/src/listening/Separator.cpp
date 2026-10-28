#include "Separator.h"

#include "Ort.h"

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

} // namespace gk::listening
