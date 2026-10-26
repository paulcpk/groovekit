#include "Classifier.h"

#include <cctype>
#include <regex>
#include <set>

namespace gk::library
{

namespace
{
    struct CategoryWords
    {
        const char* category;
        std::set<std::string> words;
    };

    /** Checked in order, so more specific categories win (a "kick loop" is a kick, "808 kick" too). */
    const std::vector<CategoryWords>& categoryWords()
    {
        static const std::vector<CategoryWords> words {
            { "kick", { "kick", "kicks", "kik", "bd", "bassdrum" } },
            { "snare", { "snare", "snares", "snr", "sd" } },
            { "clap", { "clap", "claps", "clp" } },
            { "hat", { "hat", "hats", "hihat", "hihats", "hh", "oh", "ch", "openhat", "closedhat" } },
            { "cymbal", { "crash", "ride", "cymbal", "cymbals", "splash", "china" } },
            { "tom", { "tom", "toms" } },
            { "perc", { "perc", "percs", "percussion", "shaker", "shakers", "conga", "congas", "bongo", "bongos",
                        "tamb", "tambourine", "cowbell", "clave", "rim", "rimshot", "woodblock", "snap" } },
            { "bass", { "bass", "basses", "sub", "subbass", "808", "reese" } },
            { "vocal", { "vocal", "vocals", "vox", "voice", "acapella", "acappella", "chant", "phrase" } },
            { "synth", { "synth", "synths", "lead", "leads", "pad", "pads", "chord", "chords", "stab", "stabs", "pluck",
                         "plucks", "arp", "arps", "keys", "piano", "organ", "rhodes", "bell", "bells", "string", "strings",
                         "melody", "melodic", "guitar", "brass" } },
            { "fx", { "fx", "sfx", "riser", "risers", "sweep", "impact", "impacts", "downlifter", "uplifter", "noise",
                      "whoosh", "reverse", "texture", "atmos", "ambience", "foley" } },
            { "drums", { "drum", "drums", "break", "breaks", "breakbeat", "top", "tops", "beat", "beats", "groove", "kit" } },
        };

        return words;
    }

    const std::set<std::string> loopWords { "loop", "loops", "lp", "break", "breaks", "breakbeat", "groove", "grooves" };

    std::optional<std::string> categoryFromWords (const std::vector<std::string>& words)
    {
        for (auto& c : categoryWords())
            for (auto& w : words)
                if (c.words.contains (w))
                    return c.category;

        return {};
    }

    /** Guess for unnamed one-shots. Deliberately conservative. */
    std::string categoryFromSound (const SampleFeatures& f, bool loop)
    {
        const auto lowEnergy = f.bands.sub + f.bands.low;

        if (loop)
            return f.onsetCount >= 4 && f.bands.high > 0.2f ? "drums" : "other";

        if (f.durationSeconds < 0.8 && lowEnergy > 0.6f && f.attackMs < 30)
            return "kick";

        if (f.durationSeconds < 0.6 && f.centroidHz > 5000 && ! f.pitch)
            return "hat";

        if (f.pitch && f.pitch->hz < 200 && lowEnergy > 0.5f && f.durationSeconds >= 0.3)
            return "bass";

        if (f.pitch && f.pitch->confidence > 0.8f)
            return "synth";

        return "other";
    }
}

std::vector<std::string> tokenize (const std::string& text)
{
    std::vector<std::string> words;
    std::string current;

    auto flush = [&]
    {
        if (! current.empty())
            words.push_back (current);

        current.clear();
    };

    for (size_t i = 0; i < text.size(); ++i)
    {
        const auto c = static_cast<unsigned char> (text[i]);

        if (std::isalpha (c) || c == '#')
        {
            // camelCase boundary: "KickLoop" -> "kick", "loop"
            if (std::isupper (c) && ! current.empty() && std::islower (static_cast<unsigned char> (text[i - 1])))
                flush();

            if (! current.empty() && std::isdigit (static_cast<unsigned char> (current.back())))
                flush();

            current += static_cast<char> (std::tolower (c));
        }
        else if (std::isdigit (c))
        {
            // Keep "808" whole but split "kick01" into "kick", "01"
            if (! current.empty() && ! std::isdigit (static_cast<unsigned char> (current.back())))
                flush();

            current += static_cast<char> (c);
        }
        else
        {
            flush();
        }
    }

    flush();
    return words;
}

std::optional<std::string> parseKey (const std::string& token)
{
    static const std::regex pattern ("^([a-g])(#|b)?(m|min|minor|maj|major)?$", std::regex::icase);
    std::smatch m;

    if (! std::regex_match (token, m, pattern))
        return {};

    // A bare letter is too ambiguous ("a", "b" appear everywhere); require an accidental or a quality.
    if (! m[2].matched && ! m[3].matched)
        return {};

    std::string note (1, static_cast<char> (std::toupper (static_cast<unsigned char> (m[1].str()[0]))));

    if (m[2].matched)
        note += m[2].str() == "b" ? "b" : "#";

    const auto quality = m[3].str();
    const bool minor = quality == "m" || quality == "min" || quality == "minor";
    return note + (minor ? " minor" : " major");
}

Classification classify (const std::filesystem::path& path, const SampleFeatures& features)
{
    Classification c;

    const auto name = path.stem().string();
    const auto nameWords = tokenize (name);

    // Folders nearest the file first, at most four levels up.
    std::vector<std::string> folderWords;
    int depth = 0;

    for (auto dir = path.parent_path(); ! dir.empty() && dir != dir.root_path() && depth < 4; dir = dir.parent_path(), ++depth)
        for (auto& w : tokenize (dir.filename().string()))
            folderWords.push_back (w);

    std::set<std::string> seen;

    for (auto* list : { &nameWords, static_cast<const std::vector<std::string>*> (&folderWords) })
        for (auto& w : *list)
            if (w.size() > 1 && ! std::isdigit (static_cast<unsigned char> (w[0])) && seen.insert (w).second)
                c.tags.push_back (w);

    auto hasAny = [] (const std::vector<std::string>& words, const std::set<std::string>& set)
    {
        return std::any_of (words.begin(), words.end(), [&] (auto& w) { return set.contains (w); });
    };

    c.loop = hasAny (nameWords, loopWords) || hasAny (folderWords, loopWords);

    if (auto fromName = categoryFromWords (nameWords))
    {
        c.category = *fromName;
        c.categorySource = "name";
    }
    else if (auto fromFolders = categoryFromWords (folderWords))
    {
        c.category = *fromFolders;
        c.categorySource = "name";
    }
    else
    {
        c.category = categoryFromSound (features, c.loop);
        c.categorySource = "sound";
    }

    // Tempo: "124bpm", "124 BPM", or a bare 60-200 number in a loop's name.
    static const std::regex bpmPattern ("(\\d{2,3}(?:\\.\\d+)?)\\s*bpm", std::regex::icase);
    std::smatch m;

    if (std::regex_search (name, m, bpmPattern))
        c.tempo = std::stod (m[1].str());
    else if (c.loop)
        for (auto& w : nameWords)
            if (std::all_of (w.begin(), w.end(), ::isdigit) && w.size() <= 3)
                if (auto v = std::stoi (w); v >= 60 && v <= 200)
                    c.tempo = v;

    if (! c.tempo && c.loop)
        c.tempo = features.estimatedTempo;

    // Key: split the raw name on separators only, so "C#m" and "Am" survive.
    static const std::regex separators ("[\\s_\\-\\.\\(\\)\\[\\]]+");

    for (std::sregex_token_iterator it (name.begin(), name.end(), separators, -1), end; it != end; ++it)
        if (auto key = parseKey (it->str()))
            c.key = key;

    return c;
}

} // namespace gk::library
