#pragma once

#include "model/Project.h"

#include <filesystem>

namespace gk::presets
{

/** Presets for an effect kind ("eq", "reverb", ...) or for channels ("sampler": a filter and a
    volume envelope). An effect preset is { "name", "params", "mix"? }; a channel preset is
    { "name", "filter"?, "envelope"? }. Factory presets ship with GrooveKit; the user's are
    files in <folder>/<kind>/<name>.json. */

/** Every factory preset, by kind. */
const model::Json& factory();

/** The factory and user presets of a kind: { "factory": [...], "user": [...] }. */
model::Json list (const std::string& kind, const std::filesystem::path& folder);

/** Saves (or replaces) a user preset; the name is cleaned to make the file name. */
void save (const std::string& kind, const model::Json& preset, const std::filesystem::path& folder);

void remove (const std::string& kind, const std::string& name, const std::filesystem::path& folder);

} // namespace gk::presets
