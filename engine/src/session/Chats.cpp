#include "Chats.h"

#include "EngineContext.h"

#include <juce_core/juce_core.h>

#include <fstream>

namespace gk
{

using model::Json;
namespace fs = std::filesystem;

namespace
{
    // Ids name files: letters, digits and dashes only.
    bool validId (const std::string& id)
    {
        return ! id.empty() && id.size() <= 64
            && std::all_of (id.begin(), id.end(), [] (unsigned char c) { return std::isalnum (c) || c == '-'; });
    }

    void writeAtomically (const fs::path& file, const std::string& text)
    {
        fs::create_directories (file.parent_path());
        const auto temp = file.string() + ".tmp";

        {
            std::ofstream out (temp, std::ios::trunc | std::ios::binary);
            out << text;

            if (! out)
                throw std::runtime_error ("Couldn't write " + file.string());
        }

        fs::rename (temp, file);
    }

    Json toJson (const ChatStore::Summary& s)
    {
        return { { "id", s.id }, { "title", s.title }, { "project", s.project }, { "projectName", s.projectName },
                 { "created", s.created }, { "updated", s.updated }, { "messages", s.messages } };
    }

    /** The summary of a conversation: the fields the UI sets, and how many messages the user sent. */
    ChatStore::Summary summarise (const nlohmann::json& chat)
    {
        ChatStore::Summary s;
        s.id = chat.value ("id", "");
        s.title = chat.value ("title", "");
        s.project = chat.value ("project", "");
        s.projectName = chat.value ("projectName", "");
        s.created = chat.value ("created", int64_t { 0 });
        s.updated = chat.value ("updated", int64_t { 0 });

        s.messages = chat.value ("messages", 0);

        if (auto items = chat.find ("items"); items != chat.end() && items->is_array())
            for (auto& item : *items)
                if (item.value ("kind", "") == "user")
                    ++s.messages;

        return s;
    }
}

ChatStore::ChatStore (fs::path f) : folder (std::move (f)) {}

fs::path ChatStore::getDefaultFolder()
{
    if (auto env = std::getenv ("GROOVEKIT_CHATS"))
        return env;

    return EngineContext::getAppDataFolder() / "chats";
}

fs::path ChatStore::fileFor (const std::string& id) const
{
    return folder / (id + ".json");
}

std::vector<ChatStore::Summary>& ChatStore::getIndex() const
{
    if (index)
        return *index;

    index.emplace();

    // The index file, or (missing or damaged) every conversation read once to rebuild it.
    try
    {
        std::ifstream in (folder / "index.json");

        if (in)
        {
            for (auto& j : nlohmann::json::parse (in).value ("chats", nlohmann::json::array()))
                if (fs::exists (fileFor (j.value ("id", ""))))
                    index->push_back (summarise (j));

            return *index;
        }
    }
    catch (const std::exception&)
    {
        index->clear();
    }

    std::error_code ec;

    for (auto& entry : fs::directory_iterator (folder, ec))
    {
        if (entry.path().extension() != ".json" || entry.path().filename() == "index.json")
            continue;

        try
        {
            std::ifstream in (entry.path());
            index->push_back (summarise (nlohmann::json::parse (in)));
        }
        catch (const std::exception&)
        {
            // Not a conversation; left alone.
        }
    }

    saveIndex();
    return *index;
}

void ChatStore::saveIndex() const
{
    auto list = Json::array();

    for (auto& s : *index)
        list.push_back (toJson (s));

    writeAtomically (folder / "index.json", Json { { "chats", list } }.dump());
}

std::vector<ChatStore::Summary> ChatStore::list() const
{
    std::lock_guard lock (mutex);
    auto all = getIndex();
    std::sort (all.begin(), all.end(), [] (auto& a, auto& b) { return a.updated > b.updated; });
    return all;
}

std::optional<Json> ChatStore::get (const std::string& id) const
{
    if (! validId (id))
        return {};

    std::lock_guard lock (mutex);
    std::ifstream in (fileFor (id));

    if (! in)
        return {};

    // Read as plain JSON (its lookups are fast for big transcripts), handed on as the engine's.
    return Json::parse (nlohmann::json::parse (in).dump());
}

ChatStore::Summary ChatStore::save (Json chat)
{
    const auto id = chat.value ("id", "");

    if (! validId (id))
        throw std::invalid_argument ("a chat id is letters, digits and dashes");

    const auto now = juce::Time::currentTimeMillis();
    chat["updated"] = now;

    if (! chat.contains ("created"))
        chat["created"] = now;

    const auto text = chat.dump();
    const auto summary = summarise (nlohmann::json::parse (text));

    std::lock_guard lock (mutex);
    writeAtomically (fileFor (id), text);

    auto& all = getIndex();
    std::erase_if (all, [&] (auto& s) { return s.id == id; });
    all.push_back (summary);
    saveIndex();
    return summary;
}

bool ChatStore::remove (const std::string& id)
{
    if (! validId (id))
        return false;

    std::lock_guard lock (mutex);
    auto& all = getIndex();
    const auto before = all.size();
    std::erase_if (all, [&] (auto& s) { return s.id == id; });

    std::error_code ec;
    const bool removed = fs::remove (fileFor (id), ec) || all.size() != before;
    saveIndex();
    return removed;
}

} // namespace gk
