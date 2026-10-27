#include "Download.h"

#include <juce_core/juce_core.h>

namespace gk
{

void downloadFile (const std::string& url, const std::filesystem::path& target, int64_t size,
                   const std::function<void (int64_t)>& progress, const std::atomic<bool>& stop)
{
    auto partial = target;
    partial += ".download";

    try
    {
        std::filesystem::create_directories (target.parent_path());
        auto stream = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                             .withConnectionTimeoutMs (15000)
                                                             .withNumRedirectsToFollow (5));
        if (stream == nullptr)
            throw std::runtime_error ("Couldn't reach the download server. Check your connection.");

        const juce::File file (partial.string());
        file.deleteFile();

        {
            juce::FileOutputStream out (file);

            if (! out.openedOk())
                throw std::runtime_error ("Couldn't write " + partial.string());

            std::vector<char> buffer (1 << 16);
            int64_t written = 0;
            auto lastReport = juce::Time::getMillisecondCounter();

            while (! stream->isExhausted() && ! stop)
            {
                const auto read = stream->read (buffer.data(), static_cast<int> (buffer.size()));

                if (read <= 0)
                    break;

                out.write (buffer.data(), static_cast<size_t> (read));
                written += read;

                if (progress && juce::Time::getMillisecondCounter() - lastReport > 200)
                {
                    lastReport = juce::Time::getMillisecondCounter();
                    progress (written);
                }
            }

            out.flush();

            if (stop)
                throw std::runtime_error ("Stopped");

            if (written != size)
                throw std::runtime_error ("The download broke off. Try again.");
        }

        std::filesystem::rename (partial, target);
    }
    catch (...)
    {
        std::error_code ec;
        std::filesystem::remove (partial, ec);
        throw;
    }
}

} // namespace gk
