#pragma once

#include <filesystem>
#include <vector>

namespace gk::recent
{

/** Project folders opened lately, most recent first, kept in <app data>/recent.json.
    Folders that no longer hold a project are left out. */
std::vector<std::filesystem::path> load (const std::filesystem::path& file);

/** Moves the folder to the front of the list (at most 20 are kept). Folders in the system's
    temporary folder aren't listed. */
void add (const std::filesystem::path& file, const std::filesystem::path& project);

/** True if the folder holds a project (a song.json). */
bool isProject (const std::filesystem::path& dir);

} // namespace gk::recent
