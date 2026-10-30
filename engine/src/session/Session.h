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

    ~Session();

    /** Applies commands atomically and returns the new history entry. Throws CommandError. */
    const model::HistoryEntry& execute (const std::vector<model::Command>&, const ExecuteOptions& = {});

    /** Every change is logged as it's made; song.json is rewritten at most every few hundred
        milliseconds, so a burst of small edits (a fader drag) doesn't rewrite the whole song each
        time. This writes a pending one when it's due (call it regularly, e.g. from a timer),
        or at once with force (before copying the folder, or closing). */
    void flushSave (bool force = false);

    /** Steps back one entry, or every consecutive entry of the same group. Returns false at the start. */
    bool undo (bool wholeGroup = true);

    /** Steps forward along the most recent branch. Returns false at the end. */
    bool redo (bool wholeGroup = true);

    bool canUndo() const                            { return head.has_value(); }
    bool canRedo() const                            { return findRedoTarget().has_value(); }

    const std::vector<model::HistoryEntry>& getHistory() const   { return entries; }
    std::optional<int64_t> getHead() const          { return head; }

    /** Every change is written as it's made; saving marks the point the song was saved at, which
        closing without saving goes back to. A project from before saves were marked counts as
        saved where it was opened. */
    void markSaved();
    std::optional<int64_t> getSavedHead() const     { return savedHead; }
    bool hasUnsavedChanges() const                  { return head != savedHead; }

    /** Goes back (or forward, along another branch) to where the song was last saved. The
        changes since stay in the history, so they can be redone. */
    void revertToSaved();

    /** Marks a copy of the project in another folder as saved at this session's head. */
    void markCopySaved (const std::filesystem::path& dir) const;

    using Listener = std::function<void (const Change&)>;
    int addListener (Listener);
    void removeListener (int id);

private:
    model::Json document;
    model::Project project;
    std::optional<std::filesystem::path> projectDir;

    std::vector<model::HistoryEntry> entries;   // entry id n is at index n - 1
    std::optional<int64_t> head, savedHead;

    /** The head song.json on disk holds, from the log's doc records; and whether the document
        has changed since it was written. */
    std::optional<int64_t> writtenHead;
    bool documentDirty = false;
    int64_t lastWriteMs = 0;

    std::map<int, Listener> listeners;
    int nextListenerId = 1;

    Session (model::Json document, std::filesystem::path dir);

    const model::HistoryEntry& getEntry (int64_t id) const     { return entries.at (static_cast<size_t> (id - 1)); }
    std::optional<int64_t> findRedoTarget() const;
    std::vector<int64_t> pathToRoot (std::optional<int64_t> from) const;
    /** Applies entries' patches (their inverses when going back) as one change: one reparse,
        one log record, one write, one notification. */
    void step (const std::vector<const model::HistoryEntry*>&, bool forward);
    void catchUpDocument();
    void reparse();
    void notify (const Change&);

    void loadHistory();
    void appendToLog (const model::Json& record);
    void saveDocument();
};

} // namespace gk
