#pragma once

#include "Transaction.h"

namespace gk
{

/** Applies one command to the document through the transaction. Throws CommandError.
    The result isn't validated here; the caller checks the whole project afterwards.
*/
void applyCommand (Transaction&, const model::Command&);

/** A short human-readable description, e.g. "Update channel kick". */
std::string describeCommand (const model::Command&);

} // namespace gk
