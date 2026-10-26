#include "RecentProjects.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace gk::recent
{

namespace
{
    constexpr size_t maxProjects = 20;
}

bool isProject (const std::filesystem::path& dir)
{
    std::error_code ec;
    return std::filesystem::is_regular_file (dir / "song.json", ec);
}

std::vector<std::filesystem::path> load (const std::filesystem::path& file)
{
    std::vector<std::filesystem::path> projects;

    try
    {
        std::ifstream in (file);

        if (! in)
            return projects;

        for (auto& p : nlohmann::json::parse (in).value ("projects", nlohmann::json::array()))
            if (std::filesystem::path dir = p.get<std::string>(); isProject (dir))
                projects.push_back (dir);
    }
    catch (const std::exception&)
    {
    }

    return projects;
}

void add (const std::filesystem::path& file, const std::filesystem::path& project)
{
    auto projects = load (file);
    const auto dir = std::filesystem::weakly_canonical (project);

    std::erase_if (projects, [&] (auto& p) { return std::filesystem::weakly_canonical (p) == dir; });
    projects.insert (projects.begin(), dir);

    if (projects.size() > maxProjects)
        projects.resize (maxProjects);

    auto list = nlohmann::json::array();

    for (auto& p : projects)
        list.push_back (p.string());

    std::error_code ec;
    std::filesystem::create_directories (file.parent_path(), ec);
    std::ofstream (file) << nlohmann::json { { "projects", list } }.dump (2);
}

} // namespace gk::recent
