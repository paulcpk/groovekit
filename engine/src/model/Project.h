#pragma once

#include "Project.generated.h"

#include <filesystem>

namespace gk::model
{

/** Ticks per quarter note for every position and duration in the model. */
inline constexpr int64_t ticksPerQuarter = 960;

inline constexpr const char* masterInsertId = "master";

int64_t ticksPerBar (const TimeSignature&);

/** The pattern's loop length: its explicit length, or the end of its last note rounded up to a whole bar. */
TickLength getPatternLength (const Pattern&, const TimeSignature&);

/** The end of the last clip, in ticks. */
Ticks getSongEnd (const Project&);

//==============================================================================
/** Parses and schema-checks a document. Throws ReadError. */
Project parseProject (const Json&);

/** Reads and schema-checks a song.json file. Throws ReadError or std::runtime_error. */
Project loadProject (const std::filesystem::path& songFile);

Json projectToJson (const Project&);
void saveProject (const Project&, const std::filesystem::path& songFile);

//==============================================================================
/** A problem the schema can't express: dangling references, duplicate ids, routing cycles. */
struct ValidationIssue
{
    std::string path;    // JSON pointer
    std::string message;
};

std::vector<ValidationIssue> validate (const Project&);

/** Throws std::runtime_error listing every issue, if there are any. */
void validateOrThrow (const Project&);

//==============================================================================
const Channel* findChannel (const Project&, const Id&);
const Pattern* findPattern (const Project&, const Id&);
const PlaylistTrack* findTrack (const Project&, const Id&);
const Insert* findInsert (const Project&, const Id&);

const Id& getClipId (const Clip&);
const Id& getClipTrack (const Clip&);
Ticks getClipStart (const Clip&);
TickLength getClipLength (const Clip&);
bool isClipMuted (const Clip&);

const Id& getEffectId (const Effect&);

} // namespace gk::model
