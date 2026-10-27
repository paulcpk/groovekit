#pragma once

#include "model/JsonIO.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <optional>

namespace gk::effects
{

/** What an effect measures for the UI to draw: frames of a few numbers each (levels, gain
    changes in dB), one per hop of about 5 ms, written by the audio thread and read by the API.
    The ring holds about five seconds. */
class MeterHistory
{
public:
    explicit MeterHistory (int valuesPerFrame, int frames = 1024)
        : width (valuesPerFrame), size (frames), data (static_cast<size_t> (valuesPerFrame * frames))
    {
    }

    static constexpr double hopSeconds = 0.005;

    /** Samples per frame at this rate. */
    static int hopSamples (double sampleRate)     { return std::max (1, juce::roundToInt (sampleRate * hopSeconds)); }

    /** Adds a frame (the audio thread). */
    void push (const float* values)
    {
        const auto n = written.load (std::memory_order_relaxed);
        const auto base = static_cast<size_t> ((n % size) * width);

        for (int i = 0; i < width; ++i)
            data[base + static_cast<size_t> (i)].store (values[i], std::memory_order_relaxed);

        written.store (n + 1, std::memory_order_release);
    }

    /** { position, frames: [[...], ...] }: the frames written after `since` (a position this
        returned before), or the latest ones, oldest first. Values are rounded to 0.1. */
    model::Json read (std::optional<int64_t> since, int maxFrames = 1024) const
    {
        const auto end = written.load (std::memory_order_acquire);
        auto start = since.value_or (end - maxFrames);
        start = std::clamp (start, std::max<int64_t> (0, end - std::min<int64_t> (size - 1, maxFrames)), end);

        auto frames = model::Json::array();

        for (auto n = start; n < end; ++n)
        {
            auto frame = model::Json::array();
            const auto base = static_cast<size_t> ((n % size) * width);

            for (int i = 0; i < width; ++i)
                frame.push_back (std::round (data[base + static_cast<size_t> (i)].load (std::memory_order_relaxed) * 10.0f) / 10.0f);

            frames.push_back (std::move (frame));
        }

        return { { "position", end }, { "frames", std::move (frames) } };
    }

private:
    const int width;
    const int64_t size;
    std::vector<std::atomic<float>> data;
    std::atomic<int64_t> written { 0 };
};

/** The latest stereo samples, for a vectorscope: written by the audio thread, read by the API. */
class StereoScope
{
public:
    static constexpr int size = 4096;

    void push (float left, float right)
    {
        const auto n = written.load (std::memory_order_relaxed);
        samples[static_cast<size_t> (2 * (n % size))].store (left, std::memory_order_relaxed);
        samples[static_cast<size_t> (2 * (n % size) + 1)].store (right, std::memory_order_relaxed);
        written.store (n + 1, std::memory_order_release);
    }

    /** { left: [...], right: [...] }: the latest numSamples, oldest first, rounded to 0.001. */
    model::Json read (int numSamples) const
    {
        numSamples = std::clamp (numSamples, 0, size / 2);
        const auto end = written.load (std::memory_order_acquire);
        auto left = model::Json::array(), right = model::Json::array();

        for (auto n = std::max<int64_t> (0, end - numSamples); n < end; ++n)
        {
            left.push_back (std::round (samples[static_cast<size_t> (2 * (n % size))].load (std::memory_order_relaxed) * 1000.0f) / 1000.0f);
            right.push_back (std::round (samples[static_cast<size_t> (2 * (n % size) + 1)].load (std::memory_order_relaxed) * 1000.0f) / 1000.0f);
        }

        return { { "left", std::move (left) }, { "right", std::move (right) } };
    }

private:
    std::vector<std::atomic<float>> samples = std::vector<std::atomic<float>> (2 * size);
    std::atomic<int64_t> written { 0 };
};

/** Peak in dB over a hop, for meters: floored at -100. */
inline float toMeterDb (float gain)
{
    return juce::Decibels::gainToDecibels (gain, -100.0f);
}

} // namespace gk::effects
