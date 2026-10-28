#include "Listening.h"
#include "Maps.h"

#include "analysis/Analysis.h"
#include "library/SampleFeatures.h"

#include <fstream>

namespace gk::listening
{

using model::Json;

namespace
{
    // Character and feel, not instruments: the category already says what a sample is.
    constexpr const char* vocabulary[] = {
        "dark", "bright", "warm", "cold", "dusty", "clean", "dirty", "distorted", "saturated", "crunchy", "gritty", "noisy",
        "metallic", "wooden", "glassy", "airy", "punchy", "tight", "boomy", "rumbling", "deep", "thin", "fat", "heavy", "soft",
        "hard", "harsh", "smooth", "crisp", "muffled", "lo-fi", "analog", "digital", "vintage", "industrial", "mechanical",
        "organic", "synthetic", "hollow", "resonant", "sharp", "snappy", "reverberant", "dry", "filtered", "detuned", "pulsing",
        "rolling", "driving", "hypnotic", "groovy", "swung", "sparse", "busy", "minimal", "chaotic", "tense", "melancholic",
        "eerie", "menacing", "aggressive", "dreamy", "ominous", "spacey", "tribal", "acid", "rave", "soulful", "funky", "dubby",
    };

    // The analysis's ten bands, for notes.
    constexpr const char* bandNames[] = { "20-60 Hz", "60-120 Hz", "120-250 Hz", "250-500 Hz", "500 Hz-1 kHz",
                                          "1-2 kHz", "2-4 kHz", "4-8 kHz", "8-12 kHz", "12-20 kHz" };

    constexpr float silentDb = -50.0f;

    float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }

    /** Library statistics are kept until the index has grown or shrunk by this much. */
    bool changedMuch (size_t then, size_t now)
    {
        return then == 0 || static_cast<double> (std::max (then, now) - std::min (then, now)) > 0.02 * static_cast<double> (then);
    }

    double round (double v, double step) { return std::round (v / step) * step; }

    float rmsDb (const std::vector<float>& x)
    {
        double sum = 0;

        for (auto v : x)
            sum += static_cast<double> (v) * v;

        return x.empty() || sum <= 0 ? analysis::silenceDb : static_cast<float> (10.0 * std::log10 (sum / static_cast<double> (x.size())));
    }

    /** The part a style has sounds for that's closest to the one asked for. */
    Role available (const StyleProfile& profile, Role role)
    {
        if (profile.has (role))
            return role;

        switch (role)
        {
            case Role::kickHit: return available (profile, Role::kick);
            case Role::hatHit:  return available (profile, Role::hats);
            case Role::percHit: return available (profile, Role::drums);
            case Role::kick:
            case Role::hats:    return available (profile, Role::drums);
            case Role::bass:    return available (profile, Role::music);
            default:            return Role::mix;
        }
    }

    std::string describePart (Role r)
    {
        switch (r)
        {
            case Role::mix:     return "the whole mix";
            case Role::drums:   return "the drums";
            case Role::kick:    return "the kick";
            case Role::hats:    return "the hats";
            case Role::bass:    return "the bass";
            case Role::kickHit: return "a single kick";
            case Role::hatHit:  return "a single hat";
            case Role::percHit: return "a single clap, snare or percussion hit";
            default:            return "the music (synths, pads, effects)";
        }
    }

    /** "is" or "are", to go with describePart. */
    std::string verbFor (Role r)
    {
        return r == Role::drums || r == Role::hats ? " are " : " is ";
    }

    std::string joinWords (const std::vector<std::string>& words)
    {
        std::string s;

        for (size_t i = 0; i < words.size(); ++i)
            s += (i == 0 ? "" : i + 1 == words.size() ? " and " : ", ") + words[i];

        return s;
    }
}

Listening::Listening (library::SampleLibrary& l, Files files)
    : library (l),
      models (files.models),
      clap (models),
      index (l, clap, files.soundIndex),
      styles (models, clap, files.maps.empty() ? files.styles.parent_path() / "maps" : files.maps, files.styles),
      taste (files.taste)
{
    styles.setObserver ([this] { notify(); });

    // Carries on with the songs queued before a restart.
    styles.start();
}

Listening::~Listening()
{
    // Every job reports through this object: stop them all before its members go.
    stopping = true;

    if (indexStarter.joinable())
        indexStarter.join();

    index.stop();
    styles.stop();
    models.stop();
}

void Listening::setObserver (std::function<void (const Json&)> f)
{
    std::lock_guard lock (observerMutex);
    observer = std::move (f);
}

void Listening::notify()
{
    std::function<void (const Json&)> f;
    {
        std::lock_guard lock (observerMutex);
        f = observer;
    }

    if (f)
        f (getStatus());
}

Json Listening::getStatus() const
{
    const auto m = models.getStatus();
    const auto i = index.getProgress();
    const auto s = styles.getProgress();

    Json modelsJson { { "ready", m.ready }, { "downloading", m.downloading }, { "progress", round (m.progress, 0.001) }, { "totalBytes", m.totalBytes } };

    if (! m.error.empty())
        modelsJson["error"] = m.error;

    Json stylesJson { { "running", s.running }, { "paused", s.paused }, { "names", styles.names() }, { "queued", s.queued.size() },
                      { "secondsPerSong", round (s.secondsPerSong, 0.1) }, { "mapsVersion", styles.getMaps().getVersion() } };

    if (! s.style.empty())
    {
        stylesJson["style"] = s.style;
        stylesJson["tracks"] = s.tracks;
        stylesJson["analysed"] = s.analysed;
        stylesJson["failed"] = s.failed;
    }

    if (s.current)
        stylesJson["current"] = { { "id", s.current->id }, { "style", s.current->style }, { "title", s.current->title },
                                  { "progress", round (s.currentProgress, 0.001) }, { "started", s.currentStarted } };

    if (! s.error.empty())
        stylesJson["error"] = s.error;

    return { { "models", modelsJson },
             { "index", { { "running", i.running }, { "total", i.total }, { "embedded", i.embedded }, { "failed", i.failed },
                          { "samples", index.size() }, { "librarySize", library.size() } } },
             { "styles", stylesJson } };
}

void Listening::download()
{
    models.download ([this] (const Models::Status& s)
    {
        notify();

        if (s.ready && ! s.downloading)
        {
            startIndexing();
            styles.start();
        }
    });

    notify();
}

void Listening::startIndexing()
{
    if (! models.getStatus().ready)
        return;

    std::lock_guard lock (starterMutex);

    // One request at a time; it waits for a running library scan (which may add samples) and a
    // running indexing pass, off the caller's thread.
    if (starting)
        return;

    if (indexStarter.joinable())
        indexStarter.join();

    starting = true;
    indexStarter = std::thread ([this]
    {
        while ((library.getProgress().running || index.getProgress().running) && ! stopping)
            std::this_thread::sleep_for (std::chrono::milliseconds (200));

        if (! stopping)
        {
            // The words for sounds take a while the first time: not during someone's search.
            try
            {
                std::lock_guard lock (statsMutex);
                getWords();
            }
            catch (const std::exception&)
            {
                // Searches report it when they need them.
            }

            index.start ([this] (const SoundIndex::Progress&) { notify(); });
        }

        starting = false;
    });
}

std::string Listening::addStyle (const std::filesystem::path& folder, const std::string& name)
{
    if (! models.getStatus().ready)
        throw std::runtime_error ("The listening models aren't downloaded yet (listening.download)");

    auto added = styles.add (folder, name);
    notify();
    return added;
}

//==============================================================================
std::vector<std::pair<std::string, Embedding>> Listening::indexSnapshot() const
{
    std::vector<std::pair<std::string, Embedding>> all;
    index.forEach ([&] (const std::string& path, const Embedding& e) { all.emplace_back (path, e); });
    return all;
}

Listening::Words& Listening::getWords()
{
    if (words.embeddings.empty())
    {
        for (auto* w : vocabulary)
            words.words.push_back (w);

        // The words' embeddings never change: worked out once, then read from a file.
        const auto file = models.getFolder() / "clap-words.bin";
        std::ifstream in (file, std::ios::binary);
        std::vector<Embedding> saved (words.words.size());
        uint32_t count = 0;

        if (in.read (reinterpret_cast<char*> (&count), 4) && count == saved.size()
            && in.read (reinterpret_cast<char*> (saved.data()), static_cast<std::streamsize> (saved.size() * sizeof (Embedding))))
        {
            words.embeddings = std::move (saved);
        }
        else
        {
            for (auto& w : words.words)
                words.embeddings.push_back (clap.embedText (w));

            std::ofstream out (file, std::ios::binary | std::ios::trunc);
            count = static_cast<uint32_t> (words.embeddings.size());
            out.write (reinterpret_cast<const char*> (&count), 4);
            out.write (reinterpret_cast<const char*> (words.embeddings.data()), static_cast<std::streamsize> (words.embeddings.size() * sizeof (Embedding)));
        }
    }

    // Each word's similarity over the library: a word is distinctive for a sound when the sound
    // is more like it than the library's samples usually are.
    if (const auto size = index.size(); size > 0 && changedMuch (words.indexSize, size))
    {
        const auto n = words.embeddings.size();
        std::vector<double> sum (n, 0.0), squares (n, 0.0);

        index.forEach ([&] (const std::string&, const Embedding& e)
        {
            for (size_t w = 0; w < n; ++w)
            {
                const double s = similarity (words.embeddings[w], e);
                sum[w] += s;
                squares[w] += s * s;
            }
        });

        words.mean.assign (n, 0.0f);
        words.deviation.assign (n, 1.0f);

        for (size_t w = 0; w < n; ++w)
        {
            const auto mean = sum[w] / static_cast<double> (size);
            words.mean[w] = static_cast<float> (mean);
            words.deviation[w] = static_cast<float> (std::sqrt (std::max (1e-8, squares[w] / static_cast<double> (size) - mean * mean)));
        }

        words.indexSize = size;
    }

    return words;
}

std::vector<std::string> Listening::describe (const Embedding& e, size_t count)
{
    std::lock_guard lock (statsMutex);
    auto& w = getWords();

    if (w.mean.empty())
        return {};

    std::vector<std::pair<float, size_t>> scores;

    for (size_t i = 0; i < w.embeddings.size(); ++i)
        scores.emplace_back ((similarity (w.embeddings[i], e) - w.mean[i]) / w.deviation[i], i);

    std::partial_sort (scores.begin(), scores.begin() + static_cast<long> (std::min (count, scores.size())), scores.end(), std::greater<>());

    std::vector<std::string> result;

    for (size_t i = 0; i < std::min (count, scores.size()); ++i)
        result.push_back (w.words[scores[i].second]);

    return result;
}

float Listening::baseline (const StyleProfile& profile, Role role)
{
    const auto key = std::make_pair (profile.name, role);
    const auto size = index.size();

    {
        std::lock_guard lock (statsMutex);

        if (auto it = baselines.find (key); it != baselines.end() && ! changedMuch (it->second.indexSize, size)
                                             && it->second.tracks == profile.tracks.size())
            return it->second.median;
    }

    // The median fit of the library's samples in this role (all of them, if it has few).
    std::map<std::string, Role> roles;

    for (auto& s : library.getSamples())
        roles[s.path] = roleOf (s.classification.category, s.classification.loop);

    std::vector<float> inRole, all;

    index.forEach ([&] (const std::string& path, const Embedding& e)
    {
        const auto f = profile.fit (role, e);
        all.push_back (f);

        if (auto it = roles.find (path); it != roles.end() && available (profile, it->second) == role)
            inRole.push_back (f);
    });

    auto& values = inRole.size() >= 20 ? inRole : all;
    float median = 0;

    if (! values.empty())
    {
        std::nth_element (values.begin(), values.begin() + static_cast<long> (values.size() / 2), values.end());
        median = values[values.size() / 2];
    }

    std::lock_guard lock (statsMutex);
    baselines[key] = { size, profile.tracks.size(), median };
    return median;
}

std::optional<TasteModel> Listening::learnedTaste (const std::string& style, const std::vector<std::pair<bool, Embedding>>& verdicts,
                                                   const std::string& signature)
{
    const auto size = index.size();

    {
        std::lock_guard lock (statsMutex);

        if (auto it = tastes.find (style); it != tastes.end() && it->second.verdicts == signature && ! changedMuch (it->second.indexSize, size))
            return it->second.model;
    }

    std::vector<Embedding> fits, misfits, background;

    for (auto& [fit, e] : verdicts)
        (fit ? fits : misfits).push_back (e);

    // The library's sounds at large: an even spread of up to 256.
    const auto every = std::max<size_t> (1, size / 256);
    size_t i = 0;

    index.forEach ([&] (const std::string&, const Embedding& e)
    {
        if (i++ % every == 0 && background.size() < 256)
            background.push_back (e);
    });

    auto model = TasteModel::train (fits, misfits, background);
    std::lock_guard lock (statsMutex);
    tastes[style] = { signature, size, model };
    return model;
}

double Listening::fitPercent (const StyleProfile& profile, Role role, const Embedding& e)
{
    const auto target = available (profile, role);
    const auto base = baseline (profile, target);
    const auto typical = profile.typicalFit.contains (target) ? profile.typicalFit.at (target) : 1.0f;
    return 100.0 * (profile.fit (target, e) - base) / std::max (1e-3f, typical - base);
}

Json Listening::search (const library::SampleQuery& filters, const SoundQuery& q)
{
    auto query = filters;
    const auto limit = query.limit;
    query.limit = std::numeric_limits<int>::max();
    auto candidates = library.search (query);

    const bool listening = models.getStatus().ready && index.size() > 0;
    const bool ranked = ! q.sound.empty() || ! q.like.empty() || ! q.style.empty();

    if (ranked && ! models.getStatus().ready)
        throw std::runtime_error ("Searching by sound needs the listening models (listening.download)");

    if (ranked && index.size() == 0)
        throw std::runtime_error ("The library isn't indexed by sound yet (see listening.status)");

    std::shared_ptr<const StyleProfile> profile;

    if (! q.style.empty() && (profile = styles.get (q.style)) == nullptr)
        throw std::runtime_error ("No style named \"" + q.style + "\" (styles.list)");

    // How the library ranks against the words or the sample: a candidate's match is the share
    // of the library it beats.
    std::vector<float> librarySimilarities;
    std::optional<Embedding> target;

    if (! q.sound.empty())
        target = clap.embedText (q.sound);
    else if (! q.like.empty())
        target = index.embedFile (q.like);

    if (target)
    {
        index.forEach ([&] (const std::string&, const Embedding& e) { librarySimilarities.push_back (similarity (*target, e)); });
        std::sort (librarySimilarities.begin(), librarySimilarities.end());
    }

    // The user's verdicts, with what the rated samples sound like.
    struct Verdict { std::string path; bool fits; Embedding embedding; };
    std::vector<Verdict> verdicts;

    if (ranked)
        for (auto& r : profile != nullptr ? taste.forStyle (profile->name) : taste.all())
            if (auto e = index.find (r.path))
                verdicts.push_back ({ r.path, r.fits, *e });

    // And what they have in common, learned from them.
    std::optional<TasteModel> learned;

    if (verdicts.size() >= 2)
    {
        std::string signature;
        std::vector<std::pair<bool, Embedding>> rated;

        for (auto& v : verdicts)
        {
            signature += (v.fits ? "+" : "-") + v.path + "\n";
            rated.push_back ({ v.fits, v.embedding });
        }

        learned = learnedTaste (profile != nullptr ? profile->name : "", rated, signature);
    }

    struct Result
    {
        const library::SampleEntry* entry;
        double key = 0;
        std::optional<Embedding> embedding;
        std::optional<double> fit, match, liking;
        std::optional<bool> rating;
    };

    std::vector<Result> results;

    for (auto& c : candidates)
    {
        Result r { &c };
        r.embedding = index.find (c.path);

        if (ranked)
        {
            if (! r.embedding)
                continue; // not heard yet

            if (profile != nullptr)
                r.fit = fitPercent (*profile, q.role.value_or (roleOf (c.classification.category, c.classification.loop)), *r.embedding);

            if (target)
            {
                const auto s = similarity (*target, *r.embedding);
                const auto below = std::lower_bound (librarySimilarities.begin(), librarySimilarities.end(), s) - librarySimilarities.begin();
                r.match = 100.0 * static_cast<double> (below) / static_cast<double> (std::max<size_t> (1, librarySimilarities.size()));
            }

            // Words or a sample lead when given; the style then breaks ties. (Fit isn't
            // comparable between parts, so it mustn't pull, say, drum loops over a described synth.)
            r.key = r.fit && r.match ? 0.7 * *r.match + 0.3 * std::clamp (*r.fit, -50.0, 120.0)
                                     : r.fit ? *r.fit : *r.match;

            // The learned taste: up to 30 either way, as far as it can be trusted.
            if (learned)
            {
                const auto p = learned->probability (*r.embedding);
                r.liking = 100.0 * p;
                r.key += 60.0 * (p - 0.5) * learned->confidence();
            }

            // Verdicts: the sample itself, and samples that sound almost the same.
            for (auto& v : verdicts)
            {
                if (v.path == c.path)
                {
                    r.rating = v.fits;
                    r.key += v.fits ? 30.0 : -100.0;
                }
                else
                {
                    const auto closeness = clamp01 ((similarity (v.embedding, *r.embedding) - 0.8f) / 0.2f);
                    r.key += (v.fits ? 25.0 : -25.0) * closeness;
                }
            }
        }

        results.push_back (std::move (r));
    }

    if (ranked)
        std::stable_sort (results.begin(), results.end(), [] (auto& a, auto& b) { return a.key > b.key; });

    auto list = Json::array();
    std::set<std::pair<std::string, int64_t>> seen; // copies of a file in several folders show once

    for (auto& r : results)
    {
        if (static_cast<int> (list.size()) >= limit)
            break;

        if (! seen.insert ({ std::filesystem::path (r.entry->path).filename().string(), r.entry->size }).second)
            continue;

        auto j = library::SampleLibrary::toJson (*r.entry, false, filters.fitsKey);

        if (r.fit)      j["fit"] = static_cast<int> (std::lround (*r.fit));
        if (r.match)    j["match"] = static_cast<int> (std::lround (*r.match));
        if (r.rating)   j["rating"] = *r.rating ? "fits" : "doesn't fit";
        if (r.liking)   j["taste"] = static_cast<int> (std::lround (*r.liking));

        if (listening && r.embedding)
            j["sounds"] = describe (*r.embedding, 3);

        list.push_back (std::move (j));
    }

    Json result { { "results", list }, { "librarySize", library.size() }, { "heard", index.size() } };

    if (learned)
        result["taste"] = { { "verdicts", verdicts.size() }, { "confidence", std::round (learned->confidence() * 100) / 100 } };

    return result;
}

//==============================================================================
Json Listening::describeStyle (const StyleProfile& profile)
{
    auto j = profile.measurements();
    j["name"] = profile.name;
    j["folder"] = profile.folder.string();

    auto tracks = Json::array();

    for (auto& t : profile.tracks)
        tracks.push_back (std::filesystem::path (t.path).stem().string());

    j["trackNames"] = tracks;

    Json parts = Json::object();

    for (auto role : { Role::mix, Role::drums, Role::kick, Role::hats, Role::bass, Role::music, Role::kickHit, Role::hatHit, Role::percHit })
    {
        std::vector<Embedding> sounds;

        for (auto& t : profile.tracks)
            if (auto it = t.sounds.find (role); it != t.sounds.end())
                sounds.insert (sounds.end(), it->second.begin(), it->second.end());

        if (! sounds.empty())
            parts[roleName (role)] = describe (average (sounds), 5);
    }

    j["sounds"] = parts;
    return j;
}

Listening::ReferenceNotes Listening::hearNotes (const std::string& styleName, const std::string& trackName, std::optional<double> from,
                                               double length, Role part)
{
    // The track: the first whose file name has the words asked for.
    const auto wanted = juce::String (trackName).toLowerCase().trim();
    std::shared_ptr<const StyleProfile> profile;
    const StyleProfile::Track* track = nullptr;
    std::vector<std::string> known;

    for (auto& name : styles.names())
    {
        if (! styleName.empty() && ! juce::String (name).equalsIgnoreCase (juce::String (styleName)))
            continue;

        auto p = styles.get (name);

        if (p == nullptr)
            continue;

        for (auto& t : p->tracks)
        {
            const auto stem = std::filesystem::path (t.path).stem().string();
            known.push_back (stem);

            if (track == nullptr && juce::String (stem).toLowerCase().contains (wanted))
            {
                profile = p;
                track = &t;
            }
        }
    }

    if (track == nullptr)
    {
        std::string list;

        for (size_t i = 0; i < known.size() && i < 40; ++i)
            list += (i ? ", " : "") + known[i];

        throw std::runtime_error (known.empty() ? "There are no reference tracks yet: add a style first"
                                                : "No reference track named like \"" + trackName + "\". The tracks: " + list);
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (track->path)));

    if (reader == nullptr)
        throw std::runtime_error ("Can't read " + track->path);

    const auto duration = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
    length = std::clamp (length, 2.0, std::min (60.0, duration));
    const auto start = std::clamp (from.value_or (duration * 0.4), 0.0, std::max (0.0, duration - length));
    const auto frames = static_cast<int> (length * reader->sampleRate);

    juce::AudioBuffer<float> raw (2, frames);
    reader->read (&raw, 0, frames, static_cast<juce::int64> (start * reader->sampleRate), true, true);

    // The separator wants 44.1 kHz stereo.
    const auto stretch = static_cast<int> (length * Separator::sampleRate);
    juce::AudioBuffer<float> chunk (2, stretch);
    chunk.clear();

    for (int c = 0; c < 2; ++c)
    {
        juce::AudioBuffer<float> channel (1, frames);
        channel.copyFrom (0, 0, raw, std::min (c, static_cast<int> (reader->numChannels) - 1), 0, frames);
        auto resampled = resampleMono (channel, reader->sampleRate, Separator::sampleRate);
        chunk.copyFrom (c, 0, resampled.data(), std::min (stretch, static_cast<int> (resampled.size())));
    }

    ReferenceNotes result;
    result.track = std::filesystem::path (track->path).stem().string();
    result.tempo = track->tempo;
    result.from = start;
    result.length = length;

    std::lock_guard lock (separatorMutex);

    if (separator == nullptr)
        separator = std::make_unique<Separator> (models);

    auto stems = separator->separate (chunk);
    separator.reset(); // a gigabyte, until it's next needed

    if (noteTranscriber == nullptr)
        noteTranscriber = std::make_unique<NoteTranscriber> (models);

    result.notes = noteTranscriber->transcribe (part == Role::bass ? stems.bass : stems.other, Separator::sampleRate);
    return result;
}

Json Listening::compare (const StyleProfile& profile, const std::filesystem::path& render, double songTempo, double songSwing)
{
    // The song is heard exactly as the references were: the same stretches, split the same way.
    StyleProfile::Track song;

    {
        std::lock_guard lock (separatorMutex);

        if (separator == nullptr)
            separator = std::make_unique<Separator> (models);

        song = hearTrack (*separator, clap, render, songTempo);
        separator.reset(); // a gigabyte, until the next comparison
    }

    struct Note { double weight; std::string text; };
    std::vector<Note> notes;
    const auto references = profile.measurements();

    Json parts = Json::object(), levels = Json::object();

    for (auto role : allRoles)
    {
        auto it = song.sounds.find (role);

        if (it == song.sounds.end() || it->second.empty() || ! profile.has (role))
            continue;

        double fit = 0;

        for (auto& e : it->second)
            fit += fitPercent (profile, role, e) / static_cast<double> (it->second.size());

        std::vector<Embedding> theirs;

        for (auto& t : profile.tracks)
            if (auto r = t.sounds.find (role); r != t.sounds.end())
                theirs.insert (theirs.end(), r->second.begin(), r->second.end());

        const auto ourWords = describe (average (it->second), 3), theirWords = describe (average (theirs), 3);
        parts[roleName (role)] = { { "fit", std::lround (fit) }, { "sounds", ourWords }, { "referencesSound", theirWords } };

        if (fit < 50)
            notes.push_back ({ (60.0 - fit) / 20.0, describePart (role) + verbFor (role) + "far from the references' (fit " + std::to_string (std::lround (fit))
                                                    + "): theirs sound " + joinWords (theirWords) + ", the song's " + joinWords (ourWords) });
    }

    for (auto& [role, level] : song.levels)
    {
        if (! references["levels"].contains (roleName (role)))
            continue;

        const auto theirs = references["levels"][roleName (role)].get<double>();
        levels[roleName (role)] = { { "song", round (level, 0.1) }, { "references", theirs } };

        if (std::abs (level - theirs) >= 4.0)
            notes.push_back ({ std::abs (level - theirs) / 4.0,
                               describePart (role) + verbFor (role) + juce::String (std::abs (level - theirs), 1).toStdString() + " dB "
                               + (level > theirs ? "louder" : "quieter") + " against the mix than in the references" });
    }

    // Groove: swing and density from the hats, as in the references.
    Json groove { { "hitsPerBeat", round (song.density, 0.1) } };

    if (song.swing)
        groove["swing"] = round (*song.swing, 0.01);

    if (references.contains ("swing") && song.swing && std::abs (*song.swing - references["swing"].get<double>()) >= 0.15)
        notes.push_back ({ 1.5, std::string ("the hats are ") + (*song.swing > references["swing"].get<double>() ? "more" : "less")
                                + " swung than the references' (" + juce::String (*song.swing, 2).toStdString() + " against "
                                + juce::String (references["swing"].get<double>(), 2).toStdString() + "; the song's swing setting is "
                                + juce::String (songSwing, 2).toStdString() + ")" });

    if (references.contains ("onsetsPerBeat") && references["onsetsPerBeat"].get<double>() > 0 && song.density > 0)
    {
        const auto theirs = references["onsetsPerBeat"].get<double>();
        const auto ratio = song.density / theirs;

        if (ratio < 0.65 || ratio > 1.5)
            notes.push_back ({ std::abs (std::log2 (ratio)) * 2.0, std::string ("the hats and high percussion are ") + (ratio < 1 ? "sparser" : "busier")
                                                                   + " than the references' (" + juce::String (song.density, 1).toStdString()
                                                                   + " hits a beat against " + juce::String (theirs, 1).toStdString() + ")" });
    }

    if (references.contains ("tempo"))
    {
        const auto low = references["tempo"]["min"].get<double>(), high = references["tempo"]["max"].get<double>();

        if (songTempo < low - 2 || songTempo > high + 2)
            notes.push_back ({ 2.0, "at " + juce::String (songTempo, 1).toStdString() + " BPM the song is " + (songTempo < low ? "slower" : "faster")
                                    + " than the references (" + juce::String (low, 1).toStdString() + "-" + juce::String (high, 1).toStdString() + " BPM)" });
    }

    // Balance, band by band, for the mix's level.
    auto balance = Json::array();

    for (size_t b = 0; b < song.balance.size() && b < references["balance"].size(); ++b)
    {
        const auto diff = song.balance[b] - references["balance"][b].get<double>();
        balance.push_back ({ { "band", bandNames[b] }, { "song", round (song.balance[b], 0.1) }, { "references", references["balance"][b] } });

        if (std::abs (diff) >= 4.0 && b < 9)
            notes.push_back ({ std::abs (diff) / 5.0, juce::String (std::abs (diff), 1).toStdString() + " dB " + (diff > 0 ? "more" : "less")
                                                      + " energy at " + bandNames[b] + " than the references, for the mix's level" });
    }

    std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.weight > b.weight; });
    auto noteList = Json::array();

    for (auto& n : notes)
        noteList.push_back (n.text);

    return { { "style", profile.name },
             { "parts", parts },
             { "tempo", { { "song", songTempo }, { "references", references.value ("tempo", Json::object()) } } },
             { "groove", groove },
             { "referencesGroove", { { "swing", references.value ("swing", Json()) }, { "hitsPerBeat", references.value ("onsetsPerBeat", Json()) } } },
             { "loudness", { { "songLufs", round (song.lufs, 0.1) }, { "referencesLufs", references.value ("lufs", Json()) } } },
             { "balance", balance },
             { "levels", levels },
             { "notes", noteList } };
}

} // namespace gk::listening
