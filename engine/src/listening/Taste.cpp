#include "Taste.h"

#include "EngineContext.h"

#include <juce_core/juce_core.h>

#include <fstream>

namespace gk::listening
{

using model::Json;

Taste::Taste (std::filesystem::path f) : file (std::move (f))
{
    std::ifstream in (file);

    if (! in)
        return;

    try
    {
        for (auto& j : Json::parse (in).value ("ratings", Json::array()))
            ratings.push_back ({ j.value ("path", ""), j.value ("style", ""), j.value ("fits", true), j.value ("time", int64_t { 0 }) });
    }
    catch (const std::exception&)
    {
        ratings.clear();
    }
}

std::filesystem::path Taste::getDefaultFile()
{
    if (auto env = std::getenv ("GROOVEKIT_TASTE"))
        return env;

    return EngineContext::getAppDataFolder() / "taste.json";
}

void Taste::rate (const std::string& path, const std::string& style, std::optional<bool> fits)
{
    std::lock_guard lock (mutex);
    std::erase_if (ratings, [&] (auto& r) { return r.path == path && r.style == style; });

    if (fits)
        ratings.push_back ({ path, style, *fits, juce::Time::currentTimeMillis() });

    save();
}

std::vector<Taste::Rating> Taste::forStyle (const std::string& style) const
{
    std::lock_guard lock (mutex);
    std::vector<Rating> list;

    for (auto& r : ratings)
        if (r.style.empty() || juce::String (r.style).equalsIgnoreCase (juce::String (style)))
            list.push_back (r);

    return list;
}

std::vector<Taste::Rating> Taste::all() const
{
    std::lock_guard lock (mutex);
    return ratings;
}

void Taste::save() const
{
    auto list = Json::array();

    for (auto& r : ratings)
        list.push_back ({ { "path", r.path }, { "style", r.style }, { "fits", r.fits }, { "time", r.time } });

    std::filesystem::create_directories (file.parent_path());
    const auto temp = file.string() + ".tmp";

    {
        std::ofstream out (temp, std::ios::trunc);
        out << Json { { "ratings", list } }.dump (1);
    }

    std::filesystem::rename (temp, file);
}

} // namespace gk::listening
