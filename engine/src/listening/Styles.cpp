#include "Styles.h"

#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "library/SampleFeatures.h"

#include <fstream>

namespace gk::listening
{

using model::Json;

namespace
{
    constexpr int fileVersion = 1;
    constexpr int analysisVersion = 5;      // tracks heard by an older analysis are heard again
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

Json StyleProfile::toJson() const
{
    auto trackList = Json::array();

    for (auto& t : tracks)
    {
        Json levels = Json::object(), sounds = Json::object();

        for (auto& [role, db] : t.levels)
            levels[roleName (role)] = round (db, 0.1);

        for (auto& [role, list] : t.sounds)
        {
            auto encoded = Json::array();

            for (auto& e : list)
                encoded.push_back (encode (e));

            sounds[roleName (role)] = encoded;
        }

        Json j { { "path", t.path }, { "size", t.size }, { "modified", t.modified }, { "analysis", t.analysis }, { "tempo", t.tempo }, { "density", round (t.density, 0.01) },
                 { "lufs", round (t.lufs, 0.1) }, { "balance", t.balance }, { "levels", levels }, { "sounds", sounds } };

        if (t.swing)
            j["swing"] = round (*t.swing, 0.01);

        trackList.push_back (std::move (j));
    }

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

        const auto levels = tj.value ("levels", Json::object());
        const auto sounds = tj.value ("sounds", Json::object());

        for (auto& [name, db] : levels.items())
            if (auto role = roleFromName (name))
                t.levels[*role] = db.get<float>();

        for (auto& [name, list] : sounds.items())
            if (auto role = roleFromName (name))
                for (auto& s : list)
                    t.sounds[*role].push_back (decode (s.get<std::string>()));

        p.tracks.push_back (std::move (t));
    }

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

//==============================================================================
Styles::Styles (const Models& m, Clap& c, std::filesystem::path f) : models (m), clap (c), folder (std::move (f))
{
    std::error_code ec;

    for (auto& entry : std::filesystem::directory_iterator (folder, ec))
    {
        if (entry.path().extension() != ".json")
            continue;

        try
        {
            std::ifstream in (entry.path());
            auto j = Json::parse (in);

            if (j.value ("version", 0) == fileVersion)
            {
                auto p = std::make_shared<StyleProfile> (StyleProfile::fromJson (j));
                profiles[p->name] = std::move (p);
            }
        }
        catch (const std::exception&)
        {
            // A damaged profile is left out; adding the folder again rebuilds it.
        }
    }
}

Styles::~Styles()
{
    stop();
}

void Styles::stop()
{
    cancel = true;
    wait();
}

std::filesystem::path Styles::getDefaultFolder()
{
    return EngineContext::getAppDataFolder() / "styles";
}

std::filesystem::path Styles::fileFor (const std::string& name) const
{
    auto safe = juce::File::createLegalFileName (juce::String (name)).toStdString();
    return folder / (safe + ".json");
}

std::vector<std::string> Styles::names() const
{
    std::lock_guard lock (mutex);
    std::vector<std::string> list;

    for (auto& [name, p] : profiles)
        list.push_back (name);

    return list;
}

std::shared_ptr<const StyleProfile> Styles::get (const std::string& name) const
{
    std::lock_guard lock (mutex);

    for (auto& [n, p] : profiles)
        if (juce::String (n).equalsIgnoreCase (juce::String (name)))
            return p;

    return {};
}

Styles::Progress Styles::getProgress() const
{
    std::lock_guard lock (mutex);
    return progress;
}

void Styles::wait()
{
    if (thread.joinable())
        thread.join();
}

std::string Styles::add (const std::filesystem::path& tracksFolder, std::string name, ProgressCallback callback)
{
    std::error_code ec;
    const auto absolute = std::filesystem::weakly_canonical (std::filesystem::absolute (tracksFolder), ec);

    if (! std::filesystem::is_directory (absolute))
        throw std::runtime_error ("Not a folder: " + tracksFolder.string());

    if (name.empty())
        name = absolute.filename().string();

    StyleProfile profile;

    if (auto existing = get (name))
        profile = *existing;

    profile.name = name;
    profile.folder = absolute;

    std::lock_guard lock (mutex);

    if (progress.running)
        throw std::runtime_error ("Style \"" + progress.style + "\" is being analysed; try again when it's done");

    if (thread.joinable())
        thread.join();

    progress = { true, name, 0, 0, 0, {} };
    cancel = false;
    thread = std::thread ([this, profile = std::move (profile), callback]() mutable { analyse (std::move (profile), callback); });
    return name;
}

void Styles::remove (const std::string& name)
{
    auto profile = get (name);

    if (profile == nullptr)
        throw std::runtime_error ("No style named \"" + name + "\"");

    std::lock_guard lock (mutex);
    profiles.erase (profile->name);
    std::error_code ec;
    std::filesystem::remove (fileFor (profile->name), ec);
}

void Styles::analyse (StyleProfile profile, ProgressCallback callback)
{
    auto report = [&] (auto&& change)
    {
        Progress now;
        {
            std::lock_guard lock (mutex);
            change (progress);
            now = progress;
        }

        if (callback)
            callback (now);
    };

    std::vector<std::filesystem::path> files;
    std::error_code ec;

    for (auto it = std::filesystem::recursive_directory_iterator (profile.folder, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment (ec))
    {
        if (ec)
            break;

        if (it->is_regular_file (ec) && isAudioFile (it->path()) && ! it->path().filename().string().starts_with ("._"))
            files.push_back (it->path());
    }

    std::sort (files.begin(), files.end());
    report ([&] (Progress& p) { p.tracks = static_cast<int> (files.size()); });

    std::string error;

    try
    {
        if (files.empty())
            throw std::runtime_error ("There are no audio files in " + profile.folder.string());

        // Tracks heard before and unchanged are kept; the rest are heard now. The style is saved
        // after each, so it's usable while it grows and an interrupted analysis carries on later.
        std::vector<StyleProfile::Track> tracks;
        std::vector<std::filesystem::path> todo;

        for (auto& file : files)
        {
            const auto size = static_cast<int64_t> (std::filesystem::file_size (file, ec));
            const auto modified = modifiedTime (file);
            auto cached = std::find_if (profile.tracks.begin(), profile.tracks.end(), [&] (auto& t)
            {
                return t.path == file.string() && t.size == size && t.modified == modified && t.analysis == analysisVersion;
            });

            if (cached != profile.tracks.end())
            {
                tracks.push_back (*cached);
                report ([] (Progress& p) { ++p.analysed; });
            }
            else
            {
                todo.push_back (file);
            }
        }

        std::unique_ptr<Separator> separator;

        for (auto& file : todo)
        {
            if (cancel)
                throw std::runtime_error ("Stopped");

            try
            {
                if (separator == nullptr)
                    separator = std::make_unique<Separator> (models);

                auto track = hearTrack (*separator, clap, file);
                track.size = static_cast<int64_t> (std::filesystem::file_size (file, ec));
                track.modified = modifiedTime (file);
                track.analysis = analysisVersion;
                tracks.push_back (std::move (track));
                publish (profile, tracks);
                report ([] (Progress& p) { ++p.analysed; });
            }
            catch (const std::exception&)
            {
                report ([] (Progress& p) { ++p.failed; });
            }
        }

        if (tracks.empty())
            throw std::runtime_error ("None of the tracks could be read");

        // Also when nothing was new: tracks may have gone from the folder.
        publish (profile, tracks);
    }
    catch (const std::exception& e)
    {
        error = e.what();
    }

    report ([&] (Progress& p)
    {
        p.running = false;
        p.error = error;
    });
}

void Styles::publish (StyleProfile& profile, const std::vector<StyleProfile::Track>& tracks)
{
    profile.tracks = tracks;
    profile.typicalFit.clear();

    // How close each track's sounds are to the other tracks': the yardstick for fit.
    if (profile.tracks.size() > 1)
    {
        for (auto role : allRoles)
        {
            double sum = 0;
            int count = 0;

            for (size_t i = 0; i < profile.tracks.size(); ++i)
                if (auto it = profile.tracks[i].sounds.find (role); it != profile.tracks[i].sounds.end())
                    for (auto& s : it->second)
                        sum += profile.fit (role, s, i), ++count;

            if (count > 0)
                profile.typicalFit[role] = static_cast<float> (sum / count);
        }
    }

    std::filesystem::create_directories (folder);
    const auto file = fileFor (profile.name);
    const auto temp = file.string() + ".tmp";

    {
        std::ofstream out (temp, std::ios::trunc);
        out << profile.toJson().dump();
    }

    std::filesystem::rename (temp, file);

    std::lock_guard lock (mutex);
    profiles[profile.name] = std::make_shared<StyleProfile> (profile);
}

StyleProfile::Track hearTrack (Separator& separator, Clap& clap, const std::filesystem::path& path, std::optional<double> knownTempo)
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

    std::vector<double> swings, densities;
    std::vector<std::vector<double>> kickOnsetsPerStretch, hatOnsetsPerStretch;
    std::vector<float> lufs;
    std::map<Role, std::vector<float>> levels;
    std::vector<std::vector<float>> balances;

    for (int w = 0; w < windowsPerTrack; ++w)
    {
        // Spread over the middle of the track, past the intro and before the outro.
        const auto position = 0.2 + 0.6 * w / (windowsPerTrack - 1);
        const auto start = static_cast<juce::int64> (position * static_cast<double> (length - windowLength));

        juce::AudioBuffer<float> raw (2, windowLength);
        reader->read (&raw, 0, windowLength, start, true, true);

        // The separator wants 44.1 kHz stereo.
        juce::AudioBuffer<float> chunk (2, windowSamples);

        for (int c = 0; c < 2; ++c)
        {
            juce::AudioBuffer<float> channel (1, windowLength);
            channel.copyFrom (0, 0, raw, std::min (c, static_cast<int> (reader->numChannels) - 1), 0, windowLength);
            auto resampled = resampleMono (channel, rate, Separator::sampleRate);
            chunk.clear (c, 0, windowSamples);
            chunk.copyFrom (c, 0, resampled.data(), std::min (windowSamples, static_cast<int> (resampled.size())));
        }

        auto stems = separator.separate (chunk);

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

    return track;
}

} // namespace gk::listening
