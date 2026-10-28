#include "Styles.h"

#include "EngineContext.h"
#include "Maps.h"
#include "analysis/Analysis.h"
#include "library/SampleFeatures.h"

#include <fstream>
#include <numeric>
#include <set>

namespace gk::listening
{

using model::Json;

namespace
{
    constexpr int fileVersion = 1;
    constexpr int analysisVersion = 6;      // 6: the whole track, with its arrangement
    constexpr int windowsPerTrack = 6;
    constexpr int windowSamples = 343980;  // one separator chunk, 7.8 s at 44.1 kHz
    constexpr float silentDb = -50.0f;
    constexpr size_t neighbours = 8;

    const std::pair<Role, const char*> roleNames[] = {
        { Role::mix, "mix" }, { Role::drums, "drums" }, { Role::kick, "kick" }, { Role::hats, "hats" }, { Role::bass, "bass" },
        { Role::music, "music" }, { Role::kickHit, "kickHit" }, { Role::hatHit, "hatHit" }, { Role::percHit, "percHit" },
    };

    bool isAudioFile (const std::filesystem::path& p)
    {
        auto ext = juce::String (p.extension().string()).toLowerCase();
        return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3" || ext == ".ogg" || ext == ".m4a";
    }

    int64_t modifiedTime (const std::filesystem::path& p)
    {
        return static_cast<int64_t> (std::filesystem::last_write_time (p).time_since_epoch().count());
    }

    /** The audio files in a folder and the folders inside it, sorted. */
    std::vector<std::filesystem::path> audioFilesIn (const std::filesystem::path& folder)
    {
        std::vector<std::filesystem::path> files;
        std::error_code ec;

        for (auto it = std::filesystem::recursive_directory_iterator (folder, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment (ec))
        {
            if (ec)
                break;

            if (it->is_regular_file (ec) && isAudioFile (it->path()) && ! it->path().filename().string().starts_with ("._"))
                files.push_back (it->path());
        }

        std::sort (files.begin(), files.end());
        return files;
    }

    std::string encode (const Embedding& e)
    {
        return juce::Base64::toBase64 (e.data(), e.size() * sizeof (float)).toStdString();
    }

    Embedding decode (const std::string& s)
    {
        juce::MemoryOutputStream out;
        Embedding e {};

        if (juce::Base64::convertFromBase64 (out, juce::String (s)) && out.getDataSize() == e.size() * sizeof (float))
            std::memcpy (e.data(), out.getData(), out.getDataSize());

        return e;
    }

    float rmsDb (const std::vector<float>& x)
    {
        double sum = 0;

        for (auto v : x)
            sum += static_cast<double> (v) * v;

        return x.empty() || sum <= 0 ? analysis::silenceDb : static_cast<float> (10.0 * std::log10 (sum / static_cast<double> (x.size())));
    }

    /** A 4th-order Butterworth low- or high-pass, as two biquads. */
    std::vector<float> filter (std::vector<float> x, double sampleRate, double hz, bool highPass)
    {
        for (auto q : { 0.5412, 1.3066 })
        {
            auto c = highPass ? juce::IIRCoefficients::makeHighPass (sampleRate, hz, q) : juce::IIRCoefficients::makeLowPass (sampleRate, hz, q);
            juce::IIRFilter f;
            f.setCoefficients (c);
            f.processSamples (x.data(), static_cast<int> (x.size()));
        }

        return x;
    }

    analysis::Audio toAudio (juce::AudioBuffer<float> buffer, double sampleRate)
    {
        analysis::Audio a;
        a.buffer = std::move (buffer);
        a.sampleRate = sampleRate;
        return a;
    }

    template <typename T>
    T median (std::vector<T> v)
    {
        if (v.empty())
            return {};

        std::nth_element (v.begin(), v.begin() + static_cast<long> (v.size() / 2), v.end());
        return v[v.size() / 2];
    }

    double round (double v, double step)
    {
        return std::round (v / step) * step;
    }
}

std::string roleName (Role r)
{
    for (auto& [role, name] : roleNames)
        if (role == r)
            return name;

    return "mix";
}

std::optional<Role> roleFromName (const std::string& s)
{
    for (auto& [role, name] : roleNames)
        if (s == name)
            return role;

    return {};
}

Role roleOf (const std::string& category, bool loop)
{
    if (category == "kick")                         return loop ? Role::kick : Role::kickHit;
    if (category == "hat" || category == "cymbal")  return loop ? Role::hats : Role::hatHit;
    if (category == "drums" || category == "perc" || category == "clap" || category == "snare" || category == "tom")
        return loop ? Role::drums : Role::percHit;
    if (category == "bass")                         return Role::bass;
    return Role::music;
}

//==============================================================================
bool StyleProfile::has (Role role) const
{
    return std::any_of (tracks.begin(), tracks.end(), [&] (auto& t) { return t.sounds.contains (role) && ! t.sounds.at (role).empty(); });
}

float StyleProfile::fit (Role role, const Embedding& e, std::optional<size_t> leaveOut) const
{
    std::vector<float> similarities;

    for (size_t i = 0; i < tracks.size(); ++i)
        if (i != leaveOut)
            if (auto it = tracks[i].sounds.find (role); it != tracks[i].sounds.end())
                for (auto& s : it->second)
                    similarities.push_back (similarity (s, e));

    if (similarities.empty())
        return 0;

    const auto k = std::min (neighbours, similarities.size());
    std::partial_sort (similarities.begin(), similarities.begin() + static_cast<long> (k), similarities.end(), std::greater<>());
    return std::accumulate (similarities.begin(), similarities.begin() + static_cast<long> (k), 0.0f) / static_cast<float> (k);
}

Json StyleProfile::Track::toJson() const
{
    Json levelsJson = Json::object(), soundsJson = Json::object();

    for (auto& [role, db] : levels)
        levelsJson[roleName (role)] = round (db, 0.1);

    for (auto& [role, list] : sounds)
    {
        auto encoded = Json::array();

        for (auto& e : list)
            encoded.push_back (encode (e));

        soundsJson[roleName (role)] = encoded;
    }

    Json j { { "path", path }, { "size", size }, { "modified", modified }, { "analysis", analysis }, { "tempo", tempo }, { "density", round (density, 0.01) },
             { "lufs", round (lufs, 0.1) }, { "balance", balance }, { "levels", levelsJson }, { "sounds", soundsJson } };

    if (swing)
        j["swing"] = round (*swing, 0.01);

    if (arrangement)
        j["arrangement"] = arrangement->toJson();

    return j;
}

StyleProfile::Track StyleProfile::Track::fromJson (const Json& tj)
{
    Track t;
    t.path = tj.value ("path", "");
    t.size = tj.value ("size", int64_t { 0 });
    t.modified = tj.value ("modified", int64_t { 0 });
    t.analysis = tj.value ("analysis", 1);
    t.tempo = tj.value ("tempo", 0.0);
    t.density = tj.value ("density", 0.0);
    t.lufs = tj.value ("lufs", 0.0f);
    t.balance = tj.value ("balance", std::vector<float>());

    if (tj.contains ("swing"))
        t.swing = tj["swing"].get<double>();

    if (tj.contains ("arrangement"))
        t.arrangement = Arrangement::fromJson (tj["arrangement"]);

    // Held here: items() only refers to the object it's given.
    const auto levels = tj.value ("levels", Json::object());
    const auto sounds = tj.value ("sounds", Json::object());

    for (auto& [name, db] : levels.items())
        if (auto role = roleFromName (name))
            t.levels[*role] = db.get<float>();

    for (auto& [name, list] : sounds.items())
        if (auto role = roleFromName (name))
            for (auto& s : list)
                t.sounds[*role].push_back (decode (s.get<std::string>()));

    return t;
}

Json StyleProfile::toJson() const
{
    auto trackList = Json::array();

    for (auto& t : tracks)
        trackList.push_back (t.toJson());

    Json typical = Json::object();

    for (auto& [role, f] : typicalFit)
        typical[roleName (role)] = f;

    return { { "version", fileVersion }, { "name", name }, { "folder", folder.string() }, { "tracks", trackList }, { "typicalFit", typical } };
}

StyleProfile StyleProfile::fromJson (const Json& j)
{
    StyleProfile p;
    p.name = j.value ("name", "");
    p.folder = j.value ("folder", "");

    for (auto& tj : j.value ("tracks", Json::array()))
        p.tracks.push_back (Track::fromJson (tj));

    const auto typical = j.value ("typicalFit", Json::object());

    for (auto& [name, f] : typical.items())
        if (auto role = roleFromName (name))
            p.typicalFit[*role] = f.get<float>();

    return p;
}

Json StyleProfile::measurements() const
{
    std::vector<double> tempos, swings, densities, lufs;

    for (auto& t : tracks)
    {
        if (t.tempo > 0) tempos.push_back (t.tempo);
        if (t.swing)     swings.push_back (*t.swing);
        densities.push_back (t.density);
        lufs.push_back (t.lufs);
    }

    Json j { { "tracks", tracks.size() } };

    if (! tempos.empty())
        j["tempo"] = { { "median", median (tempos) }, { "min", *std::min_element (tempos.begin(), tempos.end()) },
                       { "max", *std::max_element (tempos.begin(), tempos.end()) } };

    if (! swings.empty())
        j["swing"] = round (median (swings), 0.01);

    if (! densities.empty())
    {
        j["onsetsPerBeat"] = round (median (densities), 0.1);
        j["lufs"] = round (median (lufs), 0.1);
    }

    // The mix's balance, band by band (the analysis's ten bands), and each part's level.
    std::vector<double> balance;

    for (auto& t : tracks)
        for (size_t b = 0; b < t.balance.size(); ++b)
        {
            balance.resize (std::max (balance.size(), t.balance.size()), 0.0);
            balance[b] += t.balance[b] / static_cast<double> (tracks.size());
        }

    auto balanceJson = Json::array();

    for (auto v : balance)
        balanceJson.push_back (round (v, 0.1));

    j["balance"] = balanceJson;

    Json levels = Json::object();

    for (auto role : { Role::drums, Role::kick, Role::hats, Role::bass, Role::music })
    {
        std::vector<float> values;

        for (auto& t : tracks)
            if (auto it = t.levels.find (role); it != t.levels.end())
                values.push_back (it->second);

        if (! values.empty())
            levels[roleName (role)] = round (median (values), 0.1);
    }

    j["levels"] = levels;
    return j;
}

int currentAnalysisVersion()
{
    return analysisVersion;
}

//==============================================================================
namespace
{
    int64_t now()
    {
        return juce::Time::currentTimeMillis();
    }

    std::string lower (const std::string& s)
    {
        return juce::String (s).toLowerCase().toStdString();
    }

    std::string songId (const std::string& style, const std::filesystem::path& file)
    {
        return style + "/" + file.stem().string();
    }

    /** Whether a map was heard from this file, unchanged: by its name, size and date rather than
        its path, so a folder that moved isn't heard again. */
    bool isSameFile (const StyleProfile::Track& t, const std::filesystem::path& file)
    {
        std::error_code ec;
        const auto size = static_cast<int64_t> (std::filesystem::file_size (file, ec));
        return std::filesystem::path (t.path).filename() == file.filename() && t.size == size && t.modified == modifiedTime (file);
    }

    /** The style folders in a references folder. */
    std::vector<std::pair<std::string, std::filesystem::path>> styleFoldersIn (const std::filesystem::path& root)
    {
        static const std::set<std::string> notStyles { "albums", "mixes", "not used" };
        std::vector<std::pair<std::string, std::filesystem::path>> folders;
        std::error_code ec;

        for (auto& entry : std::filesystem::directory_iterator (root, ec))
        {
            const auto name = entry.path().filename().string();

            if (entry.is_directory (ec) && ! name.starts_with (".") && ! notStyles.contains (lower (name)))
                folders.emplace_back (name, entry.path());
        }

        return folders;
    }

    bool naturalLess (const std::string& a, const std::string& b)
    {
        return juce::String (a).compareNatural (juce::String (b)) < 0;
    }
}

Styles::Styles (const Models& m, Clap& c, std::filesystem::path mapsFolder, std::filesystem::path oldProfilesFolder)
    : models (m), clap (c), maps (std::make_unique<MapStore> (std::move (mapsFolder)))
{
    if (! oldProfilesFolder.empty())
        migrate (oldProfilesFolder);

    loadQueue();
}

Styles::~Styles()
{
    stop();
}

void Styles::stop()
{
    cancel = true;

    if (thread.joinable())
        thread.join();

    cancel = false;
}

void Styles::wait()
{
    for (;;)
    {
        {
            std::lock_guard lock (mutex);

            if (! progress.running)
                break;
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }
}

std::filesystem::path Styles::getDefaultFolder()
{
    return EngineContext::getAppDataFolder() / "styles";
}

std::filesystem::path Styles::getReferencesFolder()
{
    if (auto env = std::getenv ("GROOVEKIT_REFERENCES"))
        return env;

    auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    return std::filesystem::path (music.getFullPathName().toStdString()) / "GrooveKit" / "References";
}

void Styles::setObserver (Observer o)
{
    std::lock_guard lock (mutex);
    observer = std::move (o);
}

void Styles::notify()
{
    Observer o;
    {
        std::lock_guard lock (mutex);
        o = observer;
    }

    if (o)
        o();
}

//==============================================================================
std::vector<Styles::Song> Styles::songs (const std::filesystem::path& root) const
{
    // The references folder's style folders, and folders styles were heard from elsewhere.
    std::map<std::string, std::filesystem::path> folders;
    std::error_code ec;

    for (auto& [name, path] : styleFoldersIn (root))
        folders[name] = path;

    for (auto& map : maps->all())
        if (! folders.contains (map->style))
            if (auto folder = maps->getStyleFolder (map->style); folder && std::filesystem::is_directory (*folder, ec))
                folders[map->style] = *folder;

    const auto now = getProgress();
    std::vector<Song> list;
    std::set<std::string> seen;

    auto queued = [&] (const std::string& id)
    {
        return std::any_of (now.queued.begin(), now.queued.end(), [&] (auto& i) { return i.id == id; });
    };

    auto failure = [&] (const std::string& id) -> const Progress::Done*
    {
        for (auto& d : now.recent)
            if (d.item.id == id)
                return d.ok ? nullptr : &d;

        return nullptr;
    };

    for (auto& [style, folder] : folders)
    {
        for (auto& file : audioFilesIn (folder))
        {
            Song s;
            s.style = style;
            s.title = file.stem().string();
            s.id = songId (style, file);

            if (! seen.insert (s.id).second)
                continue;

            s.file = file;
            s.map = maps->get (s.id);
            s.inUse = maps->isInUse (s.id);

            if (now.current && now.current->id == s.id)
                s.status = "hearing";
            else if (queued (s.id))
                s.status = "queued";
            else if (s.map != nullptr && isSameFile (s.map->track, file))
                s.status = s.map->track.analysis < analysisVersion ? "older" : "heard";
            else if (s.map != nullptr)
                s.status = s.map->source == "imported" ? "imported" : "changed";
            else if (auto failed = failure (s.id))
                s.status = "failed", s.error = failed->error;
            else
                s.status = maps->isSkipped (s.id) ? "skipped" : "new";

            list.push_back (std::move (s));
        }
    }

    for (auto& map : maps->all())
    {
        if (seen.contains (map->id()))
            continue;

        Song s { map->id(), map->style, map->title, {}, map, map->source == "imported" ? "imported" : "gone", maps->isInUse (map->id()), {} };
        list.push_back (std::move (s));
    }

    std::sort (list.begin(), list.end(), [] (auto& a, auto& b)
    {
        return a.style != b.style ? naturalLess (a.style, b.style) : naturalLess (a.title, b.title);
    });

    return list;
}

std::vector<Styles::ReferenceFolder> Styles::referenceFolders (const std::filesystem::path& root) const
{
    std::vector<ReferenceFolder> folders;
    const auto all = songs (root);
    const auto heardStyles = names();

    for (auto& [name, path] : styleFoldersIn (root))
    {
        ReferenceFolder folder { name, path };

        for (auto& s : all)
        {
            if (s.style != name)
                continue;

            folder.files += s.file ? 1 : 0;
            folder.unheard += s.status == "new" || s.status == "changed" || s.status == "failed" ? 1 : 0;
            folder.gone += s.status == "gone" ? 1 : 0;
        }

        if (std::find (heardStyles.begin(), heardStyles.end(), name) != heardStyles.end())
            folder.style = name;

        folders.push_back (std::move (folder));
    }

    std::sort (folders.begin(), folders.end(), [] (auto& a, auto& b) { return naturalLess (a.name, b.name); });
    return folders;
}

//==============================================================================
std::vector<std::string> Styles::names() const
{
    std::set<std::string> styles;

    for (auto& map : maps->all())
        if (maps->isInUse (map->id()))
            styles.insert (map->style);

    return { styles.begin(), styles.end() };
}

std::shared_ptr<const StyleProfile> Styles::get (const std::string& name) const
{
    const auto version = maps->getVersion();
    std::vector<std::shared_ptr<const SongMap>> inUse;

    for (auto& map : maps->all())
        if (lower (map->style) == lower (name) && maps->isInUse (map->id()))
            inUse.push_back (map);

    if (inUse.empty())
        return {};

    {
        std::lock_guard lock (cacheMutex);

        if (auto it = cache.find (lower (name)); it != cache.end() && it->second.version == version)
            return it->second.profile;
    }

    auto profile = std::make_shared<StyleProfile>();
    profile->name = inUse.front()->style;
    profile->folder = maps->getStyleFolder (profile->name).value_or (std::filesystem::path());

    for (auto& map : inUse)
        profile->tracks.push_back (map->track);

    // How close each track's sounds are to the other tracks': the yardstick for fit.
    if (profile->tracks.size() > 1)
    {
        for (auto role : allRoles)
        {
            double sum = 0;
            int count = 0;

            for (size_t i = 0; i < profile->tracks.size(); ++i)
                if (auto it = profile->tracks[i].sounds.find (role); it != profile->tracks[i].sounds.end())
                    for (auto& s : it->second)
                        sum += profile->fit (role, s, i), ++count;

            if (count > 0)
                profile->typicalFit[role] = static_cast<float> (sum / count);
        }
    }

    std::lock_guard lock (cacheMutex);
    cache[lower (name)] = { version, profile };
    return profile;
}

//==============================================================================
std::string Styles::add (const std::filesystem::path& tracksFolder, std::string name)
{
    std::error_code ec;
    const auto absolute = std::filesystem::weakly_canonical (std::filesystem::absolute (tracksFolder), ec);

    if (! std::filesystem::is_directory (absolute))
        throw std::runtime_error ("Not a folder: " + tracksFolder.string());

    if (name.empty())
        name = absolute.filename().string();

    const auto files = audioFilesIn (absolute);

    if (files.empty())
        throw std::runtime_error ("There are no audio files in " + absolute.string());

    maps->setStyleFolder (name, absolute);

    // New tracks and changed ones; skipped tracks wait until they're asked for by name.
    std::vector<std::pair<std::string, std::filesystem::path>> todo;

    for (auto& file : files)
    {
        const auto id = songId (name, file);
        auto map = maps->get (id);

        if ((map == nullptr || ! isSameFile (map->track, file)) && ! maps->isSkipped (id))
            todo.emplace_back (name, file);
    }

    hear (todo);
    return name;
}

void Styles::hear (const std::vector<std::pair<std::string, std::filesystem::path>>& songs)
{
    std::vector<std::string> ids;
    {
        std::lock_guard lock (mutex);

        if (progress.queued.empty() && ! progress.current)
        {
            progress.tracks = progress.analysed = progress.failed = 0;
            progress.error.clear();
        }

        for (auto& [style, file] : songs)
        {
            Progress::Item item { songId (style, file), style, file.stem().string(), file };
            ids.push_back (item.id);

            if ((progress.current && progress.current->id == item.id)
                || std::any_of (progress.queued.begin(), progress.queued.end(), [&] (auto& i) { return i.id == item.id; }))
                continue;

            progress.queued.push_back (std::move (item));
            ++progress.tracks;
        }

        saveQueue();
    }

    maps->setSkipped (ids, false);
    start();
    notify();
}

void Styles::start()
{
    std::unique_lock lock (mutex);

    if (progress.running || progress.paused || progress.queued.empty() || ! models.getStatus().ready)
        return;

    // Claimed before the lock is let go: a hearing that just finished may still be reporting,
    // which takes the lock.
    progress.running = true;
    auto finished = std::move (thread);
    lock.unlock();

    if (finished.joinable())
        finished.join();

    lock.lock();
    thread = std::thread ([this] { run(); });
}

void Styles::pause()
{
    {
        std::lock_guard lock (mutex);
        progress.paused = true;
        pausing = true;
        saveQueue();
    }

    notify();
}

void Styles::resume()
{
    {
        std::lock_guard lock (mutex);
        progress.paused = false;
        pausing = false;
        saveQueue();
    }

    start();
    notify();
}

void Styles::unqueue (const std::vector<std::string>& ids)
{
    {
        std::lock_guard lock (mutex);
        const std::set<std::string> gone (ids.begin(), ids.end());
        const auto before = progress.queued.size();
        std::erase_if (progress.queued, [&] (auto& i) { return gone.contains (i.id); });
        progress.tracks -= static_cast<int> (before - progress.queued.size());
        saveQueue();
    }

    notify();
}

void Styles::clearQueue()
{
    {
        std::lock_guard lock (mutex);
        progress.tracks -= static_cast<int> (progress.queued.size());
        progress.queued.clear();
        saveQueue();
    }

    notify();
}

void Styles::forget (const std::vector<std::string>& ids)
{
    unqueue (ids);

    for (auto& id : ids)
        maps->remove (id);

    maps->setSkipped (ids, true);
    notify();
}

Styles::Progress Styles::getProgress() const
{
    std::lock_guard lock (mutex);
    return progress;
}

void Styles::run()
{
    std::unique_ptr<Separator> separator;

    for (;;)
    {
        Progress::Item item;
        {
            std::lock_guard lock (mutex);

            if (cancel || pausing || progress.queued.empty())
            {
                progress.running = false;
                break;
            }

            item = progress.queued.front();
            progress.queued.erase (progress.queued.begin());
            progress.current = item;
            progress.currentProgress = 0;
            progress.currentStarted = now();
            progress.style = item.style;
            saveQueue();
        }

        notify();

        const auto started = now();
        std::string error;
        bool stopped = false;

        try
        {
            if (separator == nullptr)
                separator = std::make_unique<Separator> (models);

            auto track = hearTrack (*separator, clap, item.file, {}, [&] (double fraction)
            {
                if (cancel || pausing)
                    return false;

                {
                    std::lock_guard lock (mutex);
                    progress.currentProgress = fraction;
                }

                notify();
                return true;
            }, true);

            std::error_code ec;
            track.size = static_cast<int64_t> (std::filesystem::file_size (item.file, ec));
            track.modified = modifiedTime (item.file);
            track.analysis = analysisVersion;

            SongMap map;
            map.style = item.style;
            map.title = item.title;
            map.heardAt = now();
            map.track = std::move (track);
            maps->put (std::move (map));
        }
        catch (const std::exception& e)
        {
            stopped = cancel || pausing;
            error = e.what();
        }

        {
            std::lock_guard lock (mutex);
            progress.current.reset();

            if (stopped)
            {
                // Heard again from the start next time.
                progress.queued.insert (progress.queued.begin(), item);
            }
            else
            {
                const auto t = now();
                progress.recent.insert (progress.recent.begin(), { item, error.empty(), error, (t - started) / 1000.0, t });

                if (progress.recent.size() > 200)
                    progress.recent.resize (200);

                if (error.empty())
                    ++progress.analysed;
                else
                    ++progress.failed, progress.error = error;

                // The time a song takes, from the last ten heard.
                std::vector<double> took;

                for (auto& d : progress.recent)
                    if (d.ok && took.size() < 10)
                        took.push_back (d.seconds);

                if (! took.empty())
                    progress.secondsPerSong = std::accumulate (took.begin(), took.end(), 0.0) / static_cast<double> (took.size());
            }

            saveQueue();
        }

        notify();
    }

    notify();
}

//==============================================================================
void Styles::saveQueue()
{
    auto songs = Json::array();

    if (progress.current)
        songs.push_back ({ { "style", progress.current->style }, { "file", progress.current->file.string() } });

    for (auto& item : progress.queued)
        songs.push_back ({ { "style", item.style }, { "file", item.file.string() } });

    try
    {
        std::filesystem::create_directories (maps->getFolder());
        const auto file = maps->getFolder() / "queue.json";
        const auto temp = file.string() + ".tmp";
        {
            std::ofstream out (temp, std::ios::trunc);
            out << Json { { "paused", progress.paused }, { "songs", songs } }.dump (2);
        }
        std::filesystem::rename (temp, file);
    }
    catch (const std::exception&)
    {
        // The queue lives on here; only a restart would lose it.
    }
}

void Styles::loadQueue()
{
    std::lock_guard lock (mutex);
    std::error_code ec;

    try
    {
        std::ifstream in (maps->getFolder() / "queue.json");

        if (! in)
            return;

        const auto j = Json::parse (in);
        progress.paused = j.value ("paused", false);
        pausing = progress.paused;

        for (auto& s : j.value ("songs", Json::array()))
        {
            const std::filesystem::path file = s.value ("file", "");
            const auto style = s.value ("style", "");

            if (! style.empty() && std::filesystem::is_regular_file (file, ec))
                progress.queued.push_back ({ songId (style, file), style, file.stem().string(), file });
        }

        progress.tracks = static_cast<int> (progress.queued.size());
    }
    catch (const std::exception&)
    {
        // A damaged queue is started again by hearing the folders.
    }
}

void Styles::migrate (const std::filesystem::path& oldProfilesFolder)
{
    std::error_code ec;

    for (auto& entry : std::filesystem::directory_iterator (oldProfilesFolder, ec))
    {
        if (entry.path().extension() != ".json")
            continue;

        try
        {
            StyleProfile profile;
            {
                std::ifstream in (entry.path());
                auto j = Json::parse (in);

                if (j.value ("version", 0) != fileVersion)
                    continue;

                profile = StyleProfile::fromJson (j);
            }

            const auto heardAt = juce::File (entry.path().string()).getLastModificationTime().toMilliseconds();

            for (auto& track : profile.tracks)
            {
                SongMap map;
                map.style = profile.name;
                map.title = std::filesystem::path (track.path).stem().string();
                map.heardAt = heardAt;
                map.track = track;

                if (maps->get (map.id()) == nullptr)
                    maps->put (std::move (map));
            }

            if (! profile.folder.empty())
                maps->setStyleFolder (profile.name, profile.folder);

            // Kept, out of the way, in case something went wrong.
            const auto kept = oldProfilesFolder / "Before maps";
            std::filesystem::create_directories (kept);
            std::filesystem::rename (entry.path(), kept / entry.path().filename(), ec);
        }
        catch (const std::exception&)
        {
            // A damaged profile is left where it is.
        }
    }
}

StyleProfile::Track hearTrack (Separator& separator, Clap& clap, const std::filesystem::path& path, std::optional<double> knownTempo,
                               const std::function<bool (double)>& progress, bool wholeTrack)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (path.string())));

    if (reader == nullptr)
        throw std::runtime_error ("Can't read " + path.string());

    const auto rate = reader->sampleRate;
    const auto length = reader->lengthInSamples;
    const auto windowLength = static_cast<int> (std::ceil (windowSamples / Separator::sampleRate * rate));

    if (length < windowLength)
        throw std::runtime_error ("Too short: " + path.string());

    StyleProfile::Track track;
    track.path = path.string();

    // Audio from the file as the separator wants it: 44.1 kHz stereo.
    auto read = [&] (juce::int64 start, int count)
    {
        juce::AudioBuffer<float> raw (2, count);
        reader->read (&raw, 0, count, start, true, true);
        const auto out = static_cast<int> (std::ceil (count / rate * Separator::sampleRate));
        juce::AudioBuffer<float> audio (2, out);
        audio.clear();

        for (int c = 0; c < 2; ++c)
        {
            juce::AudioBuffer<float> channel (1, count);
            channel.copyFrom (0, 0, raw, std::min (c, static_cast<int> (reader->numChannels) - 1), 0, count);
            auto resampled = resampleMono (channel, rate, Separator::sampleRate);
            audio.copyFrom (c, 0, resampled.data(), std::min (out, static_cast<int> (resampled.size())));
        }

        return audio;
    };

    // The whole track split at once (most of the time), for the arrangement and the stretches.
    juce::AudioBuffer<float> whole;
    Separator::Stems wholeStems;

    if (wholeTrack)
    {
        whole = read (0, static_cast<int> (length));
        wholeStems = separator.separate (whole, [&] (double f) { return ! progress || progress (0.8 * f); });
    }

    std::vector<double> swings, densities;
    std::vector<std::vector<double>> kickOnsetsPerStretch, hatOnsetsPerStretch;
    std::vector<float> lufs;
    std::map<Role, std::vector<float>> levels;
    std::vector<std::vector<float>> balances;

    for (int w = 0; w < windowsPerTrack; ++w)
    {
        // Spread over the middle of the track, past the intro and before the outro.
        const auto position = 0.2 + 0.6 * w / (windowsPerTrack - 1);
        juce::AudioBuffer<float> chunk (2, windowSamples);
        Separator::Stems stems;

        if (wholeTrack)
        {
            const auto start = std::clamp (static_cast<int> (position * (whole.getNumSamples() - windowSamples)), 0, std::max (0, whole.getNumSamples() - windowSamples));
            const auto count = std::min (windowSamples, whole.getNumSamples() - start);
            auto slice = [&] (const juce::AudioBuffer<float>& from)
            {
                juce::AudioBuffer<float> b (2, windowSamples);
                b.clear();

                for (int c = 0; c < 2; ++c)
                    b.copyFrom (c, 0, from, c, start, count);

                return b;
            };

            chunk = slice (whole);
            stems = { slice (wholeStems.drums), slice (wholeStems.bass), slice (wholeStems.other), slice (wholeStems.vocals) };
        }
        else
        {
            const auto start = static_cast<juce::int64> (position * static_cast<double> (length - windowLength));
            auto audio = read (start, windowLength);
            chunk.clear();

            for (int c = 0; c < 2; ++c)
                chunk.copyFrom (c, 0, audio, c, 0, std::min (windowSamples, audio.getNumSamples()));

            stems = separator.separate (chunk);
        }

        auto toClap = [] (const juce::AudioBuffer<float>& b) { return resampleMono (b, Separator::sampleRate, Clap::sampleRate); };
        auto drums = toClap (stems.drums);

        const std::pair<Role, std::vector<float>> parts[] = {
            { Role::mix, toClap (chunk) },
            { Role::drums, drums },
            { Role::kick, filter (drums, Clap::sampleRate, 200, false) },
            { Role::hats, filter (drums, Clap::sampleRate, 5000, true) },
            { Role::bass, toClap (stems.bass) },
            { Role::music, toClap (stems.other) },
        };

        const auto mixDb = rmsDb (parts[0].second);

        for (auto& [role, audio] : parts)
        {
            const auto db = rmsDb (audio);

            if (role != Role::mix)
                levels[role].push_back (std::max (db, silentDb) - mixDb);

            if (db > silentDb)
                track.sounds[role].push_back (clap.embedAudio (audio.data(), audio.size()));
        }

        // Loudness, balance and groove.
        auto mixAudio = toAudio (chunk, Separator::sampleRate);
        lufs.push_back (analysis::integratedLufs (mixAudio));

        std::vector<float> balance;

        for (auto& band : analysis::spectralBalance (mixAudio))
            balance.push_back (band.db);

        balances.push_back (balance);

        // The tempo from the kick (in this music, on every beat) when there is one; the swing and
        // density from the hats, which carry the 16ths and are lost under the kick in the full drums.
        auto bandOnsets = [] (const std::vector<float>& audio)
        {
            juce::AudioBuffer<float> buffer (1, static_cast<int> (audio.size()));
            buffer.copyFrom (0, 0, audio.data(), buffer.getNumSamples());
            return library::detectTransients (toAudio (std::move (buffer), Clap::sampleRate));
        };

        const auto hatOnsets = bandOnsets (parts[3].second);
        kickOnsetsPerStretch.push_back (bandOnsets (parts[2].second));
        hatOnsetsPerStretch.push_back (hatOnsets);

        // Single hits: the loudest kick, hat and other hit in this stretch.
        struct Hit { float peak = 0; std::vector<float> audio; };
        std::map<Role, Hit> loudest;
        constexpr double hitSeconds = 0.25;

        // Hits in the full drums, and the hats that hide under the kick there.
        auto onsets = library::detectTransients (toAudio (stems.drums, Separator::sampleRate));
        onsets.insert (onsets.end(), hatOnsets.begin(), hatOnsets.end());

        for (auto t : onsets)
        {
            const auto from = static_cast<int> ((t - 0.005) * Separator::sampleRate);
            const auto count = static_cast<int> (hitSeconds * Separator::sampleRate);

            if (from < 0 || from + count > windowSamples)
                continue;

            auto hit = toAudio (juce::AudioBuffer<float> (2, count), Separator::sampleRate);

            for (int c = 0; c < 2; ++c)
                hit.buffer.copyFrom (c, 0, stems.drums, c, from, count);

            const auto peak = hit.buffer.getMagnitude (0, count);
            const auto features = library::analyseSample (hit, hitSeconds);
            const auto& b = features.bands;
            const auto role = b.sub + b.low > 0.55f ? Role::kickHit : b.high > 0.45f ? Role::hatHit : Role::percHit;

            if (peak > loudest[role].peak)
                loudest[role] = { peak, resampleMono (hit.buffer, Separator::sampleRate, Clap::sampleRate) };
        }

        for (auto& [role, hit] : loudest)
            if (rmsDb (hit.audio) > silentDb)
                track.sounds[role].push_back (clap.embedAudio (hit.audio.data(), hit.audio.size()));

        if (progress && ! progress ((wholeTrack ? 0.8 + 0.15 * (w + 1) / windowsPerTrack : (w + 1.0) / windowsPerTrack)))
            throw std::runtime_error ("Stopped");
    }

    // One tempo for the track, from all its stretches' kicks (or hats, without a kick); then the
    // swing and density of each stretch's hats against it.
    const auto seconds = windowSamples / Separator::sampleRate;
    track.tempo = knownTempo.value_or (0.0);

    if (track.tempo <= 0)
        track.tempo = estimateTempo (kickOnsetsPerStretch, seconds);

    if (track.tempo <= 0)
        track.tempo = estimateTempo (hatOnsetsPerStretch, seconds);

    auto measure = [&]
    {
        swings.clear();
        densities.clear();

        for (size_t w = 0; w < hatOnsetsPerStretch.size() && track.tempo > 0; ++w)
        {
            const auto& onsets = hatOnsetsPerStretch[w].size() >= 8 ? hatOnsetsPerStretch[w] : kickOnsetsPerStretch[w];
            const auto groove = measureGroove (onsets, seconds, track.tempo);

            if (onsets.size() >= 8)
                densities.push_back (groove.density);

            if (groove.swing)
                swings.push_back (*groove.swing);
        }
    };

    measure();

    // More than about five hats a beat at a slow tempo is 16ths at twice that tempo: nothing finer
    // is common, so the tempo was heard at half time.
    if (! knownTempo && track.tempo < 100 && median (densities) > 5.0)
    {
        track.tempo *= 2;
        measure();
    }

    track.density = median (densities);
    track.lufs = median (lufs);

    if (! swings.empty())
        track.swing = median (swings);

    for (auto& [role, values] : levels)
        track.levels[role] = median (values);

    if (! balances.empty())
    {
        track.balance.assign (balances.front().size(), 0.0f);

        for (auto& b : balances)
            for (size_t i = 0; i < std::min (b.size(), track.balance.size()); ++i)
                track.balance[i] += b[i] / static_cast<float> (balances.size());
    }

    // How the whole track unfolds, on the tempo heard above.
    if (wholeTrack)
    {
        auto mono = [] (const juce::AudioBuffer<float>& b)
        {
            std::vector<float> m (static_cast<size_t> (b.getNumSamples()));

            for (int i = 0; i < b.getNumSamples(); ++i)
                m[static_cast<size_t> (i)] = 0.5f * (b.getSample (0, i) + b.getSample (b.getNumChannels() > 1 ? 1 : 0, i));

            return m;
        };

        TrackStems full { Separator::sampleRate, mono (whole), mono (wholeStems.drums), mono (wholeStems.bass), {}, mono (wholeStems.vocals) };

        // The music without the vocals, as the arrangement hears them apart.
        full.other = mono (wholeStems.other);

        for (size_t i = 0; i < full.other.size() && i < full.vocals.size(); ++i)
            full.other[i] -= full.vocals[i];

        track.arrangement = hearArrangement (full, track.tempo > 0 ? std::optional (track.tempo) : std::nullopt);

        if (progress && ! progress (1.0))
            throw std::runtime_error ("Stopped");
    }

    return track;
}

} // namespace gk::listening
