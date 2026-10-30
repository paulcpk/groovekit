#include "SampleLibrary.h"

#include "EngineContext.h"

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

    /** Index keys are canonical, so /var/... and /private/var/... are the same file. This takes
        a few system calls per path component, so it's for paths users give; a scan derives keys
        from the folders, which are canonical already. */
    std::string key (const fs::path& p)
    {
        return fs::weakly_canonical (fs::absolute (p)).string();
    }

    /** Fills what searches read on every entry, once. */
    void prepare (SampleEntry& e)
    {
        e.nameWords = tokenize (fs::path (e.path).stem().string());
        e.namedKey = e.classification.key ? music::parseKey (*e.classification.key) : std::nullopt;
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

        if (auto t = j.find ("tonality"); t != j.end())
            if (auto key = music::parseKey (t->value ("key", std::string())))
                f.tonality = SampleFeatures::Tonality { *key, t->value ("confidence", 0.0f) };

        f.version = j.value ("version", 0);

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

    /** An entry as the index file keeps it. */
    std::string entryJson (const SampleEntry& e)
    {
        return Json { { "path", e.path }, { "size", e.size }, { "modified", e.modified },
                      { "features", library::toJson (e.features) }, { "classification", classificationToJson (e.classification) } }.dump();
    }

    /** A sentence for agents: "kick one-shot, 0.30 s, pitched A1 (+12 c), fast attack, dark". */
    std::string describe (const SampleEntry& e, const SampleLibrary::SampleKey& key)
    {
        auto& f = e.features;
        auto& c = e.classification;
        std::string s = c.category + (c.loop ? " loop" : " one-shot");

        char duration[32];
        std::snprintf (duration, sizeof (duration), ", %.2f s", f.durationSeconds);
        s += duration;

        if (c.tempo)    s += ", " + juce::String (*c.tempo, 1).toStdString() + " BPM";

        if (key.key)
            s += ", " + music::keyName (*key.key) + (key.source == "sound" ? " (heard)" : key.heard ? " (sounds like " + music::keyName (*key.heard) + ")" : "");

        // Pitch and attack describe a single hit; in a loop they'd only describe its loudest moment.
        if (! c.loop && f.pitch && f.pitch->confidence > 0.7f)
        {
            const auto cents = static_cast<int> (std::lround (f.pitch->cents));
            s += ", pitched " + noteName (f.pitch->key) + (cents != 0 ? " (" + std::string (cents > 0 ? "+" : "") + std::to_string (cents) + " c)" : "");
        }

        if (! c.loop && f.attackMs <= 10)
            s += ", sharp attack";
        else if (! c.loop && f.attackMs >= 100)
            s += ", soft attack";

        if (f.centroidHz > 0)
            s += f.centroidHz < 500 ? ", dark" : f.centroidHz > 4000 ? ", bright" : "";

        if (f.bands.sub + f.bands.low > 0.6f)
            s += ", bass-heavy";

        return s;
    }

    bool keysMatch (const std::optional<music::Key>& entryKey, const std::optional<music::Key>& wanted)
    {
        return entryKey && wanted && entryKey->relativeMajor() == wanted->relativeMajor();
    }

    /** How a key (or a root) goes with a song's key: see SampleLibrary::fitTo. */
    std::optional<SampleLibrary::KeyFit> fitOf (const SampleLibrary::SampleKey& k, music::Key songKey)
    {
        if (k.key)
            return SampleLibrary::KeyFit { music::compatible (*k.key, songKey), music::semitonesBetween (*k.key, songKey) };

        if (k.root)
        {
            auto toTonic = ((songKey.tonic - *k.root) % 12 + 12) % 12;
            return SampleLibrary::KeyFit { songKey.contains (*k.root % 12), toTonic > 6 ? toTonic - 12 : toTonic };
        }

        return {};
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

    return fs::path (EngineContext::getAppDataFolder()) / "library.json";
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

    {
        std::lock_guard lock (mutex);

        if (std::find (folders.begin(), folders.end(), absolute) == folders.end())
            folders.push_back (absolute);
    }

    save();
}

void SampleLibrary::removeFolder (const fs::path& folder)
{
    auto absolute = key (folder);

    {
        std::lock_guard lock (mutex);
        folders.erase (std::remove (folders.begin(), folders.end(), fs::path (absolute)), folders.end());

        for (auto it = entries.begin(); it != entries.end();)
            it = it->first.starts_with (absolute + "/") ? entries.erase (it) : std::next (it);
    }

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

    // The folders are canonical (addFolder), so a file's key is its path below one made normal:
    // no system calls per file, where weakly_canonical would take one per path component.
    struct Found { fs::path path; std::string key; };
    std::vector<Found> files;

    for (auto& folder : getFolders())
    {
        std::error_code ec;

        for (auto it = fs::recursive_directory_iterator (folder, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment (ec))
        {
            if (ec || cancelScan)
                break;

            if (it->is_regular_file (ec) && isAudioFile (it->path()) && ! it->path().filename().string().starts_with ("._"))
                files.push_back ({ it->path(), it->path().lexically_normal().string() });
        }
    }

    std::set<std::string> present;

    for (auto& f : files)
        present.insert (f.key);

    {
        std::lock_guard lock (mutex);
        progress.filesFound = static_cast<int> (files.size());

        // Forget files that no longer exist.
        for (auto it = entries.begin(); it != entries.end();)
            it = present.contains (it->first) ? std::next (it) : entries.erase (it);
    }

    report (true);

    // Analysis is independent per file: spread it over the cores. Each worker merges what it
    // analysed into the index in batches, so searches and the other workers rarely wait on the
    // lock, and the index is saved now and then so an interrupted first scan of a big library
    // isn't lost.
    std::atomic<size_t> next { 0 };
    std::atomic<int> sinceSave { 0 };
    constexpr int saveEvery = 2000, mergeEvery = 200;

    auto work = [&]
    {
        std::vector<Stored> batch;

        auto merge = [&]
        {
            if (batch.empty())
                return;

            {
                std::lock_guard lock (mutex);

                for (auto& s : batch)
                {
                    auto path = s.entry.path;
                    entries.insert_or_assign (std::move (path), std::move (s));
                }
            }

            const auto count = static_cast<int> (batch.size());
            batch.clear();

            if (sinceSave.fetch_add (count) + count >= saveEvery)
            {
                sinceSave = 0;
                save();
            }
        };

        for (size_t i; ! cancelScan && (i = next++) < files.size();)
        {
            auto& [file, fileKey] = files[i];
            std::error_code ec;
            const auto size = static_cast<int64_t> (fs::file_size (file, ec));
            const auto modified = modifiedTime (file);

            {
                std::lock_guard lock (mutex);

                if (auto it = entries.find (fileKey); it != entries.end() && it->second.entry.size == size && it->second.entry.modified == modified
                                                      && it->second.entry.features.version >= SampleFeatures::currentVersion)
                {
                    ++progress.unchanged;
                    continue;
                }
            }

            try
            {
                auto entry = analyseEntry (file, fileKey);
                auto json = entryJson (entry);
                batch.push_back ({ std::move (entry), std::move (json) });
                std::lock_guard lock (mutex);
                ++progress.analysed;
            }
            catch (const std::exception&)
            {
                std::lock_guard lock (mutex);
                ++progress.failed;
            }

            if (batch.size() >= mergeEvery)
                merge();

            report (false);
        }

        merge();
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
    }

    save();
    report (true);
}

SampleEntry SampleLibrary::analyseFile (const fs::path& file)
{
    return analyseEntry (file, key (file));
}

SampleEntry SampleLibrary::analyseEntry (const fs::path& file, std::string fileKey)
{
    SampleEntry e;
    e.path = std::move (fileKey);
    e.size = static_cast<int64_t> (fs::file_size (file));
    e.modified = modifiedTime (file);

    // One reader for the analysed stretch and the file's real length (the analysis may only
    // cover the start).
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (e.path)));

    if (reader == nullptr)
        throw std::runtime_error ("Can't read audio file " + e.path);

    const auto length = std::min (reader->lengthInSamples, static_cast<juce::int64> (analysedSeconds * reader->sampleRate));
    analysis::Audio audio;
    audio.sampleRate = reader->sampleRate;
    audio.buffer.setSize (static_cast<int> (reader->numChannels), static_cast<int> (length));
    reader->read (&audio.buffer, 0, static_cast<int> (length), 0, true, true);

    e.features = analyseSample (audio, analysedSeconds);
    e.features.durationSeconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
    e.classification = classify (file, e.features);
    prepare (e);
    return e;
}

//==============================================================================
size_t SampleLibrary::size() const
{
    std::lock_guard lock (mutex);
    return entries.size();
}

std::vector<SampleEntry> SampleLibrary::getSamples() const
{
    std::lock_guard lock (mutex);
    std::vector<SampleEntry> samples;
    samples.reserve (entries.size());

    for (auto& [path, s] : entries)
        samples.push_back (s.entry);

    return samples;
}

void SampleLibrary::forEach (const std::function<void (const SampleEntry&)>& f) const
{
    std::lock_guard lock (mutex);

    for (auto& [path, s] : entries)
        f (s.entry);
}

std::optional<SampleEntry> SampleLibrary::find (const fs::path& path) const
{
    return findByKey (key (path));
}

std::optional<SampleEntry> SampleLibrary::findByKey (const std::string& pathKey) const
{
    std::lock_guard lock (mutex);

    if (auto it = entries.find (pathKey); it != entries.end())
        return it->second.entry;

    return {};
}

SampleLibrary::Listing SampleLibrary::list (const std::filesystem::path& folder) const
{
    // Finder order: case-insensitive, with numbers compared by value ("Kick 2" before "Kick 10").
    const auto finderOrder = [] (const std::string& a, const std::string& b)
    {
        return juce::String (a).compareNatural (juce::String (b)) < 0;
    };

    Listing listing;
    std::lock_guard lock (mutex);

    // How many samples lie under a folder; entries are sorted by path.
    const auto countBelow = [this] (const std::string& prefix)
    {
        size_t n = 0;

        for (auto it = entries.lower_bound (prefix); it != entries.end() && it->first.starts_with (prefix); ++it)
            ++n;

        return n;
    };

    if (folder.empty())
    {
        for (auto& f : folders)
            listing.folders.push_back ({ f, countBelow (f.string() + "/") });

        return listing;
    }

    auto dir = folder.lexically_normal().string();

    while (dir.size() > 1 && dir.ends_with ("/"))
        dir.pop_back();

    const auto prefix = dir + "/";
    std::map<std::string, size_t, std::less<>> subfolders;

    for (auto it = entries.lower_bound (prefix); it != entries.end() && it->first.starts_with (prefix); ++it)
    {
        const auto rest = std::string_view (it->first).substr (prefix.size());

        if (const auto slash = rest.find ('/'); slash != std::string_view::npos)
        {
            const auto name = rest.substr (0, slash);

            if (auto f = subfolders.find (name); f != subfolders.end())
                ++f->second;
            else
                subfolders.emplace (name, 1);
        }
        else
            listing.samples.push_back (it->second.entry);
    }

    for (auto& [name, count] : subfolders)
        listing.folders.push_back ({ std::filesystem::path (prefix + name), count });

    std::sort (listing.folders.begin(), listing.folders.end(), [&] (auto& a, auto& b)
    {
        return finderOrder (a.path.filename().string(), b.path.filename().string());
    });

    std::sort (listing.samples.begin(), listing.samples.end(), [&] (auto& a, auto& b)
    {
        return finderOrder (std::filesystem::path (a.path).filename().string(), std::filesystem::path (b.path).filename().string());
    });

    return listing;
}

std::vector<SampleEntry> SampleLibrary::search (const SampleQuery& q) const
{
    const auto words = tokenize (q.text);
    const auto wantedKey = q.key ? music::parseKey (*q.key) : std::nullopt;
    std::vector<std::pair<int, const SampleEntry*>> scored;

    std::lock_guard lock (mutex);

    for (auto& [path, s] : entries)
    {
        auto& e = s.entry;
        auto& c = e.classification;
        auto& f = e.features;

        if (q.category && c.category != *q.category)                    continue;
        if (q.loop && c.loop != *q.loop)                                continue;
        if (q.minDuration && f.durationSeconds < *q.minDuration)        continue;
        if (q.maxDuration && f.durationSeconds > *q.maxDuration)        continue;
        if (q.key && ! keysMatch (keyOf (e).key, wantedKey))            continue;

        if (q.fitsKey)
            if (auto fit = fitTo (e, *q.fitsKey); ! fit || ! fit->fits)
                continue;

        if (q.tempo)
        {
            if (! c.tempo)
                continue;

            auto near = [&] (double t) { return std::abs (t - *q.tempo) <= q.tempoTolerance; };

            if (! near (*c.tempo) && ! near (*c.tempo * 2) && ! near (*c.tempo / 2))
                continue;
        }

        const auto& nameWords = e.nameWords;
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

    // Among equally good matches, alternate between packs (the first folder below the library
    // folder) so one pack doesn't fill every result.
    std::vector<std::string> roots;

    for (auto& folder : folders)
        roots.push_back (folder.string() + "/");

    auto packOf = [&roots] (const std::string& path)
    {
        for (auto& root : roots)
            if (path.starts_with (root))
                return path.substr (root.size(), path.find ('/', root.size()) - root.size());

        return std::string();
    };

    std::map<std::pair<int, std::string>, int> seenPerPack;
    std::vector<std::tuple<int, int, const SampleEntry*>> ranked; // score, rank within its pack, entry

    for (auto& [score, e] : scored)
        ranked.emplace_back (score, seenPerPack[{ score, packOf (e->path) }]++, e);

    std::stable_sort (ranked.begin(), ranked.end(), [] (auto& a, auto& b)
    {
        return std::get<0> (a) != std::get<0> (b) ? std::get<0> (a) > std::get<0> (b) : std::get<1> (a) < std::get<1> (b);
    });

    std::vector<SampleEntry> results;
    std::set<std::pair<std::string, int64_t>> seenFiles; // copies of a file in several folders show once

    for (auto& [score, rank, e] : ranked)
    {
        if (static_cast<int> (results.size()) >= q.limit)
            break;

        if (seenFiles.insert ({ fs::path (e->path).filename().string(), e->size }).second)
            results.push_back (*e);
    }

    return results;
}

SampleLibrary::SampleKey SampleLibrary::keyOf (const SampleEntry& e)
{
    auto& f = e.features;
    auto& c = e.classification;
    SampleKey k;

    // The name's key, parsed once by the library; entries made by hand haven't been through it.
    const auto named = ! c.key ? std::nullopt : e.namedKey ? e.namedKey : music::parseKey (*c.key);

    // A key is heard in what has harmony or a line to it; a drum hit's "key" is its root, and a
    // drum loop's only counts when it's very clear (tuned toms, a pitched perc line).
    static const std::set<std::string> drums { "kick", "snare", "clap", "hat", "cymbal", "tom", "perc", "drums" };
    const bool tonal = c.loop || f.durationSeconds >= 1.5 || c.category == "synth" || c.category == "bass" || c.category == "vocal";
    const auto needed = drums.contains (c.category) ? 0.6f : 0.25f;
    const auto heard = tonal && f.tonality && f.tonality->confidence >= needed ? std::optional (f.tonality->key) : std::nullopt;

    if (named)
    {
        k.key = named;
        k.source = heard && heard->relativeMajor() == named->relativeMajor() ? "name and sound" : "name";

        if (heard && heard->relativeMajor() != named->relativeMajor())
            k.heard = heard;
    }
    else if (heard)
    {
        k.key = heard;
        k.source = "sound";
    }

    if (! c.loop && f.pitch && f.pitch->confidence > 0.7f)
        k.root = f.pitch->key;

    return k;
}

std::optional<SampleLibrary::KeyFit> SampleLibrary::fitTo (const SampleEntry& e, music::Key songKey)
{
    return fitOf (keyOf (e), songKey);
}

Json SampleLibrary::toJson (const SampleEntry& e, bool includeFeatures, std::optional<music::Key> songKey)
{
    auto& f = e.features;
    auto& c = e.classification;
    const auto key = keyOf (e);

    Json j {
        { "path", e.path },
        { "description", describe (e, key) },
        { "category", c.category },
        { "loop", c.loop },
        { "durationSeconds", std::round (f.durationSeconds * 1000.0) / 1000.0 },
    };

    if (c.tempo)    j["tempo"] = *c.tempo;

    if (c.loop)
        if (auto t = loopTempo (e))
            j["sourceTempo"] = std::round (*t * 1000) / 1000;

    if (key.key)
    {
        j["key"] = music::keyName (*key.key);
        j["keySource"] = key.source;

        if (key.heard)
            j["heardKey"] = music::keyName (*key.heard);
    }

    if (key.root)
        j["rootKey"] = *key.root;

    if (songKey)
        if (auto fit = fitOf (key, *songKey))
            j["keyFit"] = { { "fits", fit->fits }, { "transpose", fit->transpose } };

    if (includeFeatures)
    {
        j["features"] = library::toJson (f);
        j["classification"] = classificationToJson (c);
    }

    return j;
}

std::optional<double> SampleLibrary::loopTempo (const SampleEntry& e)
{
    const auto tempo = e.classification.tempo ? e.classification.tempo : e.features.estimatedTempo;
    const auto seconds = e.features.durationSeconds;

    if (! tempo || *tempo <= 0)
        return {};

    if (seconds <= 0)
        return tempo;

    const auto beats = seconds * *tempo / 60;
    const auto whole = beats >= 4 ? 4 * std::round (beats / 4) : std::round (beats);

    if (whole >= 1)
    {
        const auto exact = whole * 60 / seconds;

        if (std::abs (exact - *tempo) / *tempo < 0.03)
            return exact;
    }

    return tempo;
}

//==============================================================================
void SampleLibrary::load()
{
    std::ifstream in (indexFile);

    if (! in)
        return;

    // Read one sample at a time rather than the whole index as a document: a big library's
    // index is tens of megabytes, and many times that as parsed JSON.
    std::vector<SampleEntry> loaded;

    const Json::parser_callback_t keepOnlyHeader = [&] (int depth, Json::parse_event_t event, Json& parsed)
    {
        if (event == Json::parse_event_t::object_end && depth == 2 && parsed.contains ("path"))
        {
            SampleEntry e;
            e.path = parsed.value ("path", "");
            e.size = parsed.value ("size", int64_t { 0 });
            e.modified = parsed.value ("modified", int64_t { 0 });
            e.features = featuresFromJson (parsed.value ("features", Json::object()));
            e.classification = classificationFromJson (parsed.value ("classification", Json::object()));
            loaded.push_back (std::move (e));
            return false;
        }

        return true;
    };

    try
    {
        auto j = Json::parse (in, keepOnlyHeader);

        if (j.value ("version", 0) != indexVersion)
            return; // an older index: rebuilt on the next scan

        for (auto& folder : j.value ("folders", Json::array()))
            folders.emplace_back (folder.get<std::string>());

        autoScan = j.value ("autoScan", true);

        for (auto& e : loaded)
        {
            prepare (e);
            auto path = e.path;
            entries.emplace (std::move (path), Stored { std::move (e), {} });
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
    // One writer at a time: a save that waits for another writes the later state, so nothing
    // is lost, and the two never write the same temporary file.
    std::lock_guard writing (saveMutex);

    std::string header;
    std::vector<std::string> lines;
    std::vector<std::pair<size_t, SampleEntry>> unserialised;   // loaded entries: serialised on the first save

    {
        std::lock_guard lock (mutex);
        auto folderList = Json::array();

        for (auto& f : folders)
            folderList.push_back (f.string());

        header = "{\"version\":" + std::to_string (indexVersion) + ",\"autoScan\":" + (autoScan ? "true" : "false")
               + ",\"folders\":" + folderList.dump() + ",\"samples\":[";
        lines.reserve (entries.size());

        for (auto& [path, s] : entries)
        {
            if (s.json.empty())
                unserialised.emplace_back (lines.size(), s.entry);

            lines.push_back (s.json);
        }
    }

    for (auto& [i, e] : unserialised)
        lines[i] = entryJson (e);

    fs::create_directories (indexFile.parent_path());
    auto temp = indexFile;
    temp += ".tmp";

    {
        // Written a sample at a time, for the same reason load() reads it that way.
        std::ofstream out (temp, std::ios::trunc);
        out << header;
        bool first = true;

        for (auto& line : lines)
        {
            out << (first ? "" : ",") << line;
            first = false;
        }

        out << "]}";
    }

    fs::rename (temp, indexFile);

    // Keep what was serialised for the next save, where the entry is still the same one.
    if (! unserialised.empty())
    {
        std::lock_guard lock (mutex);

        for (auto& [i, e] : unserialised)
            if (auto it = entries.find (e.path); it != entries.end() && it->second.json.empty()
                                                 && it->second.entry.size == e.size && it->second.entry.modified == e.modified)
                it->second.json = std::move (lines[i]);
    }
}

bool SampleLibrary::getAutoScan() const
{
    std::lock_guard lock (mutex);
    return autoScan;
}

void SampleLibrary::setAutoScan (bool enabled)
{
    {
        std::lock_guard lock (mutex);
        autoScan = enabled;
    }

    save();
}

} // namespace gk::library
