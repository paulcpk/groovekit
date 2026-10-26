#pragma once

#include "Transaction.h"
#include "model/Project.h"

#include <filesystem>
#include <functional>

namespace gk
{

struct ExecuteOptions
{
    std::string origin = "cli";
    std::optional<std::string> group;
    std::optional<std::string> label;
};

/** What listeners receive after the document changed. Applying ops to the previous
    document gives the new one, so clients can keep a mirror in sync.
*/
struct Change
{
    enum class Kind { execute, undo, redo };

    Kind kind;
    std::vector<model::PatchOp> ops;
    int64_t entry;                  // the entry applied (execute, redo) or undone (undo)
    std::optional<int64_t> head;    // the history position afterwards
};

/** An open project: the song document, its parsed model and the history tree.

    Every change goes through execute(), which applies the commands as one transaction,
    validates the result (schema and cross-references) and either records a history
    entry or rolls back. With a project folder, song.json is rewritten after each change
    and history.jsonl is appended to, so the session survives restarts.
*/
class Session
{
public:
    /** Opens dir/song.json and dir/history.jsonl, creating an empty project if there is none. */
    static std::unique_ptr<Session> open (const std::filesystem::path& dir);

    /** An in-memory session, mainly for tests. Throws if the document is invalid. */
    explicit Session (model::Json document);

    static model::Json createEmptyProject();

    const model::Json& getDocument() const          { return document; }
    const model::Project& getProject() const        { return project; }
    const std::optional<std::filesystem::path>& getProjectDir() const { return projectDir; }

    /** Applies commands atomically and returns the new history entry. Throws CommandError. */
    const model::HistoryEntry& execute (const std::vector<model::Command>&, const ExecuteOptions& = {});

    /** Steps back one entry, or every consecutive entry of the same group. Returns false at the start. */
    bool undo (bool wholeGroup = true);

    /** Steps forward along the most recent branch. Returns false at the end. */
    bool redo (bool wholeGroup = true);

    bool canUndo() const                            { return head.has_value(); }
    bool canRedo() const                            { return findRedoTarget().has_value(); }

    const std::vector<model::HistoryEntry>& getHistory() const   { return entries; }
    std::optional<int64_t> getHead() const          { return head; }

    using Listener = std::function<void (const Change&)>;
    int addListener (Listener);
    void removeListener (int id);

private:
    model::Json document;
    model::Project project;
    std::optional<std::filesystem::path> projectDir;

    std::vector<model::HistoryEntry> entries;   // entry id n is at index n - 1
    std::optional<int64_t> head;

    std::map<int, Listener> listeners;
    int nextListenerId = 1;

    Session (model::Json document, std::filesystem::path dir);

    const model::HistoryEntry& getEntry (int64_t id) const     { return entries.at (static_cast<size_t> (id - 1)); }
    std::optional<int64_t> findRedoTarget() const;
    void step (const model::HistoryEntry&, bool forward);
    void reparse();
    void notify (const Change&);

    void loadHistory();
    void appendToLog (const model::Json& record);
    void saveDocument();
};

} // namespace gk
