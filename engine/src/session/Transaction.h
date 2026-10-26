#pragma once

#include "model/Protocol.generated.h"

#include <stdexcept>

namespace gk
{

/** A command that can't be applied. Nothing has changed when this is thrown. */
struct CommandError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** Escapes a key for use as one JSON Pointer segment. */
std::string pointerSegment (const std::string& key);

/** Applies JSON Patch operations (add, remove, replace) to a document in place, recording
    each forward operation and its inverse so the whole set can be undone or rolled back.
*/
class Transaction
{
public:
    explicit Transaction (model::Json& document);

    void apply (const model::PatchOp&);

    void add (const std::string& path, model::Json value);
    void remove (const std::string& path);
    void replace (const std::string& path, model::Json value);

    const model::Json& document() const   { return doc; }

    /** The applied operations, in order. */
    const std::vector<model::PatchOp>& getForward() const   { return forward; }

    /** Operations that restore the original document, in the order they must be applied. */
    std::vector<model::PatchOp> getInverse() const;

    /** Restores the document to how it was before the first operation. */
    void rollback();

private:
    model::Json& doc;
    std::vector<model::PatchOp> forward, inverse;
};

/** Applies operations without recording them (for undo and redo, whose inverses are already known). */
void applyPatch (model::Json& document, const std::vector<model::PatchOp>&);

} // namespace gk
