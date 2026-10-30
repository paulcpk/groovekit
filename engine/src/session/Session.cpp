#include "Session.h"

#include "Commands.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
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
        "schemaVersion": 5,
        "name": "",
        "tempo": 120,
        "channels": [],
        "patterns": [],
        "playlist": { "rows": [], "clips": [] },
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

    // A song from an older GrooveKit is converted. From before rows (5), its history is set aside:
    // its changes were recorded against the old layout and names and can't be undone in the new
    // ones.
    const auto version = doc.value ("schemaVersion", 0);
    const bool migrating = exists && migrateProject (doc);

    if (migrating && version < 5 && std::filesystem::exists (dir / historyFileName))
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
    // Logged, and never read again in memory: the patches are what undo and redo replay.
    entries.back().commands.clear();
    entries.back().commands.shrink_to_fit();
    saveDocument();
    notify ({ Change::Kind::execute, entries.back().forward, entries.back().id, head });

    return entries.back();
}

void Session::step (const std::vector<const HistoryEntry*>& steps, bool forward)
{
    if (steps.empty())
        return;

    Change change { forward ? Change::Kind::redo : Change::Kind::undo, {}, 0, {} };

    for (auto entry : steps)
    {
        const auto& ops = forward ? entry->forward : entry->inverse;
        applyPatch (document, ops);
        change.ops.insert (change.ops.end(), ops.begin(), ops.end());
        change.entry = entry->id;
        head = forward ? std::optional (entry->id) : entry->parent;
    }

    reparse();

    appendToLog ({ { "head", head ? Json (*head) : Json() } });
    saveDocument();
    change.head = head;
    notify (change);
}

bool Session::undo (bool wholeGroup)
{
    if (! head)
        return false;

    // The whole group at once: one reparse and one write for a fader gesture of thirty entries.
    std::vector<const HistoryEntry*> steps;
    const auto group = getEntry (*head).group;
    auto at = head;

    do
    {
        auto& entry = getEntry (*at);
        steps.push_back (&entry);
        at = entry.parent;
    }
    while (wholeGroup && group && at && getEntry (*at).group == group);

    step (steps, false);
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

    std::vector<const HistoryEntry*> steps;
    const auto group = getEntry (*target).group;

    while (target)
    {
        steps.push_back (&getEntry (*target));

        if (! wholeGroup || ! group)
            break;

        // The most recent branch on from here, as findRedoTarget would find it with head there.
        std::optional<int64_t> next;

        for (auto& e : entries)
            if (e.parent == target)
                next = e.id;

        if (! next || getEntry (*next).group != group)
            break;

        target = next;
    }

    step (steps, true);
    return true;
}

std::vector<int64_t> Session::pathToRoot (std::optional<int64_t> from) const
{
    std::vector<int64_t> path;

    for (auto at = from; at; at = getEntry (*at).parent)
        path.push_back (*at);

    return path;
}

void Session::markSaved()
{
    savedHead = head;
    flushSave (true);
    appendToLog ({ { "saved", head ? Json (*head) : Json() } });
}

Session::~Session()
{
    flushSave (true);
}

void Session::markCopySaved (const std::filesystem::path& dir) const
{
    std::ofstream out (dir / historyFileName, std::ios::app);
    out << Json { { "saved", head ? Json (*head) : Json() } }.dump() << "\n";
}

void Session::revertToSaved()
{
    // Back to the entry both paths share, then forward along the saved one.
    const auto fromHead = pathToRoot (head);
    const auto fromSaved = pathToRoot (savedHead);
    const std::set<int64_t> onSavedPath (fromSaved.begin(), fromSaved.end());

    std::vector<const HistoryEntry*> back;

    for (auto id : fromHead)
    {
        if (onSavedPath.contains (id))
            break;

        back.push_back (&getEntry (id));
    }

    step (back, false);

    std::vector<const HistoryEntry*> forward;

    for (auto id : fromSaved)
    {
        if (id == head)
            break;

        forward.push_back (&getEntry (id));
    }

    std::reverse (forward.begin(), forward.end());
    step (forward, true);
    flushSave (true);
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
    bool savedMarked = false, docMarked = false;

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
            else if (record.contains ("saved"))
            {
                savedHead = record["saved"].is_null() ? std::nullopt : std::optional (record["saved"].get<int64_t>());
                savedMarked = true;
            }
            else if (record.contains ("doc"))
            {
                writtenHead = record["doc"].is_null() ? std::nullopt : std::optional (record["doc"].get<int64_t>());
                docMarked = true;
            }
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error (file.string() + ":" + std::to_string (lineNumber) + ": " + e.what());
        }
    }

    if (head && *head > static_cast<int64_t> (entries.size()))
        throw std::runtime_error (file.string() + ": head points past the last entry");

    if (! savedMarked || (savedHead && *savedHead > static_cast<int64_t> (entries.size())))
        savedHead = head;

    // Logs from before documents were marked wrote song.json with every entry.
    if (! docMarked || (writtenHead && *writtenHead > static_cast<int64_t> (entries.size())))
        writtenHead = head;

    catchUpDocument();
}

void Session::catchUpDocument()
{
    // song.json is written a little after the log (flushSave): after a crash it can be a few
    // entries behind the log's head. The patches between the two bring it up to date.
    if (writtenHead == head)
        return;

    const auto fromWritten = pathToRoot (writtenHead);
    const auto fromHead = pathToRoot (head);
    const std::set<int64_t> onHeadPath (fromHead.begin(), fromHead.end());

    for (auto id : fromWritten)
    {
        if (onHeadPath.contains (id))
            break;

        applyPatch (document, getEntry (id).inverse);
    }

    const auto shared = std::find_if (fromWritten.begin(), fromWritten.end(), [&] (auto id) { return onHeadPath.contains (id); });
    const std::optional<int64_t> common = shared != fromWritten.end() ? std::optional (*shared) : std::nullopt;
    std::vector<int64_t> forward;

    for (auto id : fromHead)
    {
        if (common && id == *common)
            break;

        forward.push_back (id);
    }

    for (auto it = forward.rbegin(); it != forward.rend(); ++it)
        applyPatch (document, getEntry (*it).forward);

    reparse();
    documentDirty = true;
    flushSave (true);
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
    if (! projectDir)
        return;

    documentDirty = true;
    flushSave (false);
}

void Session::flushSave (bool force)
{
    constexpr int64_t writeEveryMs = 300;

    if (! projectDir || ! documentDirty)
        return;

    const auto now = nowMs();

    if (! force && now - lastWriteMs < writeEveryMs)
        return;

    writeFileAtomically (*projectDir / songFileName, document.dump (2) + "\n");
    documentDirty = false;
    lastWriteMs = now;

    // Which head the file holds, so a crash between writes is caught up from the log (a log
    // without any such record is from before writes were deferred: its file is current).
    writtenHead = head;
    appendToLog ({ { "doc", head ? Json (*head) : Json() } });
}

} // namespace gk
