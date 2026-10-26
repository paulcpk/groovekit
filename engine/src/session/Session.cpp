#include "Session.h"

#include "Commands.h"

#include <chrono>
#include <fstream>
#include <sstream>

namespace gk
{

using namespace model;

namespace
{
    constexpr const char* songFileName = "song.json";
    constexpr const char* historyFileName = "history.jsonl";

    Project parseAndValidate (const Json& doc)
    {
        Project project;

        try
        {
            project = parseProject (doc);
        }
        catch (const ReadError& e)
        {
            throw CommandError (e.what());
        }

        auto issues = validate (project);

        if (! issues.empty())
        {
            std::string message;

            for (auto& i : issues)
                message += (message.empty() ? "" : "; ") + i.path + ": " + i.message;

            throw CommandError (message);
        }

        return project;
    }

    int64_t nowMs()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds> (system_clock::now().time_since_epoch()).count();
    }

    Json readJsonFile (const std::filesystem::path& file)
    {
        std::ifstream in (file);

        if (! in)
            throw std::runtime_error ("Can't open " + file.string());

        try
        {
            return Json::parse (in);
        }
        catch (const Json::parse_error& e)
        {
            throw std::runtime_error (file.string() + ": " + e.what());
        }
    }

    /** Writes via a temporary file so a crash never leaves a half-written song. */
    void writeFileAtomically (const std::filesystem::path& file, const std::string& content)
    {
        auto temp = file;
        temp += ".tmp";

        {
            std::ofstream out (temp, std::ios::trunc);

            if (! out)
                throw std::runtime_error ("Can't write " + temp.string());

            out << content;
        }

        std::filesystem::rename (temp, file);
    }
}

//==============================================================================
Json Session::createEmptyProject()
{
    return Json::parse (R"({
        "schemaVersion": 2,
        "name": "",
        "tempo": 120,
        "channels": [],
        "patterns": [],
        "playlist": { "tracks": [], "clips": [] },
        "mixer": { "inserts": [ { "id": "master", "name": "Master" } ] }
    })");
}

namespace
{
    Json migrated (Json doc)
    {
        migrateProject (doc);
        return doc;
    }
}

Session::Session (Json doc)
    : document (migrated (std::move (doc))), project (parseAndValidate (document))
{
}

Session::Session (Json doc, std::filesystem::path dir)
    : document (migrated (std::move (doc))), project (parseAndValidate (document)), projectDir (std::move (dir))
{
}

std::unique_ptr<Session> Session::open (const std::filesystem::path& dir)
{
    std::filesystem::create_directories (dir);
    const auto songFile = dir / songFileName;
    const bool exists = std::filesystem::exists (songFile);

    auto doc = exists ? readJsonFile (songFile) : createEmptyProject();

    // A song from an older GrooveKit is converted, and its history set aside: its changes were
    // recorded against the old layout and can't be undone in the new one.
    const bool migrating = exists && migrateProject (doc);

    if (migrating && std::filesystem::exists (dir / historyFileName))
    {
        std::error_code ec;
        std::filesystem::rename (dir / historyFileName, dir / "history-before-update.jsonl", ec);
    }

    std::unique_ptr<Session> session;

    try
    {
        session.reset (new Session (std::move (doc), dir));
    }
    catch (const CommandError& e)
    {
        throw std::runtime_error (songFile.string() + " is invalid: " + e.what());
    }

    if (! exists || migrating)
        session->saveDocument();

    session->loadHistory();
    return session;
}

//==============================================================================
const HistoryEntry& Session::execute (const std::vector<Command>& commands, const ExecuteOptions& options)
{
    if (commands.empty())
        throw CommandError ("no commands");

    Transaction tx (document);

    try
    {
        for (size_t i = 0; i < commands.size(); ++i)
        {
            try
            {
                applyCommand (tx, commands[i]);
            }
            catch (const CommandError& e)
            {
                throw CommandError (commands.size() > 1 ? "command " + std::to_string (i) + ": " + e.what() : e.what());
            }
        }

        project = parseAndValidate (document);
    }
    catch (...)
    {
        tx.rollback();
        throw;
    }

    HistoryEntry entry;
    entry.id = static_cast<int64_t> (entries.size()) + 1;
    entry.parent = head;
    entry.time = nowMs();
    entry.origin = options.origin;
    entry.group = options.group;
    entry.commands = commands;
    entry.forward = tx.getForward();
    entry.inverse = tx.getInverse();
    entry.label = options.label.value_or (describeCommand (commands.front())
                                          + (commands.size() > 1 ? " and " + std::to_string (commands.size() - 1) + " more" : ""));

    entries.push_back (std::move (entry));
    head = entries.back().id;

    appendToLog ({ { "entry", toJson (entries.back()) } });
    saveDocument();
    notify ({ Change::Kind::execute, entries.back().forward, entries.back().id, head });

    return entries.back();
}

void Session::step (const HistoryEntry& entry, bool forward)
{
    applyPatch (document, forward ? entry.forward : entry.inverse);
    head = forward ? std::optional (entry.id) : entry.parent;
    reparse();

    appendToLog ({ { "head", head ? Json (*head) : Json() } });
    saveDocument();
    notify ({ forward ? Change::Kind::redo : Change::Kind::undo, forward ? entry.forward : entry.inverse, entry.id, head });
}

bool Session::undo (bool wholeGroup)
{
    if (! head)
        return false;

    const auto group = getEntry (*head).group;
    step (getEntry (*head), false);

    while (wholeGroup && group && head && getEntry (*head).group == group)
        step (getEntry (*head), false);

    return true;
}

std::optional<int64_t> Session::findRedoTarget() const
{
    std::optional<int64_t> latest;

    for (auto& e : entries)
        if (e.parent == head)
            latest = e.id;

    return latest;
}

bool Session::redo (bool wholeGroup)
{
    auto target = findRedoTarget();

    if (! target)
        return false;

    const auto group = getEntry (*target).group;
    step (getEntry (*target), true);

    while (wholeGroup && group)
    {
        target = findRedoTarget();

        if (! target || getEntry (*target).group != group)
            break;

        step (getEntry (*target), true);
    }

    return true;
}

void Session::reparse()
{
    // History only replays patches that were valid when recorded, so this can't fail
    // unless song.json and history.jsonl were edited by hand.
    project = parseAndValidate (document);
}

//==============================================================================
int Session::addListener (Listener l)
{
    listeners[nextListenerId] = std::move (l);
    return nextListenerId++;
}

void Session::removeListener (int id)
{
    listeners.erase (id);
}

void Session::notify (const Change& change)
{
    for (auto& [id, l] : listeners)
        l (change);
}

//==============================================================================
void Session::loadHistory()
{
    const auto file = *projectDir / historyFileName;
    std::ifstream in (file);

    if (! in)
        return;

    std::string line;
    int lineNumber = 0;

    while (std::getline (in, line))
    {
        ++lineNumber;

        if (line.empty())
            continue;

        try
        {
            auto record = Json::parse (line);

            if (record.contains ("entry"))
            {
                Reader reader;
                HistoryEntry entry;
                readValue (reader, record["entry"], entry);

                if (entry.id != static_cast<int64_t> (entries.size()) + 1)
                    throw std::runtime_error ("entries are out of order");

                entries.push_back (std::move (entry));
                head = entries.back().id;
            }
            else if (record.contains ("head"))
            {
                head = record["head"].is_null() ? std::nullopt : std::optional (record["head"].get<int64_t>());
            }
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error (file.string() + ":" + std::to_string (lineNumber) + ": " + e.what());
        }
    }

    if (head && *head > static_cast<int64_t> (entries.size()))
        throw std::runtime_error (file.string() + ": head points past the last entry");
}

void Session::appendToLog (const Json& record)
{
    if (! projectDir)
        return;

    std::ofstream out (*projectDir / historyFileName, std::ios::app);
    out << record.dump() << "\n";
}

void Session::saveDocument()
{
    if (projectDir)
        writeFileAtomically (*projectDir / songFileName, document.dump (2) + "\n");
}

} // namespace gk
