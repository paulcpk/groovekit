#pragma once

#include "Styles.h"

#include <set>

namespace gk::listening
{

/** A reference track heard once and kept: everything the hearing found in it, so it's curated and
    shared instead of heard again. One JSON file per song, <style>/<title>.json. */
struct SongMap
{
    std::string style;
    std::string title;                  // the audio file's name without its extension: usually "Artist - Title"
    std::string source = "heard";       // heard on this Mac, or "imported" from someone's zip
    int64_t heardAt = 0;                // ms since 1970
    StyleProfile::Track track;          // its path is the audio's here; only the file name in an export

    std::string id() const { return style + "/" + title; }

    model::Json toJson() const;
    static SongMap fromJson (const model::Json&);
};

/** The song maps in a folder, one subfolder per style beside the references' own, with an
    index.json listing them: their style, what was heard, and the user's choices (which songs are
    in use, which are skipped, where each style's tracks are). The maps are the truth; the index
    keeps the choices and is rewritten with every change. Thread-safe. */
class MapStore
{
public:
    explicit MapStore (std::filesystem::path folder);

    /** GROOVEKIT_MAPS, or Maps beside the references folder (~/Music/GrooveKit/Maps). */
    static std::filesystem::path getDefaultFolder();

    const std::filesystem::path& getFolder() const    { return folder; }

    std::vector<std::shared_ptr<const SongMap>> all() const;
    std::shared_ptr<const SongMap> get (const std::string& id) const;

    /** Adds or replaces a map, on disk and here. */
    void put (SongMap);
    void remove (const std::string& id);

    /** Songs are in use (their style is learnt from them) unless unticked, heard or not. */
    bool isInUse (const std::string& id) const;
    void setInUse (const std::vector<std::string>& ids, bool);

    /** Skipped songs aren't heard, until they're asked for by name. */
    bool isSkipped (const std::string& id) const;
    void setSkipped (const std::vector<std::string>& ids, bool);

    /** Where a style's tracks are, as last heard from. */
    std::optional<std::filesystem::path> getStyleFolder (const std::string& style) const;
    void setStyleFolder (const std::string& style, const std::filesystem::path&);

    /** Goes up with every change to a map or a choice. */
    uint64_t getVersion() const;

    /** Writes the maps to a zip, with their index, for someone else: one folder per style, and
        file names in place of paths. Returns how many went in. */
    int exportZip (const std::vector<std::string>& ids, const std::filesystem::path& zip) const;

    /** Adds the maps in a zip as imported. A song heard here keeps its own map. */
    struct Imported { int added = 0, kept = 0; };
    Imported importZip (const std::filesystem::path& zip);

private:
    const std::filesystem::path folder;

    mutable std::mutex mutex;
    std::map<std::string, std::shared_ptr<const SongMap>> maps;
    std::set<std::string> unticked, skipped;
    std::map<std::string, std::filesystem::path> styleFolders;
    uint64_t version = 1;

    std::filesystem::path fileFor (const SongMap&) const;

    /** Writes index.json; the caller holds the lock. */
    void saveIndex();
};

} // namespace gk::listening
