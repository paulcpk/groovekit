#include "Guides.h"

#include "EngineContext.h"

#include <juce_core/juce_core.h>

#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace gk::guides
{

using model::Json;
namespace fs = std::filesystem;

namespace
{
    std::string readFile (const fs::path& file)
    {
        std::ifstream in (file, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }

    /** "Drum & Bass" and "drum-and-bass" alike: lower case letters and digits only. */
    /** Lower case letters and digits only, "&" read as "and": "Drum & Bass" is "drumandbass". */
    std::string simplified (const std::string& s)
    {
        std::string out;

        for (unsigned char c : s)
            if (std::isalnum (c))
                out += static_cast<char> (std::tolower (c));
            else if (c == '&')
                out += "and";

        return out;
    }

    /** "Drum and bass" → "drum-and-bass". */
    std::string idFrom (const std::string& name)
    {
        std::string id;

        for (unsigned char c : name)
        {
            if (std::isalnum (c))
                id += static_cast<char> (std::tolower (c));
            else if (! id.empty() && id.back() != '-')
                id += '-';
        }

        while (! id.empty() && id.back() == '-')
            id.pop_back();

        return id.substr (0, 60);
    }

    bool validId (const std::string& id)
    {
        return ! id.empty() && id.size() <= 60
            && std::all_of (id.begin(), id.end(), [] (unsigned char c) { return std::islower (c) || std::isdigit (c) || c == '-'; });
    }

    Targets::Range range (const Json& j, const std::string& what)
    {
        if (! j.is_array() || j.size() != 2 || ! j[0].is_number() || ! j[1].is_number() || j[0].get<double>() > j[1].get<double>())
            throw std::invalid_argument ("targets: " + what + " must be [lowest, highest]");

        return { j[0].get<double>(), j[1].get<double>() };
    }

    Json rangeJson (const Targets::Range& r)  { return Json::array ({ r.min, r.max }); }
}

const std::vector<std::string>& roleNames()
{
    static const std::vector<std::string> names { "kick", "bass", "snare", "hats", "perc", "drums", "music", "vocal", "fx" };
    return names;
}

Guide parse (const std::string& id, const std::string& text)
{
    Guide g;
    g.id = id;
    g.text = text;

    // The name, and the first paragraph after it.
    std::istringstream lines (text);
    std::string line, paragraph;
    bool named = false;

    while (std::getline (lines, line))
    {
        const auto trimmed = juce::String (line).trim().toStdString();

        if (! named && trimmed.starts_with ("# "))
        {
            g.name = juce::String (trimmed.substr (2)).trim().toStdString();
            named = true;
            continue;
        }

        if (! named)
            continue;

        if (trimmed.empty() || trimmed.starts_with ("#") || trimmed.starts_with ("```"))
        {
            if (! paragraph.empty())
                break;

            continue;
        }

        paragraph += (paragraph.empty() ? "" : " ") + trimmed;
    }

    g.name = g.name.empty() ? id : g.name;
    g.summary = paragraph;

    // The targets.
    static const std::regex block (R"(```targets[ \t]*\r?\n([\s\S]*?)```)");
    std::smatch m;

    if (std::regex_search (text, m, block))
    {
        Json j;

        try
        {
            j = Json::parse (m[1].str());
        }
        catch (const Json::exception& e)
        {
            throw std::invalid_argument (std::string ("targets aren't valid JSON: ") + e.what());
        }

        if (! j.is_object())
            throw std::invalid_argument ("targets must be a JSON object");

        auto& t = g.targets;

        for (auto& [key, value] : j.items())
        {
            if (key == "tempo")                 t.tempo = range (value, key);
            else if (key == "swing")            t.swing = range (value, key);
            else if (key == "lowVsHigh")        t.lowVsHigh = range (value, key);
            else if (key == "highHitsPerBeat")  t.highHitsPerBeat = range (value, key);
            else if (key == "loudness")         t.loudness = range (value, key);
            else if (key == "partsAtOnce" || key == "topAtOnce")
            {
                if (! value.is_number_integer() || value.get<int>() < 1)
                    throw std::invalid_argument ("targets: " + key + " must be a whole number");

                (key == "partsAtOnce" ? t.partsAtOnce : t.topAtOnce) = value.get<int>();
            }
            else if (key == "levels")
            {
                if (! value.is_object())
                    throw std::invalid_argument ("targets: levels must map roles to [lowest, highest]");

                for (auto& [role, r] : value.items())
                {
                    if (std::find (roleNames().begin(), roleNames().end(), role) == roleNames().end())
                        throw std::invalid_argument ("targets: no role \"" + role + "\" (roles are kick, bass, snare, hats, perc, drums, music, vocal and fx)");

                    t.levels[role] = range (r, "levels." + role);
                }
            }
            else
            {
                throw std::invalid_argument ("targets: unknown \"" + key + "\"");
            }
        }
    }

    return g;
}

Json toJson (const Targets& t)
{
    auto j = Json::object();

    if (t.tempo)            j["tempo"] = rangeJson (*t.tempo);
    if (t.swing)            j["swing"] = rangeJson (*t.swing);
    if (t.partsAtOnce)      j["partsAtOnce"] = *t.partsAtOnce;
    if (t.topAtOnce)        j["topAtOnce"] = *t.topAtOnce;
    if (t.lowVsHigh)        j["lowVsHigh"] = rangeJson (*t.lowVsHigh);
    if (t.highHitsPerBeat)  j["highHitsPerBeat"] = rangeJson (*t.highHitsPerBeat);
    if (t.loudness)         j["loudness"] = rangeJson (*t.loudness);

    if (! t.levels.empty())
    {
        auto levels = Json::object();

        for (auto& [role, r] : t.levels)
            levels[role] = rangeJson (r);

        j["levels"] = levels;
    }

    return j;
}

Json toJson (const Guide& g, bool withText)
{
    Json j { { "id", g.id }, { "name", g.name }, { "summary", g.summary }, { "builtIn", g.builtIn }, { "edited", g.edited } };

    if (g.hidden)
        j["hidden"] = true;

    if (withText)
    {
        j["text"] = g.text;
        j["targets"] = toJson (g.targets);
    }

    return j;
}

//==============================================================================
GuideStore::GuideStore (fs::path f) : folder (std::move (f)) {}

/** The built-in guides the user took out of the list. */
static std::set<std::string> readHidden (const fs::path& folder)
{
    try
    {
        std::ifstream in (folder / "hidden.json");

        if (in)
            return Json::parse (in).get<std::set<std::string>>();
    }
    catch (const std::exception&)
    {
        // Nothing hidden.
    }

    return {};
}

void GuideStore::setHidden (const std::string& id, bool hide)
{
    auto hidden = readHidden (folder);
    hide ? (void) hidden.insert (id) : (void) hidden.erase (id);
    fs::create_directories (folder);
    std::ofstream (folder / "hidden.json") << Json (hidden).dump (2);
}

fs::path GuideStore::getDefaultFolder()
{
    if (auto env = std::getenv ("GROOVEKIT_GUIDES"))
        return env;

    return EngineContext::getAppDataFolder() / "guides";
}

std::vector<Guide> GuideStore::list() const
{
    std::map<std::string, Guide> byId;

    for (auto& [id, text] : builtInTexts())
    {
        auto g = parse (id, text);
        g.builtIn = true;
        byId[id] = std::move (g);
    }

    std::error_code ec;

    for (auto& entry : fs::directory_iterator (folder, ec))
    {
        if (entry.path().extension() != ".md")
            continue;

        const auto id = entry.path().stem().string();

        if (! validId (id))
            continue;

        const bool builtIn = byId.contains (id);
        Guide g;

        try
        {
            g = parse (id, readFile (entry.path()));
        }
        catch (const std::invalid_argument&)
        {
            // Saving checks the targets; a file changed by hand since keeps its words, without them.
            g = parse (id, std::regex_replace (readFile (entry.path()), std::regex (R"(```targets[\s\S]*?```)"), ""));
        }

        g.builtIn = builtIn;
        g.edited = true;
        byId[id] = std::move (g);
    }

    std::vector<Guide> guides;
    const auto hiddenIds = readHidden (folder);

    for (auto& [id, g] : byId)
    {
        g.hidden = g.builtIn && hiddenIds.contains (id);
        guides.push_back (std::move (g));
    }

    std::sort (guides.begin(), guides.end(), [] (auto& a, auto& b) { return juce::String (a.name).compareNatural (juce::String (b.name)) < 0; });
    return guides;
}

std::optional<Guide> GuideStore::find (const std::string& idOrName) const
{
    const auto wanted = simplified (idOrName);

    // A name can carry another in brackets, "Hard techno (Schranz)": either one finds it.
    auto names = [] (const std::string& name)
    {
        std::vector<std::string> all { simplified (name) };
        const auto open = name.find ('('), close = name.rfind (')');

        if (open != std::string::npos && close != std::string::npos && close > open)
        {
            all.push_back (simplified (name.substr (0, open)));
            all.push_back (simplified (name.substr (open + 1, close - open - 1)));
        }

        return all;
    };

    for (auto& g : list())
    {
        const auto all = names (g.name);

        if (simplified (g.id) == wanted || std::find (all.begin(), all.end(), wanted) != all.end())
            return g;
    }

    return {};
}

Guide GuideStore::save (const std::string& givenId, const std::string& text)
{
    if (juce::String (text).trim().isEmpty())
        throw std::invalid_argument ("A guide needs some text");

    auto id = givenId;

    if (id.empty())
    {
        auto name = parse ("guide", text).name;
        id = idFrom (name == "guide" ? std::string ("guide") : name);
    }

    if (! validId (id))
        throw std::invalid_argument ("A guide's id is lower case letters, digits and dashes");

    auto guide = parse (id, text);
    fs::create_directories (folder);
    const auto file = folder / (id + ".md");
    const auto temp = file.string() + ".tmp";

    {
        std::ofstream out (temp, std::ios::binary | std::ios::trunc);
        out << text;

        if (! out)
            throw std::runtime_error ("Couldn't write " + file.string());
    }

    fs::rename (temp, file);
    guide.builtIn = std::any_of (builtInTexts().begin(), builtInTexts().end(), [&] (auto& b) { return b.first == id; });
    guide.edited = true;
    return guide;
}

GuideStore::Removed GuideStore::remove (const std::string& id)
{
    if (! validId (id))
        return Removed::nothing;

    std::error_code ec;

    if (! fs::remove (folder / (id + ".md"), ec))
        return Removed::nothing;

    const bool builtIn = std::any_of (builtInTexts().begin(), builtInTexts().end(), [&] (auto& b) { return b.first == id; });
    return builtIn ? Removed::restored : Removed::deleted;
}

} // namespace gk::guides
