#pragma once

#include <array>
#include <cmath>

namespace gk::analysis
{

/** The 4x windowed-sinc interpolator of BS.1770-4 Annex 2, for finding peaks between samples. */
struct TruePeakInterpolator
{
    static constexpr int taps = 12;     // samples each side
    static constexpr int phases = 4;

    using Kernel = std::array<float, 2 * taps>;

    /** Kernels for the points 1/4, 2/4 and 3/4 of the way to the next sample (index 0 is unused). */
    static const std::array<Kernel, phases>& kernels()
    {
        static const auto k = []
        {
            std::array<Kernel, phases> result {};
            constexpr double pi = 3.14159265358979323846;

            for (int p = 1; p < phases; ++p)
                for (int n = -taps + 1; n <= taps; ++n)
                {
                    const double x = n - static_cast<double> (p) / phases;
                    const double sinc = std::sin (pi * x) / (pi * x);
                    const double window = 0.5 + 0.5 * std::cos (pi * x / (taps + 1));
                    result[static_cast<size_t> (p)][static_cast<size_t> (n + taps - 1)] = static_cast<float> (sinc * window);
                }

            return result;
        }();

        return k;
    }

    /** The largest magnitude at and after sample 0 up to (not including) sample 1.
        window must hold samples -11 to 12 (24 values, sample 0 at index 11). */
    static float peakAround (const float* window)
    {
        auto peak = std::abs (window[taps - 1]);

        for (int p = 1; p < phases; ++p)
        {
            auto& k = kernels()[static_cast<size_t> (p)];
            float sum = 0;

            for (int t = 0; t < 2 * taps; ++t)
                sum += window[t] * k[static_cast<size_t> (t)];

            peak = std::max (peak, std::abs (sum));
        }

        return peak;
    }
};

} // namespace gk::analysis
