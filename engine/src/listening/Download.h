#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

namespace gk
{

/** Downloads url to target, through target + ".download" so a broken download never looks
    finished. size is the expected size in bytes, which doubles as the check that it all came.
    progress gets the bytes so far now and then. Throws std::runtime_error; stops early (and
    throws) when stop is set. */
void downloadFile (const std::string& url, const std::filesystem::path& target, int64_t size,
                   const std::function<void (int64_t bytes)>& progress, const std::atomic<bool>& stop);

} // namespace gk
