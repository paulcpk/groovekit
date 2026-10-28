#include "TasteModel.h"

#include <algorithm>
#include <cmath>

namespace gk::listening
{

namespace
{
    constexpr size_t dims = std::tuple_size_v<Embedding>;

    float sigmoid (float x)     { return 1.0f / (1.0f + std::exp (-x)); }

    struct Example
    {
        Embedding x;
        float label, weight;
    };
}

std::optional<TasteModel> TasteModel::train (const std::vector<Embedding>& fits, const std::vector<Embedding>& misfits,
                                             const std::vector<Embedding>& background)
{
    if (fits.size() < 2 || (misfits.empty() && background.empty()))
        return {};

    TasteModel m;

    // Standardised against the library's sounds (or, without them, everything rated), so each
    // dimension counts by how much samples differ in it.
    std::vector<Embedding> rated;

    if (background.size() < 16)
    {
        rated = fits;
        rated.insert (rated.end(), misfits.begin(), misfits.end());
        rated.insert (rated.end(), background.begin(), background.end());
    }

    const auto& reference = background.size() >= 16 ? background : rated;

    for (auto& e : reference)
        for (size_t d = 0; d < dims; ++d)
            m.mean[d] += e[d] / static_cast<float> (reference.size());

    for (auto& e : reference)
        for (size_t d = 0; d < dims; ++d)
            m.scale[d] += (e[d] - m.mean[d]) * (e[d] - m.mean[d]) / static_cast<float> (reference.size());

    for (auto& s : m.scale)
        s = 1.0f / std::max (1.0e-3f, std::sqrt (s));

    // The verdicts weigh one each; the library as a whole half as much as the samples that fit.
    std::vector<Example> examples;
    auto add = [&] (const Embedding& e, float label, float weight)
    {
        Example ex { {}, label, weight };

        for (size_t d = 0; d < dims; ++d)
            ex.x[d] = (e[d] - m.mean[d]) * m.scale[d] / std::sqrt (static_cast<float> (dims));

        examples.push_back (ex);
    };

    for (auto& e : fits)        add (e, 1, 1);
    for (auto& e : misfits)     add (e, 0, 1);

    for (auto& e : background)
        add (e, 0, 0.5f * static_cast<float> (fits.size()) / static_cast<float> (background.size()));

    float totalWeight = 0;

    for (auto& ex : examples)
        totalWeight += ex.weight;

    // Weighted log loss plus a strong pull towards no opinion: a few verdicts mustn't make it
    // sure of anything. Plain gradient descent; features are about unit size, so it settles fast.
    constexpr float l2 = 0.02f, rate = 2.0f;
    constexpr int steps = 300;

    for (int step = 0; step < steps; ++step)
    {
        Embedding gradient {};
        float biasGradient = 0;

        for (auto& ex : examples)
        {
            float z = m.bias;

            for (size_t d = 0; d < dims; ++d)
                z += m.weights[d] * ex.x[d];

            const auto error = (sigmoid (z) - ex.label) * ex.weight / totalWeight;
            biasGradient += error;

            for (size_t d = 0; d < dims; ++d)
                gradient[d] += error * ex.x[d];
        }

        for (size_t d = 0; d < dims; ++d)
            m.weights[d] -= rate * (gradient[d] + l2 * m.weights[d]);

        m.bias -= rate * biasGradient;
    }

    m.confidenceValue = std::clamp (static_cast<float> (fits.size() + misfits.size() - 1) / 11.0f, 0.0f, 1.0f);
    return m;
}

float TasteModel::probability (const Embedding& e) const
{
    float z = bias;

    for (size_t d = 0; d < dims; ++d)
        z += weights[d] * (e[d] - mean[d]) * scale[d] / std::sqrt (static_cast<float> (dims));

    return sigmoid (z);
}

} // namespace gk::listening
