#include "Models.h"

#include "Download.h"
#include "EngineContext.h"
#include "Ort.h"

#include <juce_core/juce_core.h>

namespace gk::listening
{

namespace
{
    struct File
    {
        const char* name;
        const char* url;
        int64_t size;        // of the download
        int64_t finalSize;   // of the file it becomes (the same unless it's extracted)
    };

    // Pinned to revisions, so the files never change under the sizes checked here.
    constexpr const char* clapRepo = "https://huggingface.co/Xenova/larger_clap_music_and_speech/resolve/e9fd5ac1dbf3280936a7fc3ec8a020453ff184db/";
    constexpr const char* runtimeVersion = "1.31.1";

    const File files[] = {
        { "libonnxruntime.dylib", "https://github.com/microsoft/onnxruntime/releases/download/v1.31.1/onnxruntime-osx-arm64-1.31.1.tgz", 36936435, 44797256 },
        { "clap-audio.onnx", "onnx/audio_model_quantized.onnx", 78155433, 78155433 },
        { "clap-text.onnx", "onnx/text_model_quantized.onnx", 126603262, 126603262 },
        { "clap-vocab.json", "vocab.json", 798293, 798293 },
        { "clap-merges.txt", "merges.txt", 456318, 456318 },
        { "htdemucs.onnx", "https://huggingface.co/adowu/htdemucs-onnx/resolve/e6b688a9a9f977e2efaba79d0f18b9b0cfc606ea/htdemucs_fp16weights.onnx",
          165612636, 165612636 },
    };

    std::string urlOf (const File& f)
    {
        const std::string url = f.url;
        return url.starts_with ("https://") ? url : clapRepo + url;
    }

    int64_t totalBytes()
    {
        int64_t total = 0;

        for (auto& f : files)
            total += f.size;

        return total;
    }
}

Models::Models (std::filesystem::path f) : folder (std::move (f))
{
    status.ready = allPresent();
    status.totalBytes = totalBytes();
}

Models::~Models()
{
    stop();
}

void Models::stop()
{
    stopping = true;

    if (downloader.joinable())
        downloader.join();
}

std::filesystem::path Models::getDefaultFolder()
{
    return EngineContext::getAppDataFolder() / "models" / "listening";
}

bool Models::allPresent() const
{
    std::error_code ec;

    return std::all_of (std::begin (files), std::end (files), [&] (const File& f)
    {
        return std::filesystem::file_size (folder / f.name, ec) == static_cast<uintmax_t> (f.finalSize);
    });
}

Models::Status Models::getStatus() const
{
    std::lock_guard lock (mutex);
    return status;
}

void Models::download (std::function<void (const Status&)> onProgress)
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
        int64_t done = 0;
        const auto total = static_cast<double> (totalBytes());

        try
        {
            std::error_code ec;

            for (auto& f : files)
            {
                const auto target = folder / f.name;

                if (std::filesystem::file_size (target, ec) != static_cast<uintmax_t> (f.finalSize))
                {
                    const auto url = urlOf (f);
                    const bool archive = url.ends_with (".tgz");
                    auto downloaded = target;

                    if (archive)
                        downloaded += ".tgz";

                    downloadFile (url, downloaded, f.size, [&] (int64_t bytes)
                    {
                        report ([&] (Status& s) { s.progress = static_cast<double> (done + bytes) / total; });
                    }, stopping);

                    if (archive)
                    {
                        const auto member = std::string ("onnxruntime-osx-arm64-") + runtimeVersion + "/lib/libonnxruntime." + runtimeVersion + ".dylib";
                        extractFromTarGz (downloaded, member, target);
                        std::filesystem::remove (downloaded, ec);
                    }
                }

                done += f.size;
                report ([&] (Status& s) { s.progress = static_cast<double> (done) / total; });
            }
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }

        report ([&] (Status& s)
        {
            s.downloading = false;
            s.ready = error.empty() && allPresent();
            s.progress = s.ready ? 1.0 : 0.0;
            s.error = error;
        });
    });
}

void Models::loadRuntime() const
{
    if (! getStatus().ready)
        throw std::runtime_error ("The listening models aren't downloaded yet");

    loadOnnxRuntime (runtime());
}

//==============================================================================
void extractFromTarGz (const std::filesystem::path& archive, const std::string& member, const std::filesystem::path& target)
{
    juce::FileInputStream file (juce::File (archive.string()));

    if (! file.openedOk())
        throw std::runtime_error ("Couldn't read " + archive.string());

    juce::GZIPDecompressorInputStream tar (&file, false, juce::GZIPDecompressorInputStream::gzipFormat);
    char header[512];

    // Each entry is a 512-byte header, then its data padded to a multiple of 512 bytes.
    while (tar.read (header, 512) == 512 && header[0] != 0)
    {
        auto field = [&] (int offset, int length) { return std::string (header + offset, strnlen (header + offset, static_cast<size_t> (length))); };

        const auto prefix = field (345, 155);
        const auto name = prefix.empty() ? field (0, 100) : prefix + "/" + field (0, 100);
        const auto size = static_cast<int64_t> (std::strtoll (field (124, 12).c_str(), nullptr, 8));
        const auto padded = (size + 511) / 512 * 512;

        if (name == member && header[156] != '2')
        {
            const auto partial = target.string() + ".extract";
            juce::File out (partial);
            out.deleteFile();

            {
                juce::FileOutputStream stream (out);

                if (! stream.openedOk() || stream.writeFromInputStream (tar, size) != size)
                    throw std::runtime_error ("Couldn't extract " + member);
            }

            std::filesystem::rename (partial, target);
            return;
        }

        tar.skipNextBytes (padded);
    }

    throw std::runtime_error (member + " isn't in " + archive.string());
}

} // namespace gk::listening
