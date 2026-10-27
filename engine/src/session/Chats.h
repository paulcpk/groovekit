#pragma once

#include "model/JsonIO.h"

#include <filesystem>
#include <mutex>
#include <optional>

namespace gk
{

/** The in-app agent's conversations, one JSON file each in a folder (<app data>/chats), with a
    small index of what each is so listing them doesn't read them all. A conversation is opaque
    here: the UI's chat items and API transcript, plus a few fields the store reads. Thread-safe. */
class ChatStore
{
public:
    explicit ChatStore (std::filesystem::path folder);

    static std::filesystem::path getDefaultFolder();

    /** What the list of conversations shows. */
    struct Summary
    {
        std::string id, title;
        std::string project, projectName;   // where it was last used
        int64_t created = 0, updated = 0;   // ms since 1970
        int messages = 0;                    // the user's
    };

    /** Most recently updated first. */
    std::vector<Summary> list() const;

    /** The whole conversation as saved, or nothing. */
    std::optional<model::Json> get (const std::string& id) const;

    /** Saves a conversation: { id, title?, project?, projectName?, created?, items, history }.
        Its updated time is now. Throws std::invalid_argument for a bad id. */
    Summary save (model::Json chat);

    /** Returns false if there was no such conversation. */
    bool remove (const std::string& id);

private:
    const std::filesystem::path folder;
    mutable std::mutex mutex;
    mutable std::optional<std::vector<Summary>> index;   // loaded on first use

    std::vector<Summary>& getIndex() const;             // with the mutex held
    void saveIndex() const;                             // with the mutex held
    std::filesystem::path fileFor (const std::string& id) const;
};

} // namespace gk
