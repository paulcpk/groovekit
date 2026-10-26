#include "SampleLibrary.h"

#include <fstream>
#include <set>

namespace gk::library
{

using model::Json;
namespace fs = std::filesystem;

namespace
{
    constexpr int indexVersion = 1;
    constexpr double analysedSeconds = 30;

    bool isAudioFile (const fs::path& p)
    {
        auto ext = p.extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3" || ext == ".ogg" || ext == ".m4a";
    }

    /** Index keys are canonical, so /var/... and /private/var/... are the same file. */
    std::string key (const fs::path& p)
    {
        return fs::weakly_canonical (fs::absolute (p)).string();
    }

    int64_t modifiedTime (const fs::path& p)
    {
        return static_cast<int64_t> (fs::last_write_time (p).time_since_epoch().count());
    }

    //==============================================================================
    SampleFeatures featuresFromJson (const Json& j)
    {
        SampleFeatures f;
        f.durationSeconds = j.value ("durationSeconds", 0.0);
        f.sampleRate = j.value ("sampleRate", 0.0);
        f.channels = j.value ("channels", 0);
        f.peakDb = j.value ("peakDb", analysis::silenceDb);
        f.rmsDb = j.value ("rmsDb", analysis::silenceDb);
        f.attackMs = j.value ("attackMs", 0.0);
        f.decayMs = j.value ("decayMs", 0.0);
        f.centroidHz = j.value ("centroidHz", 0.0);
        f.onsetCount = j.value ("onsetCount", 0);

        if (j.contains ("integratedLufs"))  f.integratedLufs = j["integratedLufs"].get<float>();
        if (j.contains ("estimatedTempo"))  f.estimatedTempo = j["estimatedTempo"].get<double>();

        if (auto b = j.find ("bands"); b != j.end())
            f.bands = { b->value ("sub", 0.0f), b->value ("low", 0.0f), b->value ("mid", 0.0f), b->value ("high", 0.0f) };

        if (auto p = j.find ("pitch"); p != j.end())
            f.pitch = SampleFeatures::Pitch { p->value ("hz", 0.0), p->value ("key", 0), p->value ("cents", 0.0), p->value ("confidence", 0.0f) };

        return f;
    }

    Json classificationToJson (const Classification& c)
    {
        Json j { { "category", c.category }, { "categorySource", c.categorySource }, { "loop", c.loop }, { "tags", c.tags } };

        if (c.tempo)    j["tempo"] = *c.tempo;
        if (c.key)      j["key"] = *c.key;

        return j;
    }

    Classification classificationFromJson (const Json& j)
    {
        Classification c;
        c.category = j.value ("category", "other");
        c.categorySource = j.value ("categorySource", "name");
        c.loop = j.value ("loop", false);
        c.tags = j.value ("tags", std::vector<std::string>());

        if (j.contains ("tempo"))   c.tempo = j["tempo"].get<double>();
        if (j.contains ("key"))     c.key = j["key"].get<std::string>();

        return c;
    }

    /** A sentence for agents: "kick one-shot, 0.30 s, pitched A1 (+12 c), fast attack, dark". */
    std::string describe (const SampleEntry& e)
    {
        auto& f = e.features;
        auto& c = e.classification;
        std::string s = c.category + (c.loop ? " loop" : " one-shot");

        char duration[32];
        std::snprintf (duration, sizeof (duration), ", %.2f s", f.durationSeconds);
        s += duration;

        if (c.tempo)    s += ", " + juce::String (*c.tempo, 1).toStdString() + " BPM";
        if (c.key)      s += ", " + *c.key;

        if (f.pitch && f.pitch->confidence > 0.7f)
        {
            const auto cents = static_cast<int> (std::lround (f.pitch->cents));
            s += ", pitched " + noteName (f.pitch->key) + (cents != 0 ? " (" + std::string (cents > 0 ? "+" : "") + std::to_string (cents) + " c)" : "");
        }

        if (f.attackMs <= 10)
            s += ", sharp attack";
        else if (f.attackMs >= 100)
            s += ", soft attack";

        if (f.centroidHz > 0)
            s += f.centroidHz < 500 ? ", dark" : f.centroidHz > 4000 ? ", bright" : "";

        if (f.bands.sub + f.bands.low > 0.6f)
            s += ", bass-heavy";

        return s;
    }

    bool keysMatch (const std::optional<std::string>& entryKey, const std::string& wanted)
    {
        if (! entryKey)
            return false;

        auto normalised = parseKey (wanted).value_or (wanted);
        return juce::String (*entryKey).equalsIgnoreCase (juce::String (normalised));
    }
}

std::string noteName (int key)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((key % 12) + 12) % 12] + std::to_string (key / 12 - 1);
}

//==============================================================================
SampleLibrary::SampleLibrary (fs::path file) : indexFile (std::move (file))
{
    load();
}

SampleLibrary::~SampleLibrary()
{
    cancelScan = true;

    if (scanThread.joinable())
        scanThread.join();
}

fs::path SampleLibrary::getDefaultFolder()
{
    auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    return fs::path (music.getFullPathName().toStdString()) / "Samples";
}

bool SampleLibrary::addDefaultFolderIfNone()
{
    if (! getFolders().empty())
        return false;

    fs::create_directories (getDefaultFolder());
    addFolder (getDefaultFolder());
    return true;
}

fs::path SampleLibrary::getDefaultIndexFile()
{
    if (auto env = std::getenv ("GROOVEKIT_LIBRARY_INDEX"))
        return env;

    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    return fs::path (dir.getFullPathName().toStdString()) / "GrooveKit" / "library.json";
}

std::vector<fs::path> SampleLibrary::getFolders() const
{
    std::lock_guard lock (mutex);
    return folders;
}

void SampleLibrary::addFolder (const fs::path& folder)
{
    auto absolute = fs::weakly_canonical (fs::absolute (folder));

    if (! fs::is_directory (absolute))
        throw std::runtime_error ("Not a folder: " + absolute.string());

    std::lock_guard lock (mutex);

    if (std::find (folders.begin(), folders.end(), absolute) == folders.end())
        folders.push_back (absolute);

    save();
}

void SampleLibrary::removeFolder (const fs::path& folder)
{
    auto absolute = key (folder);

    std::lock_guard lock (mutex);
    folders.erase (std::remove (folders.begin(), folders.end(), fs::path (absolute)), folders.end());

    for (auto it = entries.begin(); it != entries.end();)
        it = it->first.starts_with (absolute + "/") ? entries.erase (it) : std::next (it);

    save();
}

//==============================================================================
bool SampleLibrary::startScan (ProgressCallback callback)
{
    std::lock_guard lock (mutex);

    if (progress.running)
        return false;

    if (scanThread.joinable())
        scanThread.join();

    progress = { true, 0, 0, 0, 0 };
    cancelScan = false;
    scanThread = std::thread ([this, callback] { scan (callback); });
    return true;
}

void SampleLibrary::waitForScan()
{
    if (scanThread.joinable())
        scanThread.join();
}

SampleLibrary::Progress SampleLibrary::getProgress() const
{
    std::lock_guard lock (mutex);
    return progress;
}

void SampleLibrary::scan (ProgressCallback callback)
{
    auto report = [&] (bool force)
    {
        Progress p;
        {
            std::lock_guard lock (mutex);
            p = progress;
        }

        if (callback && (force || (p.analysed + p.unchanged + p.failed) % 50 == 0))
            callback (p);
    };

    std::vector<fs::path> files;

    for (auto& folder : getFolders())
    {
        std::error_code ec;

        for (auto it = fs::recursive_directory_iterator (folder, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment (ec))
        {
            if (ec)
                break;

            if (it->is_regular_file (ec) && isAudioFile (it->path()) && ! it->path().filename().string().starts_with ("._"))
                files.push_back (it->path());
        }
    }

    {
        std::lock_guard lock (mutex);
        progress.filesFound = static_cast<int> (files.size());

        // Forget files that no longer exist.
        std::set<std::string> present;
        for (auto& f : files) present.insert (key (f));

        for (auto it = entries.begin(); it != entries.end();)
            it = present.contains (it->first) ? std::next (it) : entries.erase (it);
    }

    report (true);

    // Analysis is independent per file: spread it over the cores, saving now and then so an
    // interrupted first scan of a big library isn't lost.
    std::atomic<size_t> next { 0 };
    std::atomic<int> sinceSave { 0 };
    constexpr int saveEvery = 2000;

    auto work = [&]
    {
        for (size_t i; ! cancelScan && (i = next++) < files.size();)
        {
            auto& file = files[i];
            std::error_code ec;
            const auto size = static_cast<int64_t> (fs::file_size (file, ec));
            const auto modified = modifiedTime (file);

            {
                std::lock_guard lock (mutex);

                if (auto it = entries.find (key (file)); it != entries.end() && it->second.size == size && it->second.modified == modified)
                {
                    ++progress.unchanged;
                    continue;
                }
            }

            try
            {
                auto entry = analyseFile (file);
                std::lock_guard lock (mutex);
                entries[entry.path] = std::move (entry);
                ++progress.analysed;

                if (++sinceSave >= saveEvery)
                {
                    sinceSave = 0;
                    save();
                }
            }
            catch (const std::exception&)
            {
                std::lock_guard lock (mutex);
                ++progress.failed;
            }

            report (false);
        }
    };

    const auto threads = std::max (1u, std::min (8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;

    for (unsigned t = 1; t < threads; ++t)
        workers.emplace_back (work);

    work();

    for (auto& w : workers)
        w.join();

    {
        std::lock_guard lock (mutex);
        progress.running = false;
        save();
    }

    report (true);
}

SampleEntry SampleLibrary::analyseFile (const fs::path& file)
{
    SampleEntry e;
    e.path = key (file);
    e.size = static_cast<int64_t> (fs::file_size (file));
    e.modified = modifiedTime (file);

    auto audio = analysis::loadAudio (file, analysedSeconds);
    e.features = analyseSample (audio, analysedSeconds);

    // The analysis may only cover the start; keep the real length.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (e.path))); reader != nullptr)
        e.features.durationSeconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;

    e.classification = classify (file, e.features);
    return e;
}

//==============================================================================
size_t SampleLibrary::size() const
{
    std::lock_guard lock (mutex);
    return entries.size();
}

std::optional<SampleEntry> SampleLibrary::find (const fs::path& path) const
{
    std::lock_guard lock (mutex);

    if (auto it = entries.find (key (path)); it != entries.end())
        return it->second;

    return {};
}

std::vector<SampleEntry> SampleLibrary::search (const SampleQuery& q) const
{
    const auto words = tokenize (q.text);
    std::vector<std::pair<int, const SampleEntry*>> scored;

    std::lock_guard lock (mutex);

    for (auto& [path, e] : entries)
    {
        auto& c = e.classification;
        auto& f = e.features;

        if (q.category && c.category != *q.category)                    continue;
        if (q.loop && c.loop != *q.loop)                                continue;
        if (q.minDuration && f.durationSeconds < *q.minDuration)        continue;
        if (q.maxDuration && f.durationSeconds > *q.maxDuration)        continue;
        if (q.key && ! keysMatch (c.key, *q.key))                       continue;

        if (q.tempo)
        {
            if (! c.tempo)
                continue;

            auto near = [&] (double t) { return std::abs (t - *q.tempo) <= q.tempoTolerance; };

            if (! near (*c.tempo) && ! near (*c.tempo * 2) && ! near (*c.tempo / 2))
                continue;
        }

        const auto nameWords = tokenize (fs::path (path).stem().string());
        int score = 0;
        bool all = true;

        for (auto& w : words)
        {
            auto in = [&] (const std::vector<std::string>& list) { return std::find (list.begin(), list.end(), w) != list.end(); };
            auto prefix = [&] (const std::vector<std::string>& list)
            {
                return std::any_of (list.begin(), list.end(), [&] (auto& t) { return t.size() > w.size() && t.starts_with (w); });
            };

            if (in (nameWords) || w == c.category)  score += 3;
            else if (prefix (nameWords))            score += 2;
            else if (in (c.tags) || prefix (c.tags)) score += 1;
            else                                    { all = false; break; }
        }

        if (all)
            scored.emplace_back (score, &e);
    }

    std::stable_sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first > b.first; });

    std::vector<SampleEntry> results;

    for (auto& [score, e] : scored)
    {
        if (static_cast<int> (results.size()) >= q.limit)
            break;

        results.push_back (*e);
    }

    return results;
}

Json SampleLibrary::toJson (const SampleEntry& e, bool includeFeatures)
{
    auto& f = e.features;
    auto& c = e.classification;

    Json j {
        { "path", e.path },
        { "description", describe (e) },
        { "category", c.category },
        { "loop", c.loop },
        { "durationSeconds", std::round (f.durationSeconds * 1000.0) / 1000.0 },
    };

    if (c.tempo)    j["tempo"] = *c.tempo;
    if (c.key)      j["key"] = *c.key;

    if (f.pitch && f.pitch->confidence > 0.7f)
        j["rootKey"] = f.pitch->key;

    if (includeFeatures)
    {
        j["features"] = library::toJson (f);
        j["classification"] = classificationToJson (c);
    }

    return j;
}

//==============================================================================
void SampleLibrary::load()
{
    std::ifstream in (indexFile);

    if (! in)
        return;

    try
    {
        auto j = Json::parse (in);

        if (j.value ("version", 0) != indexVersion)
            return; // an older index: rebuilt on the next scan

        for (auto& folder : j.value ("folders", Json::array()))
            folders.emplace_back (folder.get<std::string>());

        for (auto& s : j.value ("samples", Json::array()))
        {
            SampleEntry e;
            e.path = s.value ("path", "");
            e.size = s.value ("size", int64_t { 0 });
            e.modified = s.value ("modified", int64_t { 0 });
            e.features = featuresFromJson (s.value ("features", Json::object()));
            e.classification = classificationFromJson (s.value ("classification", Json::object()));
            entries[e.path] = std::move (e);
        }
    }
    catch (const std::exception&)
    {
        // A damaged index is rebuilt by the next scan.
        folders.clear();
        entries.clear();
    }
}

void SampleLibrary::save() const
{
    auto samples = Json::array();

    for (auto& [path, e] : entries)
        samples.push_back ({ { "path", e.path }, { "size", e.size }, { "modified", e.modified },
                             { "features", library::toJson (e.features) }, { "classification", classificationToJson (e.classification) } });

    auto folderList = Json::array();

    for (auto& f : folders)
        folderList.push_back (f.string());

    fs::create_directories (indexFile.parent_path());
    auto temp = indexFile;
    temp += ".tmp";

    {
        std::ofstream out (temp, std::ios::trunc);
        out << Json { { "version", indexVersion }, { "folders", folderList }, { "samples", samples } }.dump();
    }

    fs::rename (temp, indexFile);
}

} // namespace gk::library
