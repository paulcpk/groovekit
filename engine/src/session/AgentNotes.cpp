#include "AgentNotes.h"

#include "EngineContext.h"

#include <juce_core/juce_core.h>

#include <fstream>
#include <mutex>

namespace gk
{

using model::Json;
namespace fs = std::filesystem;

namespace
{
    std::mutex fileMutex;   // one engine's calls, one after another

    int64_t now()   { return juce::Time::currentTimeMillis(); }

    std::string checked (const std::string& text)
    {
        const auto trimmed = juce::String (text).trim().toStdString();

        if (trimmed.empty())
            throw std::invalid_argument ("A note needs some text");

        if (trimmed.size() > AgentNotes::maxLength)
            throw std::invalid_argument ("A note can be at most " + std::to_string (AgentNotes::maxLength) + " characters");

        return trimmed;
    }

    AgentNotes::Note fromJson (const Json& j)
    {
        AgentNotes::Note n;
        n.id = j.value ("id", "");
        n.text = j.value ("text", "");
        n.project = j.value ("project", "");
        n.projectName = j.value ("projectName", "");
        n.enabled = j.value ("enabled", true);
        n.created = j.value ("created", int64_t { 0 });
        n.updated = j.value ("updated", n.created);
        return n;
    }
}

Json toJson (const AgentNotes::Note& n)
{
    Json j { { "id", n.id }, { "text", n.text }, { "enabled", n.enabled }, { "created", n.created }, { "updated", n.updated } };

    if (! n.project.empty())
    {
        j["project"] = n.project;
        j["projectName"] = n.projectName;
    }

    return j;
}

AgentNotes::AgentNotes (fs::path f) : file (std::move (f)) {}

fs::path AgentNotes::getDefaultFile()
{
    if (auto env = std::getenv ("GROOVEKIT_NOTES"))
        return env;

    return EngineContext::getAppDataFolder() / "agent-notes.json";
}

std::vector<AgentNotes::Note> AgentNotes::list() const
{
    std::lock_guard lock (fileMutex);
    std::vector<Note> notes;
    std::ifstream in (file);

    if (! in)
        return notes;

    try
    {
        auto j = Json::parse (in);

        if (j.contains ("notes") && j["notes"].is_array())
            for (auto& n : j["notes"])
                if (n.is_object() && n.contains ("id") && n.contains ("text"))
                    notes.push_back (fromJson (n));
    }
    catch (const Json::exception&)
    {
        // A damaged file reads as no notes; the next change writes a good one.
    }

    return notes;
}

void AgentNotes::save (const std::vector<Note>& notes) const
{
    auto list = Json::array();

    for (auto& n : notes)
        list.push_back (toJson (n));

    fs::create_directories (file.parent_path());
    const auto temp = file.string() + ".tmp";

    {
        std::ofstream out (temp, std::ios::trunc | std::ios::binary);
        out << Json { { "notes", list } }.dump (2);

        if (! out)
            throw std::runtime_error ("Couldn't write " + file.string());
    }

    fs::rename (temp, file);
}

AgentNotes::Note AgentNotes::add (const std::string& text, const std::string& project, const std::string& projectName)
{
    Note n;
    n.text = checked (text);
    n.project = project;
    n.projectName = project.empty() ? std::string() : projectName;
    n.created = n.updated = now();
    n.id = "n" + juce::Uuid().toString().substring (0, 10).toStdString();

    auto notes = list();
    std::lock_guard lock (fileMutex);
    notes.push_back (n);
    save (notes);
    return n;
}

std::optional<AgentNotes::Note> AgentNotes::update (const std::string& id, const Change& change)
{
    const auto text = change.text ? std::optional (checked (*change.text)) : std::nullopt;
    auto notes = list();
    std::lock_guard lock (fileMutex);
    auto it = std::find_if (notes.begin(), notes.end(), [&] (auto& n) { return n.id == id; });

    if (it == notes.end())
        return {};

    if (text)               it->text = *text;
    if (change.enabled)     it->enabled = *change.enabled;

    if (change.project)
    {
        it->project = change.project->first;
        it->projectName = it->project.empty() ? std::string() : change.project->second;
    }

    it->updated = now();
    save (notes);
    return *it;
}

bool AgentNotes::remove (const std::string& id)
{
    auto notes = list();
    std::lock_guard lock (fileMutex);
    const auto before = notes.size();
    std::erase_if (notes, [&] (auto& n) { return n.id == id; });

    if (notes.size() == before)
        return false;

    save (notes);
    return true;
}

} // namespace gk
