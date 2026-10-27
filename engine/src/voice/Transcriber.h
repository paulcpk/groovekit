#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct whisper_context;

namespace gk::voice
{

/** Speech to text on this Mac, for the agent's voice messages: whisper.cpp with Whisper's
    multilingual "small" model, which is downloaded once into the app data folder (nothing is
    sent anywhere else). Thread-safe. */
class Transcriber
{
public:
    /** The model file goes in this folder. */
    explicit Transcriber (std::filesystem::path modelFolder);
    ~Transcriber();

    /** Audio goes in at this rate, mono. */
    static constexpr int sampleRate = 16000;

    struct Status
    {
        bool ready = false;          // the model is on disk
        bool downloading = false;
        double progress = 0;         // 0..1 while downloading
        std::string error;           // why the last download failed
        int64_t modelBytes = 0;      // the download's size
    };

    Status getStatus() const;

    /** Downloads the model in the background, unless it's there or on its way. The callback runs
        on the download thread, with progress and at the end. */
    void download (std::function<void (const Status&)> onProgress);

    /** The text spoken in 16 kHz mono samples. language is a two-letter code, or empty to
        detect it. Loads the model on first use; blocks while it works, so call it off the
        message thread. Throws std::runtime_error. */
    std::string transcribe (const std::vector<float>& samples, const std::string& language);

    /** Loads the model in the background (the first load takes several seconds), so it's
        ready when a recording ends. Does nothing without the model, or once it's loaded. */
    void prepare();

private:
    /** With modelMutex held. */
    void load();

    const std::filesystem::path modelFile;
    mutable std::mutex mutex;
    Status status;
    std::thread downloader;
    std::atomic<bool> stopping { false };

    std::mutex modelMutex;   // one transcription at a time
    whisper_context* context = nullptr;
    std::thread loader;
};

} // namespace gk::voice
