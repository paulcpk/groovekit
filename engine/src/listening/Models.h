#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace gk::listening
{

/** The files that let GrooveKit listen, downloaded once into a folder: ONNX Runtime, CLAP
    (sounds and words in one space; LAION's larger_clap_music_and_speech, quantised) and
    htdemucs (splits a track into drums, bass and the rest). About 400 MB. Thread-safe. */
class Models
{
public:
    explicit Models (std::filesystem::path folder);
    ~Models();

    /** <app data>/models/listening */
    static std::filesystem::path getDefaultFolder();

    struct Status
    {
        bool ready = false;          // every file is on disk
        bool downloading = false;
        double progress = 0;         // 0..1 while downloading
        std::string error;           // why the last download failed
        int64_t totalBytes = 0;      // the whole download
    };

    Status getStatus() const;

    /** Downloads what's missing in the background, unless it's all there or on its way. The
        callback runs on the download thread, with progress and at the end. */
    void download (std::function<void (const Status&)> onProgress);

    /** Stops a running download and waits for it. */
    void stop();

    std::filesystem::path getFolder() const       { return folder; }
    std::filesystem::path clapAudio() const       { return folder / "clap-audio.onnx"; }
    std::filesystem::path clapText() const        { return folder / "clap-text.onnx"; }
    std::filesystem::path clapVocabulary() const  { return folder / "clap-vocab.json"; }
    std::filesystem::path clapMerges() const      { return folder / "clap-merges.txt"; }
    std::filesystem::path separator() const       { return folder / "htdemucs.onnx"; }
    std::filesystem::path runtime() const         { return folder / "libonnxruntime.dylib"; }

    /** Loads ONNX Runtime from the folder (see loadOnnxRuntime). Throws if the models aren't
        downloaded. */
    void loadRuntime() const;

private:
    const std::filesystem::path folder;
    mutable std::mutex mutex;
    Status status;
    std::thread downloader;
    std::atomic<bool> stopping { false };

    bool allPresent() const;
};

/** Copies the file at member (a path inside the archive) out of a .tgz to target. Throws if
    it isn't there. */
void extractFromTarGz (const std::filesystem::path& archive, const std::string& member, const std::filesystem::path& target);

} // namespace gk::listening
