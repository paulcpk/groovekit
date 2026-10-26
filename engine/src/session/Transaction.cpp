#include "Transaction.h"

namespace gk
{

using model::Json;
using model::PatchOp;
using model::PatchOpOp;

std::string pointerSegment (const std::string& key)
{
    std::string s;

    for (auto c : key)
    {
        if (c == '~')       s += "~0";
        else if (c == '/')  s += "~1";
        else                s += c;
    }

    return s;
}

namespace
{
    struct Location
    {
        Json* parent;
        std::string key;
        std::optional<size_t> index; // set when the parent is an array
    };

    Json::json_pointer parsePointer (const std::string& path)
    {
        try
        {
            return Json::json_pointer (path);
        }
        catch (const Json::exception&)
        {
            throw CommandError ("invalid JSON Pointer \"" + path + "\"");
        }
    }

    std::optional<size_t> parseIndex (const std::string& key)
    {
        if (key.empty() || key.size() > 9 || (key.size() > 1 && key[0] == '0'))
            return {};

        for (auto c : key)
            if (c < '0' || c > '9')
                return {};

        return static_cast<size_t> (std::stoul (key));
    }

    /** Finds the parent container of path. allowAppend lets "-" and index == size address the end of an array. */
    Location locate (Json& doc, const std::string& path, bool allowAppend)
    {
        auto ptr = parsePointer (path);

        if (ptr.empty())
            throw CommandError ("the document root can't be changed");

        auto key = ptr.back();
        auto parentPtr = ptr.parent_pointer();

        if (! doc.contains (parentPtr))
            throw CommandError ("\"" + parentPtr.to_string() + "\" doesn't exist");

        auto& parent = doc[parentPtr];

        if (parent.is_object())
            return { &parent, key, {} };

        if (! parent.is_array())
            throw CommandError ("\"" + parentPtr.to_string() + "\" isn't an object or array");

        if (key == "-" && allowAppend)
            return { &parent, key, parent.size() };

        auto index = parseIndex (key);

        if (! index || *index > parent.size() || (*index == parent.size() && ! allowAppend))
            throw CommandError ("\"" + path + "\" is out of range");

        return { &parent, key, index };
    }

    std::string withIndex (const std::string& path, size_t index)
    {
        return path.substr (0, path.find_last_of ('/') + 1) + std::to_string (index);
    }

    PatchOp makeOp (PatchOpOp op, std::string path, std::optional<Json> value = {})
    {
        return { op, std::move (path), std::move (value) };
    }

    /** Applies one operation and returns its inverse. */
    PatchOp applyOne (Json& doc, const PatchOp& op)
    {
        switch (op.op)
        {
            case PatchOpOp::add:
            {
                if (! op.value)
                    throw CommandError ("add needs a value");

                auto loc = locate (doc, op.path, true);

                if (loc.index)
                {
                    loc.parent->insert (loc.parent->begin() + static_cast<std::ptrdiff_t> (*loc.index), *op.value);
                    return makeOp (PatchOpOp::remove, withIndex (op.path, *loc.index));
                }

                auto it = loc.parent->find (loc.key);
                auto inverse = it != loc.parent->end() ? makeOp (PatchOpOp::replace, op.path, *it)
                                                       : makeOp (PatchOpOp::remove, op.path);
                (*loc.parent)[loc.key] = *op.value;
                return inverse;
            }

            case PatchOpOp::remove:
            {
                auto loc = locate (doc, op.path, false);

                if (loc.index)
                {
                    auto old = (*loc.parent)[*loc.index];
                    loc.parent->erase (*loc.index);
                    return makeOp (PatchOpOp::add, op.path, std::move (old));
                }

                auto it = loc.parent->find (loc.key);

                if (it == loc.parent->end())
                    throw CommandError ("\"" + op.path + "\" doesn't exist");

                auto old = *it;
                loc.parent->erase (it);
                return makeOp (PatchOpOp::add, op.path, std::move (old));
            }

            case PatchOpOp::replace:
            {
                if (! op.value)
                    throw CommandError ("replace needs a value");

                auto loc = locate (doc, op.path, false);
                Json* target;

                if (loc.index)
                {
                    target = &(*loc.parent)[*loc.index];
                }
                else
                {
                    auto it = loc.parent->find (loc.key);

                    if (it == loc.parent->end())
                        throw CommandError ("\"" + op.path + "\" doesn't exist");

                    target = &*it;
                }

                auto old = std::exchange (*target, *op.value);
                return makeOp (PatchOpOp::replace, op.path, std::move (old));
            }
        }

        throw std::logic_error ("unknown patch op");
    }
}

Transaction::Transaction (Json& document) : doc (document) {}

void Transaction::apply (const PatchOp& op)
{
    inverse.push_back (applyOne (doc, op));

    // Record appends with their real index so the patch replays identically.
    auto recorded = op;

    if (op.op == PatchOpOp::add && op.path.ends_with ("/-"))
        recorded.path = withIndex (op.path, doc.at (parsePointer (op.path).parent_pointer()).size() - 1);

    forward.push_back (std::move (recorded));
}

void Transaction::add (const std::string& path, Json value)
{
    apply (makeOp (PatchOpOp::add, path, std::move (value)));
}

void Transaction::remove (const std::string& path)
{
    apply (makeOp (PatchOpOp::remove, path));
}

void Transaction::replace (const std::string& path, Json value)
{
    apply (makeOp (PatchOpOp::replace, path, std::move (value)));
}

std::vector<PatchOp> Transaction::getInverse() const
{
    return { inverse.rbegin(), inverse.rend() };
}

void Transaction::rollback()
{
    for (auto it = inverse.rbegin(); it != inverse.rend(); ++it)
        applyOne (doc, *it);

    forward.clear();
    inverse.clear();
}

void applyPatch (Json& document, const std::vector<PatchOp>& ops)
{
    for (auto& op : ops)
        applyOne (document, op);
}

} // namespace gk
