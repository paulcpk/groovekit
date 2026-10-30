#pragma once

#include "model/JsonIO.h"

#include <filesystem>
#include <map>
#include <optional>

namespace gk::guides
{

/** The guides GrooveKit ships with (the .md files of the guides folder, built in): { id, text }. */
const std::vector<std::pair<std::string, std::string>>& builtInTexts();

/** The parts of a song a guide talks about, and that a review sorts channels into. */
const std::vector<std::string>& roleNames();

/** What a style aims for, in numbers a review can check. Levels are each role's loudness while
    it sounds, in dB against the kick's (or, with no kick, the loudest drums'). */
struct Targets
{
    struct Range
    {
        double min = 0, max = 0;
    };

    std::optional<Range> tempo, swing, lowVsHigh, highHitsPerBeat, loudness;
    std::optional<int> partsAtOnce, topAtOnce;
    std::map<std::string, Range> levels;
};

/** How to make one style: what it should feel like, what each part should sound like, how to
    mix and arrange it and what to avoid (Markdown), with targets in a ```targets block (JSON). */
struct Guide
{
    std::string id, name, summary, text;
    Targets targets;
    bool builtIn = false, edited = false;
    bool hidden = false;        // a built-in guide the user took out of the list
};

/** Reads a guide: its name from the first "# " heading, its summary from the first paragraph
    after it, its targets from the ```targets block. Throws std::invalid_argument if the
    targets aren't valid. */
Guide parse (const std::string& id, const std::string& text);

model::Json toJson (const Targets&);
model::Json toJson (const Guide&, bool withText);

/** The built-in guides, with the user's edits (a copy of their own in a folder, <id>.md, in
    place of the built-in one) and guides of their own. Reads the folder afresh on every call. */
class GuideStore
{
public:
    explicit GuideStore (std::filesystem::path folder);

    static std::filesystem::path getDefaultFolder();

    /** Every guide, by name. */
    std::vector<Guide> list() const;

    /** By id or name, ignoring case and punctuation ("Drum & Bass", "drum-and-bass"). */
    std::optional<Guide> find (const std::string& idOrName) const;

    /** The same among guides listed already, for looking many names up from one reading. */
    static const Guide* findIn (const std::vector<Guide>&, const std::string& idOrName);

    /** Saves the user's version of a guide (a new one without an id: the id comes from its
        name). Throws std::invalid_argument for a bad id, empty text or invalid targets. */
    Guide save (const std::string& id, const std::string& text);

    enum class Removed { nothing, restored, deleted };

    /** Removes the user's version: an edited built-in guide goes back to how it shipped, one of
        their own is deleted. */
    Removed remove (const std::string& id);

    /** Takes a built-in guide out of the list, or puts it back (hidden.json in the folder). A
        hidden guide is still found by find, so songs in its style keep it. */
    void setHidden (const std::string& id, bool);

private:
    const std::filesystem::path folder;
};

} // namespace gk::guides
