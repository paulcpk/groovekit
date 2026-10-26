#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gk::model
{

using Json = nlohmann::ordered_json;

/** Thrown when a document doesn't match the schema. The path is a JSON pointer, e.g. /channels/2/volumeDb. */
struct ReadError : std::runtime_error
{
    ReadError (std::string pathIn, const std::string& message)
        : std::runtime_error ((pathIn.empty() ? "/" : pathIn) + ": " + message), path (std::move (pathIn)) {}

    std::string path;
};

/** Tracks the current JSON pointer while the generated readers walk a document. */
class Reader
{
public:
    [[noreturn]] void fail (const std::string& message) const
    {
        throw ReadError (pointer(), message);
    }

    std::string pointer() const
    {
        std::string s;

        for (auto& p : path)
            s += "/" + p;

        return s;
    }

    void expectObject (const Json& j, std::initializer_list<std::string_view> knownKeys) const
    {
        if (! j.is_object())
            fail ("expected object");

        for (auto& [key, value] : j.items())
        {
            bool known = false;

            for (auto k : knownKeys)
                known = known || k == key;

            if (! known)
                fail ("unknown property \"" + key + "\"");
        }
    }

    template <typename Fn>
    void required (const Json& j, const char* key, Fn&& fn)
    {
        auto it = j.find (key);

        if (it == j.end())
            fail (std::string ("missing required property \"") + key + "\"");

        path.emplace_back (key);
        fn (*it);
        path.pop_back();
    }

    template <typename Fn>
    void optional (const Json& j, const char* key, Fn&& fn)
    {
        auto it = j.find (key);

        if (it == j.end())
            return;

        path.emplace_back (key);
        fn (*it);
        path.pop_back();
    }

    template <typename Fn>
    void element (size_t index, Fn&& fn)
    {
        path.push_back (std::to_string (index));
        fn();
        path.pop_back();
    }

    void checkConst (const Json& j, const Json& expected) const
    {
        if (j != expected)
            fail ("must be " + expected.dump());
    }

    template <typename T>
    void checkMin (T value, double min) const
    {
        if (static_cast<double> (value) < min)
            fail ("must be >= " + format (min));
    }

    template <typename T>
    void checkMax (T value, double max) const
    {
        if (static_cast<double> (value) > max)
            fail ("must be <= " + format (max));
    }

    void checkMinLength (const std::string& value, size_t min) const
    {
        if (value.size() < min)
            fail ("must be at least " + std::to_string (min) + " characters");
    }

    template <typename T>
    void checkMinItems (const std::vector<T>& value, size_t min) const
    {
        if (value.size() < min)
            fail ("must have at least " + std::to_string (min) + " items");
    }

    void checkPattern (const std::string& value, const char* pattern)
    {
        auto it = patterns.find (pattern);

        if (it == patterns.end())
            it = patterns.emplace (pattern, std::regex (pattern)).first;

        if (! std::regex_match (value, it->second))
            fail ("\"" + value + "\" does not match " + pattern);
    }

    void checkOneOf (int value, std::initializer_list<int> allowed) const
    {
        for (auto a : allowed)
            if (a == value)
                return;

        fail ("value " + std::to_string (value) + " is not allowed");
    }

private:
    std::vector<std::string> path;
    std::unordered_map<std::string, std::regex> patterns;

    static std::string format (double v)
    {
        auto s = Json (v).dump();

        if (s.size() > 2 && s.ends_with (".0"))
            s.resize (s.size() - 2);

        return s;
    }
};

//==============================================================================
inline void readValue (Reader& r, const Json& j, std::string& out)
{
    if (! j.is_string())
        r.fail ("expected string");

    out = j.get<std::string>();
}

inline void readValue (Reader& r, const Json& j, bool& out)
{
    if (! j.is_boolean())
        r.fail ("expected boolean");

    out = j.get<bool>();
}

inline void readValue (Reader& r, const Json& j, double& out)
{
    if (! j.is_number())
        r.fail ("expected number");

    out = j.get<double>();
}

inline void readValue (Reader& r, const Json& j, int& out)
{
    if (! j.is_number_integer())
        r.fail ("expected integer");

    auto v = j.get<int64_t>();

    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
        r.fail ("integer out of range");

    out = static_cast<int> (v);
}

inline void readValue (Reader& r, const Json& j, int64_t& out)
{
    if (! j.is_number_integer())
        r.fail ("expected integer");

    if (j.is_number_unsigned() && j.get<uint64_t>() > static_cast<uint64_t> (std::numeric_limits<int64_t>::max()))
        r.fail ("integer out of range");

    out = j.get<int64_t>();
}

template <typename T>
void readValue (Reader& r, const Json& j, std::vector<T>& out)
{
    if (! j.is_array())
        r.fail ("expected array");

    out.clear();
    out.reserve (j.size());

    for (size_t i = 0; i < j.size(); ++i)
        r.element (i, [&] { readValue (r, j[i], out.emplace_back()); });
}

template <typename T>
void readValue (Reader& r, const Json& j, std::map<std::string, T>& out)
{
    if (! j.is_object())
        r.fail ("expected object");

    out.clear();

    for (auto& [key, value] : j.items())
        r.optional (j, key.c_str(), [&] (const Json& v) { readValue (r, v, out[key]); });
}

//==============================================================================
inline Json toJson (const std::string& v)   { return v; }
inline Json toJson (bool v)                 { return v; }
inline Json toJson (double v)               { return v; }
inline Json toJson (int v)                  { return v; }
inline Json toJson (int64_t v)              { return v; }

template <typename T>
Json toJson (const std::vector<T>& v)
{
    auto a = Json::array();

    for (auto& item : v)
        a.push_back (toJson (item));

    return a;
}

template <typename T>
Json toJson (const std::map<std::string, T>& v)
{
    auto o = Json::object();

    for (auto& [key, value] : v)
        o[key] = toJson (value);

    return o;
}

} // namespace gk::model
