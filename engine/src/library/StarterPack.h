#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace gk::library
{

class SampleLibrary;

/** The starter samples: public-domain drum machines, one-shots and loops, built by the repo's
    samples/build-starter.mjs and published as a release asset. Downloaded once, as a zip, into
    the library's default folder (~/Music/Samples/GrooveKit Starter) and added to the library.
    Thread-safe. */
class StarterPack
{
public:
    /** folder: where the pack lives; empty for the default. */
    StarterPack (SampleLibrary&, std::filesystem::path folder = {});
    ~StarterPack();

    /** <library default folder>/GrooveKit Starter */
    static std::filesystem::path getDefaultFolder();

    struct Status
    {
        bool installed = false;      // the pack is in its folder, at this version or newer
        bool downloading = false;
        double progress = 0;         // 0..1 while downloading, then unpacking
        std::string error;           // why the last download failed
        int64_t totalBytes = 0;      // the download
        std::string folder;
        int files = 0;               // how many samples the pack holds
    };

    Status getStatus() const;

    /** Downloads and unpacks the pack in the background, unless it's there or on its way, then
        adds its folder to the library (unless a library folder already holds it). Progress goes
        to the observer, on the download thread, and once more at the end. */
    void download();

    /** Told of progress; the last call has downloading false and, when it worked, installed true. */
    void setObserver (std::function<void (const Status&)>);

    /** Stops a running download and waits for it. */
    void stop();

    std::filesystem::path getFolder() const       { return folder; }

private:
    SampleLibrary& library;
    const std::filesystem::path folder;
    mutable std::mutex mutex;
    Status status;
    std::function<void (const Status&)> observer;
    std::thread downloader;
    std::atomic<bool> stopping { false };

    bool isInstalled() const;
    void addToLibrary();
};

} // namespace gk::library
