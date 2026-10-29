#include "StarterPack.h"

#include "EngineContext.h"
#include "SampleLibrary.h"
#include "listening/Download.h"

#include <juce_core/juce_core.h>
#include <nlohmann/json.hpp>

#include <fstream>

namespace gk::library
{

namespace
{
    // The pack as samples/build-starter.mjs built and samples/README.md published it: the version
    // goes up when its contents change, and the size is the check that the whole zip came.
    constexpr int packVersion = 1;
    constexpr const char* packUrl = "https://github.com/paulcpk/groovekit/releases/download/starter-samples-v1/GrooveKit-Starter.zip";
    constexpr int64_t packBytes = 124825497;
    constexpr int packFiles = 812;
    constexpr const char* packFolderName = "GrooveKit Starter";

    std::string url()
    {
        // Tests and builds point this at a local copy of the zip.
        if (auto env = std::getenv ("GROOVEKIT_STARTER_URL"))
            return env;

        return packUrl;
    }

    int64_t bytes()
    {
        if (auto env = std::getenv ("GROOVEKIT_STARTER_BYTES"))
            return std::stoll (env);

        return packBytes;
    }

    /** Whether folder is inside (or is) one of the library's folders. */
    bool coveredBy (const std::vector<std::filesystem::path>& folders, const std::filesystem::path& folder)
    {
        std::error_code ec;
        const auto target = std::filesystem::weakly_canonical (folder, ec).string();

        for (auto& f : folders)
        {
            const auto base = std::filesystem::weakly_canonical (f, ec).string();

            if (target == base || target.starts_with (base + "/"))
                return true;
        }

        return false;
    }
}

StarterPack::StarterPack (SampleLibrary& l, std::filesystem::path f)
    : library (l), folder (f.empty() ? getDefaultFolder() : std::move (f))
{
    status.installed = isInstalled();
    status.totalBytes = bytes();
    status.folder = folder.string();
    status.files = packFiles;
}

StarterPack::~StarterPack()
{
    stop();
}

std::filesystem::path StarterPack::getDefaultFolder()
{
    return SampleLibrary::getDefaultFolder() / packFolderName;
}

void StarterPack::stop()
{
    stopping = true;

    if (downloader.joinable())
        downloader.join();
}

bool StarterPack::isInstalled() const
{
    // The pack's own manifest says which version is there.
    try
    {
        std::ifstream in (folder / "starter.json");

        if (! in)
            return false;

        return nlohmann::json::parse (in).value ("version", 0) >= packVersion;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

StarterPack::Status StarterPack::getStatus() const
{
    std::lock_guard lock (mutex);
    return status;
}

void StarterPack::setObserver (std::function<void (const Status&)> fn)
{
    std::lock_guard lock (mutex);
    observer = std::move (fn);
}

void StarterPack::addToLibrary()
{
    if (! coveredBy (library.getFolders(), folder))
        library.addFolder (folder);
}

void StarterPack::download()
{
    {
        std::lock_guard lock (mutex);

        if (status.installed || status.downloading)
            return;

        status.downloading = true;
        status.progress = 0;
        status.error.clear();
    }

    if (downloader.joinable())
        downloader.join();

    stopping = false;

    downloader = std::thread ([this]
    {
        auto report = [&] (auto&& change)
        {
            Status now;
            std::function<void (const Status&)> tell;
            {
                std::lock_guard lock (mutex);
                change (status);
                now = status;
                tell = observer;
            }

            if (tell)
                tell (now);
        };

        std::string error;
        const auto zip = EngineContext::getAppDataFolder() / "downloads" / "GrooveKit-Starter.zip";
        std::error_code ec;

        try
        {
            const auto total = static_cast<double> (bytes());

            // The download counts for most of the wait; unpacking gets the last stretch.
            downloadFile (url(), zip, bytes(), [&] (int64_t got)
            {
                report ([&] (Status& s) { s.progress = 0.9 * static_cast<double> (got) / total; });
            }, stopping);

            // The zip holds one folder, the pack's. A half-unpacked one from before goes first.
            juce::ZipFile archive (juce::File (zip.string()));

            if (archive.getNumEntries() == 0)
                throw std::runtime_error ("The download isn't a zip of samples");

            std::filesystem::remove_all (folder, ec);
            std::filesystem::create_directories (folder.parent_path(), ec);

            auto result = archive.uncompressTo (juce::File (folder.parent_path().string()), true);

            if (result.failed())
                throw std::runtime_error ("Couldn't unpack the samples: " + result.getErrorMessage().toStdString());

            if (! isInstalled())
                throw std::runtime_error ("The unpacked samples aren't the expected pack");

            addToLibrary();
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }

        std::filesystem::remove (zip, ec);

        report ([&] (Status& s)
        {
            s.downloading = false;
            s.installed = error.empty() && isInstalled();
            s.progress = s.installed ? 1.0 : 0.0;
            s.error = error;
        });
    });
}

} // namespace gk::library
