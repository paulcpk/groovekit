#include "Maps.h"

#include <fstream>

namespace gk::listening
{

using model::Json;

namespace
{
    constexpr int formatVersion = 1;

    /** A style or title as a file name: as it is, but for the characters that would make it a
        path, and short enough for any disk. Names come from other people's zips too. */
    std::string fileName (const std::string& name)
    {
        std::string safe;

        for (auto c : name)
            safe += c == '/' || c == '\\' || c == ':' || c == '\0' ? '_' : c;

        if (safe.empty() || safe.starts_with ("."))
            safe = "_" + safe;

        if (safe.size() > 200)
            safe.resize (200);

        return safe;
    }

    void writeAtomically (const std::filesystem::path& file, const std::string& text)
    {
        std::filesystem::create_directories (file.parent_path());
        const auto temp = file.string() + ".tmp";
        {
            std::ofstream out (temp, std::ios::trunc);
            out << text;

            if (! out)
                throw std::runtime_error ("Couldn't write " + file.string());
        }
        std::filesystem::rename (temp, file);
    }

    Json summary (const SongMap& m, bool inUse)
    {
        return { { "id", m.id() }, { "style", m.style }, { "title", m.title }, { "source", m.source }, { "analysis", m.track.analysis },
                 { "tempo", std::round (m.track.tempo * 10) / 10 }, { "heardAt", m.heardAt }, { "inUse", inUse } };
    }
}

Json SongMap::toJson() const
{
    return { { "format", formatVersion }, { "style", style }, { "title", title }, { "source", source }, { "heardAt", heardAt }, { "track", track.toJson() } };
}

SongMap SongMap::fromJson (const Json& j)
{
    SongMap m;
    m.style = j.value ("style", "");
    m.title = j.value ("title", "");
    m.source = j.value ("source", "heard");
    m.heardAt = j.value ("heardAt", int64_t { 0 });
    m.track = StyleProfile::Track::fromJson (j.value ("track", Json::object()));

    if (m.style.empty() || m.title.empty())
        throw std::runtime_error ("A song map needs a style and a title");

    return m;
}

//==============================================================================
MapStore::MapStore (std::filesystem::path f) : folder (std::move (f))
{
    std::error_code ec;

    for (auto& styleFolder : std::filesystem::directory_iterator (folder, ec))
    {
        if (! styleFolder.is_directory (ec))
            continue;

        for (auto& entry : std::filesystem::directory_iterator (styleFolder.path(), ec))
        {
            if (entry.path().extension() != ".json")
                continue;

            try
            {
                std::ifstream in (entry.path());
                auto map = std::make_shared<const SongMap> (SongMap::fromJson (Json::parse (in)));
                maps[map->id()] = std::move (map);
            }
            catch (const std::exception&)
            {
                // A damaged map is left out; hearing the song again replaces it.
            }
        }
    }

    try
    {
        std::ifstream in (folder / "index.json");

        if (in)
        {
            const auto j = Json::parse (in);

            for (auto& id : j.value ("unticked", std::vector<std::string>()))
                unticked.insert (id);

            for (auto& id : j.value ("skipped", std::vector<std::string>()))
                skipped.insert (id);

            const auto folders = j.value ("folders", Json::object());

            for (auto& [style, path] : folders.items())
                styleFolders[style] = path.get<std::string>();
        }
    }
    catch (const std::exception&)
    {
        // Without the index every song is in use and nothing is skipped.
    }
}

std::filesystem::path MapStore::getDefaultFolder()
{
    if (auto env = std::getenv ("GROOVEKIT_MAPS"))
        return env;

    return Styles::getReferencesFolder().parent_path() / "Maps";
}

std::vector<std::shared_ptr<const SongMap>> MapStore::all() const
{
    std::lock_guard lock (mutex);
    std::vector<std::shared_ptr<const SongMap>> list;

    for (auto& [id, map] : maps)
        list.push_back (map);

    return list;
}

std::shared_ptr<const SongMap> MapStore::get (const std::string& id) const
{
    std::lock_guard lock (mutex);
    auto it = maps.find (id);
    return it != maps.end() ? it->second : nullptr;
}

std::filesystem::path MapStore::fileFor (const SongMap& m) const
{
    return folder / fileName (m.style) / (fileName (m.title) + ".json");
}

void MapStore::put (SongMap m)
{
    auto map = std::make_shared<const SongMap> (std::move (m));
    std::lock_guard lock (mutex);
    writeAtomically (fileFor (*map), map->toJson().dump (1));
    maps[map->id()] = std::move (map);
    ++version;
    saveIndex();
}

void MapStore::remove (const std::string& id)
{
    std::lock_guard lock (mutex);
    auto it = maps.find (id);

    if (it == maps.end())
        return;

    std::error_code ec;
    std::filesystem::remove (fileFor (*it->second), ec);
    maps.erase (it);
    ++version;
    saveIndex();
}

bool MapStore::isInUse (const std::string& id) const
{
    std::lock_guard lock (mutex);
    return ! unticked.contains (id);
}

void MapStore::setInUse (const std::vector<std::string>& ids, bool inUse)
{
    std::lock_guard lock (mutex);

    for (auto& id : ids)
        inUse ? (void) unticked.erase (id) : (void) unticked.insert (id);

    ++version;
    saveIndex();
}

bool MapStore::isSkipped (const std::string& id) const
{
    std::lock_guard lock (mutex);
    return skipped.contains (id);
}

void MapStore::setSkipped (const std::vector<std::string>& ids, bool skip)
{
    std::lock_guard lock (mutex);

    for (auto& id : ids)
        skip ? (void) skipped.insert (id) : (void) skipped.erase (id);

    ++version;
    saveIndex();
}

std::optional<std::filesystem::path> MapStore::getStyleFolder (const std::string& style) const
{
    std::lock_guard lock (mutex);
    auto it = styleFolders.find (style);
    return it != styleFolders.end() ? std::optional (it->second) : std::nullopt;
}

void MapStore::setStyleFolder (const std::string& style, const std::filesystem::path& path)
{
    std::lock_guard lock (mutex);

    if (styleFolders[style] == path)
        return;

    styleFolders[style] = path;
    saveIndex();
}

uint64_t MapStore::getVersion() const
{
    std::lock_guard lock (mutex);
    return version;
}

void MapStore::saveIndex()
{
    auto songs = Json::array();

    for (auto& [id, map] : maps)
        songs.push_back (summary (*map, ! unticked.contains (id)));

    Json folders = Json::object();

    for (auto& [style, path] : styleFolders)
        folders[style] = path.string();

    try
    {
        writeAtomically (folder / "index.json", Json { { "format", formatVersion }, { "songs", songs }, { "unticked", unticked },
                                                        { "skipped", skipped }, { "folders", folders } }.dump (2));
    }
    catch (const std::exception&)
    {
        // The choices live on here until the next change writes them.
    }
}

//==============================================================================
int MapStore::exportZip (const std::vector<std::string>& ids, const std::filesystem::path& zip) const
{
    juce::ZipFile::Builder builder;
    auto songs = Json::array();
    int count = 0;

    auto add = [&] (const std::string& path, const std::string& text)
    {
        builder.addEntry (new juce::MemoryInputStream (text.data(), text.size(), true), 9, juce::String (path), juce::Time::getCurrentTime());
    };

    for (auto& id : ids)
    {
        auto map = get (id);

        if (map == nullptr)
            continue;

        // Someone else's: no paths from this Mac.
        auto copy = *map;
        copy.track.path = std::filesystem::path (copy.track.path).filename().string();
        add (fileName (copy.style) + "/" + fileName (copy.title) + ".json", copy.toJson().dump (1));
        songs.push_back (summary (copy, true));
        ++count;
    }

    if (count == 0)
        throw std::runtime_error ("None of these songs has a map to export");

    add ("index.json", Json { { "format", formatVersion }, { "songs", songs } }.dump (2));

    const juce::File file (zip.string());
    file.deleteFile();
    juce::FileOutputStream out (file);

    if (! out.openedOk() || ! builder.writeToStream (out, nullptr))
        throw std::runtime_error ("Couldn't write " + zip.string());

    return count;
}

MapStore::Imported MapStore::importZip (const std::filesystem::path& zip)
{
    juce::ZipFile archive (juce::File (zip.string()));

    if (archive.getNumEntries() == 0)
        throw std::runtime_error ("Not a zip of song maps: " + zip.string());

    Imported result;

    for (int i = 0; i < archive.getNumEntries(); ++i)
    {
        const auto name = archive.getEntry (i)->filename;

        if (! name.endsWithIgnoreCase (".json") || name.endsWithIgnoreCase ("index.json"))
            continue;

        std::unique_ptr<juce::InputStream> in (archive.createStreamForEntry (i));

        if (in == nullptr)
            continue;

        try
        {
            auto map = SongMap::fromJson (Json::parse (in->readEntireStreamAsString().toStdString()));

            if (auto existing = get (map.id()); existing != nullptr && existing->source == "heard")
            {
                ++result.kept;
                continue;
            }

            map.source = "imported";
            map.track.path = std::filesystem::path (map.track.path).filename().string();
            put (std::move (map));
            ++result.added;
        }
        catch (const std::exception&)
        {
            // Not a map: left out.
        }
    }

    return result;
}

} // namespace gk::listening
