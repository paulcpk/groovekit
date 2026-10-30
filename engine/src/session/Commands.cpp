#include "Commands.h"
#include "guides/Guides.h"
#include "music/Keys.h"

#include "model/Project.h"

#include <sstream>

namespace gk
{

using namespace model;

namespace
{
    std::string join (const std::string& path, const std::string& key)
    {
        return path + "/" + pointerSegment (key);
    }

    std::string join (const std::string& path, size_t index)
    {
        return path + "/" + std::to_string (index);
    }

    /** Wraps a document under construction with id lookups and the cascade helpers. */
    class Editor
    {
    public:
        explicit Editor (Transaction& t) : tx (t) {}

        const Json& doc() const { return tx.document(); }

        const Json& at (const std::string& path) const
        {
            return doc().at (Json::json_pointer (path));
        }

        bool has (const std::string& path) const
        {
            return doc().contains (Json::json_pointer (path));
        }

        /** The array at path, or an empty one if it isn't there. */
        const Json& array (const std::string& path) const
        {
            static const Json empty = Json::array();
            return has (path) && at (path).is_array() ? at (path) : empty;
        }

        size_t indexOf (const std::string& arrayPath, const Id& id, const char* what) const
        {
            auto& items = array (arrayPath);

            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].is_object() && items[i].value ("id", "") == id)
                    return i;

            throw CommandError (std::string ("no ") + what + " \"" + id + "\"");
        }

        std::string channelPath (const Id& id) const   { return join ("/channels", indexOf ("/channels", id, "channel")); }
        std::string patternPath (const Id& id) const   { return join ("/patterns", indexOf ("/patterns", id, "pattern")); }
        std::string clipPath (const Id& id) const      { return join ("/playlist/clips", indexOf ("/playlist/clips", id, "clip")); }
        std::string insertPath (const Id& id) const    { return join ("/mixer/inserts", indexOf ("/mixer/inserts", id, "mixer insert")); }

        std::string effectPath (const Id& insert, const Id& effect) const
        {
            auto base = insertPath (insert) + "/effects";
            return join (base, indexOf (base, effect, "effect"));
        }

        std::string sendPath (const Id& insert, const Id& send) const
        {
            auto base = insertPath (insert) + "/sends";
            return join (base, indexOf (base, send, "send"));
        }

        std::string layerPath (const Id& channel, const Id& layer) const
        {
            auto base = channelPath (channel) + "/instrument/layers";

            if (! has (base))
                throw CommandError ("channel \"" + channel + "\" isn't a sampler");

            return join (base, indexOf (base, layer, "sampler layer"));
        }

        void ensureUnique (const std::string& arrayPath, const Json& item, const char* what) const
        {
            auto id = item.value ("id", "");

            for (auto& existing : array (arrayPath))
                if (existing.value ("id", "") == id)
                    throw CommandError (std::string ("there is already a ") + what + " \"" + id + "\"");
        }

        /** Inserts at index (or appends), creating the array if it's missing. */
        void insertAt (const std::string& arrayPath, Json item, const std::optional<int>& index)
        {
            if (! has (arrayPath))
                tx.add (arrayPath, Json::array());

            auto size = at (arrayPath).size();

            if (index && static_cast<size_t> (*index) > size)
                throw CommandError ("index " + std::to_string (*index) + " is past the end (" + std::to_string (size) + " items)");

            tx.add (index ? join (arrayPath, static_cast<size_t> (*index)) : arrayPath + "/-", std::move (item));
        }

        /** Applies a changes object: each field replaces the old value, null removes an optional field. */
        void applyChanges (const std::string& path, const Json& changes, std::initializer_list<const char*> fixed = {})
        {
            if (! changes.is_object())
                throw CommandError ("changes must be an object");

            for (auto& [key, value] : changes.items())
            {
                if (key == "id")
                    throw CommandError ("ids can't be changed");

                for (auto f : fixed)
                    if (key == f)
                        throw CommandError ("\"" + key + "\" can't be changed with this command");

                auto fieldPath = join (path, key);

                if (value.is_null())
                {
                    if (has (fieldPath))
                        tx.remove (fieldPath);
                }
                else
                {
                    tx.add (fieldPath, value);
                }
            }
        }

        /** Removes array items matching the predicate, last first so indices stay valid. */
        template <typename Predicate>
        void removeWhere (const std::string& arrayPath, Predicate&& matches)
        {
            // Removing an item keeps the array (and the items before it) where they are.
            auto& items = array (arrayPath);

            for (auto i = items.size(); i-- > 0;)
                if (matches (items[i]))
                    tx.remove (join (arrayPath, i));
        }

        /** Removes automation clips whose target matches. */
        template <typename Predicate>
        void removeAutomation (Predicate&& targets)
        {
            removeWhere ("/playlist/clips", [&] (const Json& clip)
            {
                return clip.value ("type", "") == "automation" && clip.contains ("target") && targets (clip["target"]);
            });
        }

        /** Points a reference field at another id wherever it currently names from. */
        template <typename Predicate>
        void reroute (const std::string& arrayPath, const char* field, const Id& from, const Id& to, Predicate&& applies)
        {
            auto& items = array (arrayPath);

            for (size_t i = 0; i < items.size(); ++i)
                if (applies (items[i]) && items[i].contains (field) && items[i][field] == from)
                    tx.replace (join (join (arrayPath, i), field), to);
        }

        Transaction& tx;
    };

    //==============================================================================
    struct Apply
    {
        Editor e;

        void operator() (const SetTempo& c)             { e.tx.add ("/tempo", c.tempo); }

        void operator() (const SetKey& c)
        {
            if (! c.key)
            {
                if (e.has ("/key"))
                    e.tx.remove ("/key");

                return;
            }

            const auto key = music::parseKey (*c.key);

            if (! key)
                throw CommandError ("\"" + *c.key + "\" isn't a key; name one like \"A minor\" or \"F# major\"");

            e.tx.add ("/key", music::keyName (*key));
        }
        void operator() (const SetStyle& c)
        {
            if (! c.style)
            {
                if (e.has ("/style"))
                    e.tx.remove ("/style");

                return;
            }

            guides::GuideStore store (guides::GuideStore::getDefaultFolder());
            const auto guide = store.find (*c.style);

            if (! guide)
            {
                std::string names;

                for (auto& g : store.list())
                    names += (names.empty() ? "" : ", ") + g.id;

                throw CommandError ("No style guide \"" + *c.style + "\"; there are " + names);
            }

            e.tx.add ("/style", guide->id);
        }

        void operator() (const SetTimeSignature& c)     { e.tx.add ("/timeSignature", toJson (c.timeSignature)); }
        void operator() (const SetSwing& c)             { e.tx.add ("/swing", c.swing); }
        void operator() (const RenameProject& c)        { e.tx.add ("/name", c.name); }

        void operator() (const AddChannel& c)
        {
            auto item = toJson (c.channel);
            e.ensureUnique ("/channels", item, "channel");
            e.insertAt ("/channels", std::move (item), c.index);
        }

        void operator() (const UpdateChannel& c)
        {
            e.applyChanges (e.channelPath (c.channel), c.changes);
        }

        void operator() (const RemoveChannel& c)
        {
            auto path = e.channelPath (c.channel);
            auto& patterns = e.array ("/patterns");

            for (size_t p = 0; p < patterns.size(); ++p)
                e.removeWhere (join (join ("/patterns", p), "notes"),
                               [&] (const Json& note) { return note.value ("channel", "") == c.channel; });

            e.removeAutomation ([&] (const Json& t) { return t.value ("scope", "") == "channel" && t.value ("channel", "") == c.channel; });

            // So do the audio clips that play it.
            e.removeWhere ("/playlist/clips", [&] (const Json& clip)
            {
                return clip.value ("type", "") == "audio" && clip.value ("channel", "") == c.channel;
            });

            e.tx.remove (e.channelPath (c.channel));
        }

        void operator() (const UpdateInstrument& c)
        {
            auto base = e.channelPath (c.channel) + "/instrument";

            if (e.at (base).value ("type", "") == "plugin")
                throw CommandError ("channel \"" + c.channel + "\" plays a plugin: its settings are the plugin's");

            e.applyChanges (base, c.changes, { "type", "layers" });
        }

        void operator() (const AddSamplerLayer& c)
        {
            auto base = e.channelPath (c.channel) + "/instrument";

            if (e.at (base).value ("type", "") != "sampler")
                throw CommandError ("channel \"" + c.channel + "\" isn't a sampler");

            auto item = toJson (c.layer);
            e.ensureUnique (base + "/layers", item, "sampler layer");
            e.insertAt (base + "/layers", std::move (item), c.index);
        }

        void operator() (const UpdateSamplerLayer& c)   { e.applyChanges (e.layerPath (c.channel, c.layer), c.changes); }
        void operator() (const RemoveSamplerLayer& c)   { e.tx.remove (e.layerPath (c.channel, c.layer)); }

        void operator() (const AddPattern& c)
        {
            auto item = toJson (c.pattern);
            e.ensureUnique ("/patterns", item, "pattern");
            e.insertAt ("/patterns", std::move (item), c.index);
        }

        void operator() (const UpdatePattern& c)
        {
            e.applyChanges (e.patternPath (c.pattern), c.changes);
        }

        void operator() (const RemovePattern& c)
        {
            auto path = e.patternPath (c.pattern);
            e.removeWhere ("/playlist/clips", [&] (const Json& clip)
            {
                return clip.value ("type", "") == "pattern" && clip.value ("pattern", "") == c.pattern;
            });
            e.tx.remove (path);
        }

        void operator() (const AddNotes& c)
        {
            auto notes = e.patternPath (c.pattern) + "/notes";

            for (auto& note : c.notes)
                e.insertAt (notes, toJson (note), {});
        }

        void operator() (const RemoveNotes& c)
        {
            e.removeWhere (e.patternPath (c.pattern) + "/notes", [&] (const Json& note)
            {
                const auto start = note.value ("start", int64_t { 0 });

                return (! c.channel || note.value ("channel", "") == *c.channel)
                    && (! c.key || note.value ("key", 60) == *c.key)
                    && (! c.from || start >= *c.from)
                    && (! c.to || start < *c.to);
            });
        }

        void operator() (const SetNotes& c)
        {
            for (auto& note : c.notes)
                if (note.channel != c.channel)
                    throw CommandError ("setNotes for channel \"" + c.channel + "\" got a note for \"" + note.channel + "\"");

            // The whole array once (the other channels' notes as they are, this channel's new
            // ones after them): two copies of the array in the history, not two ops per note.
            auto path = e.patternPath (c.pattern) + "/notes";
            auto notes = Json::array();

            if (e.has (path))
                for (auto& note : e.array (path))
                    if (note.value ("channel", "") != c.channel)
                        notes.push_back (note);

            for (auto& note : c.notes)
                notes.push_back (toJson (note));

            if (e.has (path))
                e.tx.replace (path, std::move (notes));
            else
                e.tx.add (path, std::move (notes));
        }

        /** Moves the clips in rows from `from` down by `by` (up when negative). */
        void shiftClips (Row from, int by)
        {
            auto& clips = e.array ("/playlist/clips");

            for (size_t i = 0; i < clips.size(); ++i)
            {
                const auto row = clips[i].value ("row", 0);

                if (row < from)
                    continue;

                if (row + by > maxRow)
                    throw CommandError ("row " + std::to_string (maxRow) + " is the last; its clips can't move further down");

                e.tx.replace (join (join ("/playlist/clips", i), "row"), row + by);
            }
        }

        static constexpr Row maxRow = 999;

        void operator() (const InsertRow& c)
        {
            shiftClips (c.row, 1);

            if (static_cast<size_t> (c.row) < e.array ("/playlist/rows").size())
                e.insertAt ("/playlist/rows", Json::object(), c.row);
        }

        void operator() (const UpdateRow& c)
        {
            // Rows before it that have no settings yet get the defaults.
            while (e.array ("/playlist/rows").size() <= static_cast<size_t> (c.row))
                e.insertAt ("/playlist/rows", Json::object(), {});

            e.applyChanges (join ("/playlist/rows", static_cast<size_t> (c.row)), c.changes);
        }

        void operator() (const RemoveRow& c)
        {
            e.removeWhere ("/playlist/clips", [&] (const Json& clip) { return clip.value ("row", 0) == c.row; });
            shiftClips (c.row + 1, -1);

            if (static_cast<size_t> (c.row) < e.array ("/playlist/rows").size())
                e.tx.remove (join ("/playlist/rows", static_cast<size_t> (c.row)));
        }

        void operator() (const MoveRows& c)
        {
            const int from = c.row, count = c.count, to = c.to;

            if (to + count - 1 > maxRow)
                throw CommandError ("row " + std::to_string (maxRow) + " is the last; these rows can't move further down");

            if (from == to)
                return;

            // Where each row goes: the moved block lands at `to`, the rows it passes close the gap.
            auto moved = [&] (int row)
            {
                if (row >= from && row < from + count)  return to + (row - from);
                if (to > from && row >= from + count && row < to + count)  return row - count;
                if (to < from && row >= to && row < from)  return row + count;
                return row;
            };

            auto& clips = e.array ("/playlist/clips");

            for (size_t i = 0; i < clips.size(); ++i)
                if (const auto row = clips[i].value ("row", 0); moved (row) != row)
                    e.tx.replace (join (join ("/playlist/clips", i), "row"), moved (row));

            // The rows' settings move with them; rows without settings have the defaults.
            const auto& rows = e.array ("/playlist/rows");
            const auto size = std::max ({ rows.size(), static_cast<size_t> (from + count), static_cast<size_t> (to + count) });
            auto reordered = Json::array();

            for (size_t i = 0; i < size; ++i)
                reordered.push_back (Json::object());

            for (size_t i = 0; i < rows.size(); ++i)
                reordered[static_cast<size_t> (moved (static_cast<int> (i)))] = rows[i];

            while (! reordered.empty() && reordered.back().empty())
                reordered.erase (reordered.size() - 1);

            e.tx.replace ("/playlist/rows", reordered);
        }

        void operator() (const AddClip& c)
        {
            auto item = toJson (c.clip);
            e.ensureUnique ("/playlist/clips", item, "clip");
            e.insertAt ("/playlist/clips", std::move (item), {});
        }

        void operator() (const UpdateClip& c)       { e.applyChanges (e.clipPath (c.clip), c.changes, { "type" }); }
        void operator() (const RemoveClip& c)       { e.tx.remove (e.clipPath (c.clip)); }

        void operator() (const AddInsert& c)
        {
            auto item = toJson (c.insert);
            e.ensureUnique ("/mixer/inserts", item, "mixer insert");
            e.insertAt ("/mixer/inserts", std::move (item), c.index);
        }

        void operator() (const UpdateInsert& c)
        {
            e.applyChanges (e.insertPath (c.insert), c.changes, { "effects", "sends" });
        }

        void operator() (const RemoveInsert& c)
        {
            if (c.insert == masterInsertId)
                throw CommandError ("the master insert can't be removed");

            e.insertPath (c.insert); // check it exists before cascading
            auto any = [] (const Json&) { return true; };

            e.reroute ("/channels", "insert", c.insert, masterInsertId, any);
            e.reroute ("/mixer/inserts", "output", c.insert, masterInsertId, any);

            auto& inserts = e.array ("/mixer/inserts");

            for (size_t i = 0; i < inserts.size(); ++i)
                e.removeWhere (join (join ("/mixer/inserts", i), "sends"),
                               [&] (const Json& send) { return send.value ("target", "") == c.insert; });

            e.removeAutomation ([&] (const Json& t) { return t.value ("insert", "") == c.insert; });
            e.tx.remove (e.insertPath (c.insert));
        }

        void operator() (const AddEffect& c)
        {
            auto base = e.insertPath (c.insert) + "/effects";
            auto item = toJson (c.effect);
            e.ensureUnique (base, item, "effect");
            e.insertAt (base, std::move (item), c.index);
        }

        void operator() (const UpdateEffect& c)
        {
            auto path = e.effectPath (c.insert, c.effect);

            if (! c.changes.is_object())
                throw CommandError ("changes must be an object");

            auto others = Json::object();

            for (auto& [key, value] : c.changes.items())
            {
                if (key != "params")
                {
                    others[key] = value;
                    continue;
                }

                if (! value.is_object())
                    throw CommandError ("params must be an object");

                if (! e.has (path + "/params"))
                    e.tx.add (path + "/params", Json::object());

                e.applyChanges (path + "/params", value);
            }

            e.applyChanges (path, others, { "type" });
        }

        void operator() (const RemoveEffect& c)
        {
            e.effectPath (c.insert, c.effect); // check it exists before cascading
            e.removeAutomation ([&] (const Json& t)
            {
                return t.value ("scope", "") == "effect" && t.value ("insert", "") == c.insert && t.value ("effect", "") == c.effect;
            });
            e.tx.remove (e.effectPath (c.insert, c.effect));
        }

        void operator() (const MoveEffect& c)
        {
            auto path = e.effectPath (c.insert, c.effect);
            auto effect = e.at (path);
            e.tx.remove (path);
            e.insertAt (e.insertPath (c.insert) + "/effects", std::move (effect), c.index);
        }

        void operator() (const AddSend& c)
        {
            auto base = e.insertPath (c.insert) + "/sends";
            auto item = toJson (c.send);
            e.ensureUnique (base, item, "send");
            e.insertAt (base, std::move (item), {});
        }

        void operator() (const UpdateSend& c)       { e.applyChanges (e.sendPath (c.insert, c.send), c.changes); }

        void operator() (const RemoveSend& c)
        {
            e.sendPath (c.insert, c.send); // check it exists before cascading
            e.removeAutomation ([&] (const Json& t)
            {
                return t.value ("scope", "") == "send" && t.value ("insert", "") == c.insert && t.value ("send", "") == c.send;
            });
            e.tx.remove (e.sendPath (c.insert, c.send));
        }

        void operator() (const Patch& c)
        {
            for (auto& op : c.ops)
                e.tx.apply (op);
        }
    };

    std::string idOf (const Json& j)
    {
        return j.value ("id", "");
    }
}

void applyCommand (Transaction& tx, const Command& command)
{
    std::visit (Apply { Editor (tx) }, command);
}

std::string describeCommand (const Command& command)
{
    return std::visit ([] (auto& c) -> std::string
    {
        using T = std::decay_t<decltype (c)>;

        if constexpr (std::is_same_v<T, SetTempo>)              return "Set tempo to " + (std::ostringstream() << c.tempo).str();
        else if constexpr (std::is_same_v<T, SetKey>)           return c.key ? "Set key to " + *c.key : std::string ("Clear the key");
        else if constexpr (std::is_same_v<T, SetStyle>)         return c.style ? "Set style to " + *c.style : std::string ("Clear the style");
        else if constexpr (std::is_same_v<T, SetTimeSignature>) return "Set time signature";
        else if constexpr (std::is_same_v<T, SetSwing>)         return "Set swing to " + std::to_string (static_cast<int> (std::round (c.swing * 100))) + "%";
        else if constexpr (std::is_same_v<T, RenameProject>)    return "Rename project";
        else if constexpr (std::is_same_v<T, AddChannel>)       return "Add channel " + c.channel.id;
        else if constexpr (std::is_same_v<T, UpdateChannel>)    return "Update channel " + c.channel;
        else if constexpr (std::is_same_v<T, RemoveChannel>)    return "Remove channel " + c.channel;
        else if constexpr (std::is_same_v<T, UpdateInstrument>) return "Update instrument of " + c.channel;
        else if constexpr (std::is_same_v<T, AddSamplerLayer>)  return "Add layer " + c.layer.id + " to " + c.channel;
        else if constexpr (std::is_same_v<T, UpdateSamplerLayer>) return "Update layer " + c.layer + " of " + c.channel;
        else if constexpr (std::is_same_v<T, RemoveSamplerLayer>) return "Remove layer " + c.layer + " from " + c.channel;
        else if constexpr (std::is_same_v<T, AddPattern>)       return "Add pattern " + c.pattern.id;
        else if constexpr (std::is_same_v<T, UpdatePattern>)    return "Update pattern " + c.pattern;
        else if constexpr (std::is_same_v<T, RemovePattern>)    return "Remove pattern " + c.pattern;
        else if constexpr (std::is_same_v<T, AddNotes>)         return "Add " + std::to_string (c.notes.size()) + " notes to " + c.pattern;
        else if constexpr (std::is_same_v<T, RemoveNotes>)      return "Remove notes from " + c.pattern;
        else if constexpr (std::is_same_v<T, SetNotes>)         return "Set " + c.channel + " notes in " + c.pattern;
        else if constexpr (std::is_same_v<T, InsertRow>)        return "Insert row " + std::to_string (c.row + 1);
        else if constexpr (std::is_same_v<T, UpdateRow>)        return "Update row " + std::to_string (c.row + 1);
        else if constexpr (std::is_same_v<T, RemoveRow>)        return "Remove row " + std::to_string (c.row + 1);
        else if constexpr (std::is_same_v<T, MoveRows>)         return c.count == 1 ? "Move row " + std::to_string (c.row + 1) : "Move " + std::to_string (c.count) + " rows";
        else if constexpr (std::is_same_v<T, AddClip>)          return "Add clip " + getClipId (c.clip);
        else if constexpr (std::is_same_v<T, UpdateClip>)       return "Update clip " + c.clip;
        else if constexpr (std::is_same_v<T, RemoveClip>)       return "Remove clip " + c.clip;
        else if constexpr (std::is_same_v<T, AddInsert>)        return "Add insert " + c.insert.id;
        else if constexpr (std::is_same_v<T, UpdateInsert>)     return "Update insert " + c.insert;
        else if constexpr (std::is_same_v<T, RemoveInsert>)     return "Remove insert " + c.insert;
        else if constexpr (std::is_same_v<T, AddEffect>)        return "Add effect " + idOf (toJson (c.effect)) + " to " + c.insert;
        else if constexpr (std::is_same_v<T, UpdateEffect>)     return "Update effect " + c.effect + " on " + c.insert;
        else if constexpr (std::is_same_v<T, RemoveEffect>)     return "Remove effect " + c.effect + " from " + c.insert;
        else if constexpr (std::is_same_v<T, MoveEffect>)       return "Move effect " + c.effect + " on " + c.insert;
        else if constexpr (std::is_same_v<T, AddSend>)          return "Add send " + c.send.id + " to " + c.insert;
        else if constexpr (std::is_same_v<T, UpdateSend>)       return "Update send " + c.send + " on " + c.insert;
        else if constexpr (std::is_same_v<T, RemoveSend>)       return "Remove send " + c.send + " from " + c.insert;
        else if constexpr (std::is_same_v<T, Patch>)            return "Edit project (" + std::to_string (c.ops.size()) + " changes)";
        else static_assert (! sizeof (T), "describeCommand is missing a command");
    }, command);
}

} // namespace gk
