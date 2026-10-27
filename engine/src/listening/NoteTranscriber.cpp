#include "NoteTranscriber.h"

#include "Clap.h"
#include "Download.h"
#include "Ort.h"

namespace gk::listening
{

namespace
{
    // Basic Pitch's numbers (basic_pitch/constants.py and inference.py).
    constexpr double rate = 22050;
    constexpr int hopSamples = 256;                         // a frame
    constexpr int windowSamples = 2 * 22050 - hopSamples;   // 43844: two seconds, less a frame
    constexpr int framesPerWindow = 172;
    constexpr int overlapFrames = 30;
    constexpr int overlapSamples = overlapFrames * hopSamples;
    constexpr int strideSamples = windowSamples - overlapSamples;
    constexpr int lowestKey = 21;                           // A0

    constexpr float onsetThreshold = 0.5f, frameThreshold = 0.3f;
    constexpr int minNoteFrames = 11;                       // about 128 ms
    constexpr int energyTolerance = 11;

    constexpr const char* modelUrl = "https://github.com/spotify/basic-pitch/raw/v0.4.0/basic_pitch/saved_models/icassp_2022/nmp.onnx";
    constexpr int64_t modelBytes = 230444;
}

NoteTranscriber::NoteTranscriber (const Models& m) : models (m) {}
NoteTranscriber::~NoteTranscriber() = default;

void NoteTranscriber::ensureModel()
{
    if (session != nullptr)
        return;

    if (! std::filesystem::exists (modelFile()))
    {
        std::atomic<bool> stop { false };
        downloadFile (modelUrl, modelFile(), modelBytes, [] (int64_t) {}, stop);
    }

    models.loadRuntime();
    session = std::make_unique<Ort::Session> (openSession (modelFile(), 2));
}

std::vector<music::HeardNote> NoteTranscriber::transcribe (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    ensureModel();

    // Mono at 22.05 kHz, with half an overlap of silence in front, as Basic Pitch does.
    const auto mono = resampleMono (audio, sampleRate, rate);
    std::vector<float> padded (overlapSamples / 2, 0.0f);
    padded.insert (padded.end(), mono.begin(), mono.end());

    Activations a;
    std::vector<double> times;
    std::vector<float> window (windowSamples);
    const std::array<int64_t, 3> shape { 1, windowSamples, 1 };
    auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    const char* inputNames[] = { "serving_default_input_2:0" };
    const char* outputNames[] = { "StatefulPartitionedCall:1", "StatefulPartitionedCall:2" };   // notes, onsets
    const auto framesWanted = static_cast<size_t> (std::floor (static_cast<double> (mono.size()) * (static_cast<int> (rate) / hopSamples) / rate));

    for (size_t start = 0, w = 0; start < padded.size() && a.frames.size() < framesWanted; start += strideSamples, ++w)
    {
        std::fill (window.begin(), window.end(), 0.0f);
        std::copy_n (padded.begin() + static_cast<std::ptrdiff_t> (start), std::min<size_t> (windowSamples, padded.size() - start), window.begin());

        auto input = Ort::Value::CreateTensor<float> (memory, window.data(), window.size(), shape.data(), shape.size());
        auto out = session->Run (Ort::RunOptions { nullptr }, inputNames, &input, 1, outputNames, 2);
        const auto* notes = out[0].GetTensorData<float>();
        const auto* onsets = out[1].GetTensorData<float>();

        // Each window keeps its middle: half the overlap goes from each end.
        for (int f = overlapFrames / 2; f < framesPerWindow - overlapFrames / 2 && a.frames.size() < framesWanted; ++f)
        {
            std::array<float, 88> frame {}, onset {};
            std::copy_n (notes + f * 88, 88, frame.begin());
            std::copy_n (onsets + f * 88, 88, onset.begin());
            a.frames.push_back (frame);
            a.onsets.push_back (onset);
            times.push_back (static_cast<double> (static_cast<int> (start) + f * hopSamples - overlapSamples / 2) / rate);
        }
    }

    return notesFrom (std::move (a), times);
}

std::vector<music::HeardNote> NoteTranscriber::notesFrom (Activations a, const std::vector<double>& times)
{
    auto& frames = a.frames;
    auto& onsets = a.onsets;
    const auto n = static_cast<int> (frames.size());
    std::vector<music::HeardNote> notes;

    if (n < 3)
        return notes;

    // Onsets also where a key's frames jump up (inferred onsets), scaled to the onsets' peak.
    {
        std::vector<std::array<float, 88>> jumps (frames.size());
        float largestJump = 0, largestOnset = 0;

        for (int t = 0; t < n; ++t)
        {
            for (int k = 0; k < 88; ++k)
            {
                largestOnset = std::max (largestOnset, onsets[static_cast<size_t> (t)][static_cast<size_t> (k)]);

                if (t < 2)
                    continue;

                const auto now = frames[static_cast<size_t> (t)][static_cast<size_t> (k)];
                const auto jump = std::max (0.0f, std::min (now - frames[static_cast<size_t> (t - 1)][static_cast<size_t> (k)],
                                                            now - frames[static_cast<size_t> (t - 2)][static_cast<size_t> (k)]));
                jumps[static_cast<size_t> (t)][static_cast<size_t> (k)] = jump;
                largestJump = std::max (largestJump, jump);
            }
        }

        if (largestJump > 0)
            for (int t = 0; t < n; ++t)
                for (int k = 0; k < 88; ++k)
                {
                    auto& o = onsets[static_cast<size_t> (t)][static_cast<size_t> (k)];
                    o = std::max (o, largestOnset * jumps[static_cast<size_t> (t)][static_cast<size_t> (k)] / largestJump);
                }
    }

    auto energy = frames;
    const auto at = [] (auto& m, int t, int k) -> float& { return m[static_cast<size_t> (t)][static_cast<size_t> (k)]; };

    auto addNote = [&] (int first, int last, int k)
    {
        float sum = 0;

        for (int t = first; t < last; ++t)
            sum += at (frames, t, k);

        notes.push_back ({ times[static_cast<size_t> (first)], times[static_cast<size_t> (std::min (last, n - 1))],
                           static_cast<float> (k + lowestKey), sum / static_cast<float> (last - first) });
    };

    // Each onset peak over the threshold, latest first, holds while its key's frames do.
    std::vector<std::pair<int, int>> peaks;

    for (int t = 1; t + 1 < n; ++t)
        for (int k = 0; k < 88; ++k)
            if (const auto o = at (onsets, t, k); o >= onsetThreshold && o > at (onsets, t - 1, k) && o > at (onsets, t + 1, k))
                peaks.push_back ({ t, k });

    for (auto it = peaks.rbegin(); it != peaks.rend(); ++it)
    {
        const auto [start, k] = *it;

        if (start >= n - 1)
            continue;

        int t = start + 1, below = 0;

        while (t < n - 1 && below < energyTolerance)
        {
            below = at (energy, t, k) < frameThreshold ? below + 1 : 0;
            ++t;
        }

        t -= below;

        if (t - start <= minNoteFrames)
            continue;

        for (int i = start; i < t; ++i)
            for (int dk = -1; dk <= 1; ++dk)
                if (k + dk >= 0 && k + dk < 88)
                    at (energy, i, k + dk) = 0;

        addNote (start, t, k);
    }

    // What's left that's loud enough to be a note without an onset ("melodia trick"): from its
    // loudest frame, out both ways while it holds.
    for (;;)
    {
        int peakT = 0, peakK = 0;
        float peak = 0;

        for (int t = 0; t < n; ++t)
            for (int k = 0; k < 88; ++k)
                if (at (energy, t, k) > peak)
                {
                    peak = at (energy, t, k);
                    peakT = t;
                    peakK = k;
                }

        if (peak <= frameThreshold)
            break;

        at (energy, peakT, peakK) = 0;

        const auto clear = [&] (int t)
        {
            for (int dk = -1; dk <= 1; ++dk)
                if (peakK + dk >= 0 && peakK + dk < 88)
                    at (energy, t, peakK + dk) = 0;
        };

        int t = peakT + 1, below = 0;

        while (t < n - 1 && below < energyTolerance)
        {
            below = at (energy, t, peakK) < frameThreshold ? below + 1 : 0;
            clear (t);
            ++t;
        }

        const auto last = t - 1 - below;
        t = peakT - 1;
        below = 0;

        while (t > 0 && below < energyTolerance)
        {
            below = at (energy, t, peakK) < frameThreshold ? below + 1 : 0;
            clear (t);
            --t;
        }

        const auto first = t + 1 + below;

        if (last - first > minNoteFrames)
            addNote (first, last, peakK);
    }

    std::sort (notes.begin(), notes.end(), [] (auto& x, auto& y) { return x.start < y.start; });
    return notes;
}

} // namespace gk::listening
