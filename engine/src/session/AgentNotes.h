#pragma once

#include "model/JsonIO.h"

#include <filesystem>
#include <optional>

namespace gk
{

/** The user's notes for the agent: standing wishes it reads before it works ("prefer loops over
    one-shots", "techno here means ..."). A note is for every song or for one project folder, and
    can be switched off without deleting it. Kept in one JSON file (<app data>/agent-notes.json),
    read afresh on every call so engines serving different projects share it. */
class AgentNotes
{
public:
    explicit AgentNotes (std::filesystem::path file);

    static std::filesystem::path getDefaultFile();

    struct Note
    {
        std::string id, text;
        std::string project, projectName;   // empty: every song
        bool enabled = true;
        int64_t created = 0, updated = 0;   // ms since 1970
    };

    /** Oldest first. */
    std::vector<Note> list() const;

    /** Throws std::invalid_argument for empty or overlong text. */
    Note add (const std::string& text, const std::string& project = {}, const std::string& projectName = {});

    struct Change
    {
        std::optional<std::string> text;
        std::optional<bool> enabled;
        std::optional<std::pair<std::string, std::string>> project;   // folder and name; empty: every song
    };

    /** The note changed, or nothing if there's no such note. Throws std::invalid_argument for
        empty or overlong text. */
    std::optional<Note> update (const std::string& id, const Change&);

    /** Returns false if there was no such note. */
    bool remove (const std::string& id);

    static constexpr size_t maxLength = 4000;

private:
    const std::filesystem::path file;

    void save (const std::vector<Note>&) const;
};

model::Json toJson (const AgentNotes::Note&);

} // namespace gk
