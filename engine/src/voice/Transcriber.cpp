#include "Transcriber.h"

#include "listening/Download.h"

#include <juce_core/juce_core.h>
#include <cstdlib>
#include <whisper.h>

namespace gk::voice
{

namespace
{
    // Whisper "small", multilingual, 5-bit quantised: fast on Apple silicon and good with German
    // and English alike. Its size doubles as the check that a download finished.
    constexpr const char* modelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin";
    constexpr const char* modelName = "ggml-small-q5_1.bin";
    constexpr int64_t modelSize = 190085487;

    /** Words a music producer says that Whisper would otherwise spell like everyday ones. */
    constexpr const char* vocabulary =
        "GrooveKit. Kick, snare, clap, hi-hat, 808, bassline, synth, pad, pluck, arp, reverb, delay, chorus, "
        "distortion, EQ, low-pass, high-pass, sidechain, compressor, limiter, insert, send, channel rack, playlist, "
        "pattern, automation clip, sampler, BPM, swing, drop, breakdown, build-up, intro, outro.";

    void quiet (ggml_log_level, const char*, void*) {}

    /** The loaded model, freed at exit before ggml's own statics go (they assert that nothing
        holds Metal memory any more). */
    std::atomic<whisper_context*> loaded { nullptr };

    void freeLoaded()
    {
        if (auto c = loaded.exchange (nullptr))
            whisper_free (c);
    }
}

Transcriber::Transcriber (std::filesystem::path modelFolder)
    : modelFile (std::move (modelFolder) / modelName)
{
    std::error_code ec;
    status.ready = std::filesystem::file_size (modelFile, ec) == static_cast<uintmax_t> (modelSize);
    status.modelBytes = modelSize;
    whisper_log_set (quiet, nullptr);
}

Transcriber::~Transcriber()
{
    stopping = true;

    if (downloader.joinable())
        downloader.join();

    if (loader.joinable())
        loader.join();

    if (context != nullptr && loaded.exchange (nullptr) == context)
        whisper_free (context);
}

Transcriber::Status Transcriber::getStatus() const
{
    std::lock_guard lock (mutex);
    return status;
}

void Transcriber::download (std::function<void (const Status&)> onProgress)
{
    {
        std::lock_guard lock (mutex);

        if (status.ready || status.downloading)
            return;

        status.downloading = true;
        status.progress = 0;
        status.error.clear();
    }

    if (downloader.joinable())
        downloader.join();

    downloader = std::thread ([this, onProgress = std::move (onProgress)]
    {
        auto report = [&] (auto&& change)
        {
            Status now;
            {
                std::lock_guard lock (mutex);
                change (status);
                now = status;
            }

            if (onProgress)
                onProgress (now);
        };

        std::string error;

        try
        {
            downloadFile (modelUrl, modelFile, modelSize, [&] (int64_t written)
            {
                report ([&] (Status& s) { s.progress = static_cast<double> (written) / static_cast<double> (modelSize); });
            }, stopping);
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }

        report ([&] (Status& s)
        {
            s.downloading = false;
            s.ready = error.empty();
            s.progress = error.empty() ? 1.0 : 0.0;
            s.error = error;
        });
    });
}

std::string Transcriber::transcribe (const std::vector<float>& samples, const std::string& language)
{
    if (! getStatus().ready)
        throw std::runtime_error ("The speech model isn't downloaded yet");

    std::lock_guard lock (modelMutex);
    load();

    // Under a quarter of a second is a click on the button, not speech.
    if (samples.size() < sampleRate / 4)
        return {};

    auto params = whisper_full_default_params (WHISPER_SAMPLING_GREEDY);
    params.n_threads = std::clamp (static_cast<int> (std::thread::hardware_concurrency()) - 2, 1, 8);
    params.language = language.empty() ? "auto" : language.c_str();
    params.initial_prompt = vocabulary;
    params.no_timestamps = true;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_special = false;
    params.print_timestamps = false;
    params.suppress_nst = true;

    if (whisper_full (context, params, samples.data(), static_cast<int> (samples.size())) != 0)
        throw std::runtime_error ("Transcription failed");

    std::string text;

    for (int i = 0; i < whisper_full_n_segments (context); ++i)
        text += whisper_full_get_segment_text (context, i);

    return juce::String (text).trim().toStdString();
}

void Transcriber::load()
{
    if (context != nullptr)
        return;

    auto params = whisper_context_default_params();
    params.use_gpu = true;
    context = whisper_init_from_file_with_params (modelFile.string().c_str(), params);

    if (context == nullptr)
        throw std::runtime_error ("Couldn't load the speech model; delete " + modelFile.string() + " to download it again");

    // Registered after ggml's statics exist, so it runs before they're destroyed.
    loaded = context;
    static const bool registered = (std::atexit (freeLoaded), true);
    (void) registered;
}

void Transcriber::prepare()
{
    if (! getStatus().ready)
        return;

    {
        std::unique_lock lock (modelMutex, std::try_to_lock);

        // Loaded, or being loaded or used right now.
        if (! lock.owns_lock() || context != nullptr)
            return;
    }

    if (loader.joinable())
        loader.join();

    loader = std::thread ([this]
    {
        std::lock_guard lock (modelMutex);

        try
        {
            load();
        }
        catch (const std::exception&)
        {
            // transcribe() reports it when it comes to that.
        }
    });
}

} // namespace gk::voice
