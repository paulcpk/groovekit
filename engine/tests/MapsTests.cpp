// Song maps: reference tracks heard once, kept, curated and shared.

#include "TestHelpers.h"

#include "api/ApiHandler.h"
#include "library/SampleLibrary.h"
#include "listening/Listening.h"
#include "listening/KickSound.h"
#include "listening/Maps.h"
#include "listening/NoteTranscriber.h"
#include "session/Session.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>

using namespace gk;
using namespace gk::listening;
using model::Json;
namespace fs = std::filesystem;

namespace
{
    fs::path freshDir (const std::string& name)
    {
        auto dir = test::tempDir() / name;
        fs::remove_all (dir);
        fs::create_directories (dir);
        return dir;
    }

    void writeFile (const fs::path& file, size_t bytes)
    {
        fs::create_directories (file.parent_path());
        std::ofstream out (file, std::ios::binary);
        out << std::string (bytes, 'x');
    }

    /** A map as hearing this file would make it. */
    SongMap heardFrom (const std::string& style, const fs::path& file, int analysis = currentAnalysisVersion())
    {
        SongMap m;
        m.style = style;
        m.title = file.stem().string();
        m.heardAt = 1;
        m.track.path = file.string();
        m.track.size = static_cast<int64_t> (fs::file_size (file));
        m.track.modified = static_cast<int64_t> (fs::last_write_time (file).time_since_epoch().count());
        m.track.analysis = analysis;
        m.track.tempo = 132;
        return m;
    }

    SongMap someMap (const std::string& style, const std::string& title, double tempo = 130)
    {
        SongMap m;
        m.style = style;
        m.title = title;
        m.track.path = "/Users/someone/Music/" + title + ".mp3";
        m.track.analysis = currentAnalysisVersion();
        m.track.tempo = tempo;
        return m;
    }

    std::string statusOf (const std::vector<Styles::Song>& songs, const std::string& id)
    {
        for (auto& s : songs)
            if (s.id == id)
                return s.status;

        return "missing";
    }
}

TEST_CASE ("Song maps are kept with the user's choices, and shared as zips")
{
    const auto dir = freshDir ("maps-store");
    const auto one = someMap ("Berlin Techno", "Artist - One");
    const auto two = someMap ("Berlin Techno", "Artist - Two", 134);

    {
        MapStore store (dir / "Maps");
        store.put (one);
        store.put (two);
        CHECK (store.isInUse (one.id()));
        store.setInUse ({ one.id() }, false);
        store.setSkipped ({ "Berlin Techno/Not this one" }, true);
        store.setStyleFolder ("Berlin Techno", "/refs/Berlin Techno");
    }

    // On disk: a file per song in a folder per style, and the choices in the index.
    CHECK (fs::exists (dir / "Maps" / "Berlin Techno" / "Artist - One.json"));
    MapStore store (dir / "Maps");
    REQUIRE (store.all().size() == 2);
    CHECK_FALSE (store.isInUse (one.id()));
    CHECK (store.isInUse (two.id()));
    CHECK (store.isSkipped ("Berlin Techno/Not this one"));
    CHECK (store.getStyleFolder ("Berlin Techno") == fs::path ("/refs/Berlin Techno"));
    CHECK (store.get (two.id())->track.tempo == 134);

    std::ifstream indexFile (dir / "Maps" / "index.json");
    const auto index = Json::parse (indexFile);
    CHECK (index["songs"].size() == 2);

    // A zip for someone else: no paths from this Mac, and the receiver's own maps win.
    const auto zip = dir / "Berlin Techno maps.zip";
    CHECK (store.exportZip ({ one.id(), two.id(), "Berlin Techno/No map" }, zip) == 2);

    MapStore theirs (dir / "Their maps");
    theirs.put (someMap ("Berlin Techno", "Artist - Two"));
    const auto imported = theirs.importZip (zip);
    CHECK (imported.added == 1);
    CHECK (imported.kept == 1);
    REQUIRE (theirs.get (one.id()) != nullptr);
    CHECK (theirs.get (one.id())->source == "imported");
    CHECK (theirs.get (one.id())->track.path == "Artist - One.mp3");
    CHECK (theirs.get (two.id())->source == "heard");

    // Names from a zip can't reach outside the maps folder.
    const auto evil = dir / "evil.zip";
    {
        juce::ZipFile::Builder builder;
        const auto text = someMap ("../../Escaped", "../../../x").toJson().dump();
        builder.addEntry (new juce::MemoryInputStream (text.data(), text.size(), true), 9, "../../Escaped/x.json", juce::Time::getCurrentTime());
        juce::FileOutputStream out (juce::File (evil.string()));
        REQUIRE (builder.writeToStream (out, nullptr));
    }

    CHECK (theirs.importZip (evil).added == 1);
    CHECK_FALSE (fs::exists (dir / "Escaped"));
    CHECK_FALSE (fs::exists (dir.parent_path() / "Escaped"));
    CHECK (theirs.get ("../../Escaped/../../../x") != nullptr);
}

TEST_CASE ("Reference songs stand against their maps, and wait in a queue that outlives a restart")
{
    const auto dir = freshDir ("maps-songs");
    const auto refs = dir / "References";
    const auto folder = refs / "Berlin Techno";

    for (auto name : { "heard", "older", "changed", "new" })
        writeFile (folder / (std::string (name) + ".wav"), 1000);

    writeFile (refs / "Albums" / "album.wav", 1000);

    Models models (dir / "no-models");
    Clap clap (models);

    {
        Styles styles (models, clap, dir / "Maps");
        auto& maps = styles.getMaps();
        maps.put (heardFrom ("Berlin Techno", folder / "heard.wav"));
        maps.put (heardFrom ("Berlin Techno", folder / "older.wav", 1));

        auto changed = heardFrom ("Berlin Techno", folder / "changed.wav");
        changed.track.size = 5;
        maps.put (changed);

        maps.put (someMap ("Berlin Techno", "gone"));
        auto imported = someMap ("Berlin Techno", "theirs");
        imported.source = "imported";
        maps.put (imported);

        auto songs = styles.songs (refs);
        CHECK (songs.size() == 6);
        CHECK (statusOf (songs, "Berlin Techno/heard") == "heard");
        CHECK (statusOf (songs, "Berlin Techno/older") == "older");
        CHECK (statusOf (songs, "Berlin Techno/changed") == "changed");
        CHECK (statusOf (songs, "Berlin Techno/new") == "new");
        CHECK (statusOf (songs, "Berlin Techno/gone") == "gone");
        CHECK (statusOf (songs, "Berlin Techno/theirs") == "imported");

        // Hearing a folder queues what's new or changed; without the models it waits.
        CHECK (styles.add (folder, "") == "Berlin Techno");
        auto progress = styles.getProgress();
        CHECK_FALSE (progress.running);
        REQUIRE (progress.queued.size() == 2);
        CHECK (progress.tracks == 2);
        CHECK (statusOf (styles.songs (refs), "Berlin Techno/new") == "queued");

        // Forgotten: the map goes, and hearing the folder again leaves it out.
        styles.forget ({ "Berlin Techno/changed" });
        CHECK (maps.get ("Berlin Techno/changed") == nullptr);
        CHECK (statusOf (styles.songs (refs), "Berlin Techno/changed") == "skipped");
        CHECK (styles.getProgress().queued.size() == 1);
        styles.add (folder, "");
        CHECK (styles.getProgress().queued.size() == 1);

        // The style is the maps in use.
        CHECK (styles.names() == std::vector<std::string> { "Berlin Techno" });
        CHECK (styles.get ("berlin techno")->tracks.size() == 4);
        maps.setInUse ({ "Berlin Techno/heard", "Berlin Techno/older" }, false);
        CHECK (styles.get ("Berlin Techno")->tracks.size() == 2);
        maps.setInUse ({ "Berlin Techno/gone", "Berlin Techno/theirs" }, false);
        CHECK (styles.get ("Berlin Techno") == nullptr);
        CHECK (styles.names().empty());

        auto folders = styles.referenceFolders (refs);
        REQUIRE (folders.size() == 1);
        CHECK (folders[0].files == 4);
        CHECK (folders[0].unheard == 0);
        CHECK (folders[0].gone == 1);

        styles.pause();
    }

    // After a restart: still queued, still paused.
    Styles styles (models, clap, dir / "Maps");
    CHECK (styles.getProgress().queued.size() == 1);
    CHECK (styles.getProgress().paused);
    styles.clearQueue();
    CHECK (styles.getProgress().queued.empty());
}

TEST_CASE ("Styles heard before maps become maps")
{
    const auto dir = freshDir ("maps-migrate");
    StyleProfile old;
    old.name = "Industrial Techno";
    old.folder = "/refs/Industrial Techno";

    for (auto title : { "One", "Two" })
    {
        StyleProfile::Track t;
        t.path = std::string ("/refs/Industrial Techno/") + title + ".mp3";
        t.analysis = currentAnalysisVersion();
        t.tempo = 140;
        old.tracks.push_back (t);
    }

    fs::create_directories (dir / "styles");
    {
        std::ofstream out (dir / "styles" / "Industrial Techno.json");
        out << old.toJson().dump();
    }

    Models models (dir / "no-models");
    Clap clap (models);
    Styles styles (models, clap, dir / "Maps", dir / "styles");

    CHECK (styles.getMaps().all().size() == 2);
    CHECK (styles.getMaps().get ("Industrial Techno/Two") != nullptr);
    CHECK (styles.getMaps().getStyleFolder ("Industrial Techno") == fs::path ("/refs/Industrial Techno"));
    CHECK (styles.get ("Industrial Techno")->tracks.size() == 2);
    CHECK_FALSE (fs::exists (dir / "styles" / "Industrial Techno.json"));
    CHECK (fs::exists (dir / "styles" / "Before maps" / "Industrial Techno.json"));
}

TEST_CASE ("The References tab's methods tick, queue, forget, move out and share songs")
{
    const auto dir = freshDir ("maps-api");
    const auto refs = dir / "References";
    const auto folder = refs / "Hard Techno";
    writeFile (folder / "keep.wav", 1000);
    writeFile (folder / "drop.wav", 1000);

    library::SampleLibrary library (dir / "library.json");
    listening::Listening::Files files { dir / "no-models", dir / "sounds.bin", dir / "styles", dir / "taste.json", dir / "Maps" };
    listening::Listening listening (library, files);
    listening.getStyles().getMaps().put (heardFrom ("Hard Techno", folder / "keep.wav"));

    Session session (Session::createEmptyProject());
    ApiHandler handler (session, test::getEngineContext(), &library, nullptr, nullptr, &listening);
    handler.referencesFolder = refs;

    Json response;
    ApiHandler::Client client;
    client.send = [&] (const std::string& m) { response = Json::parse (m); };

    auto call = [&] (const std::string& method, Json params = Json::object())
    {
        response = test::callApi (handler, client, method, params);
        INFO (response.dump());
        CHECK_FALSE (response.contains ("error"));
        return response["result"];
    };

    auto songs = call ("maps.list")["songs"];
    REQUIRE (songs.size() == 2);
    CHECK (songs[0]["id"] == "Hard Techno/drop");
    CHECK (songs[0]["status"] == "new");
    CHECK (songs[1]["status"] == "heard");
    CHECK (songs[1]["map"]["tempo"] == 132);

    auto map = call ("maps.get", { { "id", "Hard Techno/keep" } });
    CHECK (map["track"]["tempo"] == 132);
    CHECK (map["current"] == true);

    call ("maps.setInUse", { { "ids", { "Hard Techno/keep" } }, { "inUse", false } });
    CHECK (call ("maps.list")["songs"][1]["inUse"] == false);

    CHECK (call ("maps.hearNew")["queued"] == 1);
    CHECK (call ("maps.status")["queued"].size() == 1);
    CHECK (call ("maps.list")["songs"][0]["status"] == "queued");

    call ("maps.forget", { { "ids", { "Hard Techno/drop" } } });
    CHECK (call ("maps.list")["songs"][0]["status"] == "skipped");
    CHECK (call ("maps.hearNew")["queued"] == 0);
    call ("maps.unskip", { { "ids", { "Hard Techno/drop" } } });
    CHECK (call ("maps.list")["songs"][0]["status"] == "new");

    // Shared and taken back in.
    const auto zip = dir / "maps.zip";
    CHECK (call ("maps.export", { { "ids", { "Hard Techno/keep" } }, { "path", zip.string() } })["count"] == 1);
    CHECK (call ("maps.import", { { "path", zip.string() } })["kept"] == 1);

    // Out of the references: moved to Not used, never deleted.
    CHECK (call ("maps.removeReferences", { { "ids", { "Hard Techno/drop" } } })["moved"] == 1);
    CHECK_FALSE (fs::exists (folder / "drop.wav"));
    CHECK (fs::exists (refs / "Not used" / "Hard Techno" / "drop.wav"));
    CHECK (call ("maps.list")["songs"].size() == 1);
}

TEST_CASE ("A track's arrangement is heard bar by bar: the grid, the parts, the sections")
{
    // 64 bars at 128 BPM: kick and rumble; hats from bar 17; a break with music only at 33-40;
    // everything from 41.
    constexpr double rate = 22050, tempo = 128;
    const double beat = 60 / tempo;
    const int bars = 64;
    const auto total = static_cast<size_t> (bars * 4 * beat * rate);
    TrackStems stems;
    stems.sampleRate = rate;
    stems.drums.assign (total, 0.0f);
    stems.bass.assign (total, 0.0f);
    stems.other.assign (total, 0.0f);
    stems.vocals.assign (total, 0.0f);
    juce::Random random (7);
    const double pi = juce::MathConstants<double>::pi;

    for (int b = 0; b < bars; ++b)
    {
        const bool kick = b < 32 || b >= 40, hats = (b >= 16 && b < 32) || b >= 40, music = b >= 32;

        for (int k = 0; k < 4; ++k)
        {
            const auto start = static_cast<size_t> ((b * 4 + k) * beat * rate);

            for (size_t i = 0; i < static_cast<size_t> (beat * rate) && start + i < total; ++i)
            {
                const double t = static_cast<double> (i) / rate;
                float v = 0;

                if (kick)
                {
                    v += static_cast<float> (0.8 * std::sin (2 * pi * 50 * t) * std::exp (-t / 0.06));

                    // The rumble: a low tail that swells between the kicks.
                    if (t > 0.3 * beat)
                        v += static_cast<float> (0.25 * std::sin (2 * pi * 45 * t) * std::sin (pi * (t - 0.3 * beat) / (0.7 * beat)));
                }

                if (hats && t >= 0.5 * beat && t < 0.5 * beat + 0.02)
                    v += 0.3f * (random.nextFloat() * 2 - 1);

                stems.drums[start + i] += v;

                if (music)
                    stems.other[start + i] += static_cast<float> (0.12 * (std::sin (2 * pi * 220 * t) + std::sin (2 * pi * 330 * t)));
            }
        }
    }

    stems.mix.resize (total);

    for (size_t i = 0; i < total; ++i)
        stems.mix[i] = stems.drums[i] + stems.other[i];

    const auto a = hearArrangement (stems);
    INFO (a.describe());
    CHECK (std::abs (a.tempo - tempo) < 1.0);
    CHECK ((a.downbeat < 0.03 || a.downbeat > a.barSeconds() - 0.03));
    CHECK (a.bars == bars);

    auto on = [&] (const std::string& part, int bar) { return a.parts.at (part).on[static_cast<size_t> (bar - 1)]; };
    CHECK (on ("kick", 1));
    CHECK (on ("rumble", 8));
    CHECK_FALSE (on ("hats", 8));
    CHECK (on ("hats", 20));
    CHECK_FALSE (on ("kick", 36));
    CHECK (on ("music", 36));
    CHECK_FALSE (on ("music", 20));
    CHECK (on ("kick", 50));

    REQUIRE (a.sections.size() == 4);
    CHECK (a.sections[0].role == "intro");
    CHECK (a.sections[0].end == 16);
    CHECK (a.sections[1].role == "groove");
    CHECK (a.sections[2].role == "break");
    CHECK (a.sections[2].start == 32);
    CHECK (a.sections[2].end == 40);
    CHECK (a.sections[3].role == "drop");

    // Kept in a map and read back.
    const auto copy = Arrangement::fromJson (Json::parse (a.toJson().dump()));
    CHECK (copy.sections.size() == 4);
    CHECK (copy.parts.at ("hats").on == a.parts.at ("hats").on);
    CHECK_THAT (copy.describe(), Catch::Matchers::ContainsSubstring ("33-40    break"));
}

namespace
{
    /** Kicks at 128 BPM on every beat: a sine body at hz that falls to it, decaying over decay
        seconds, and optionally a rumble between them. */
    juce::AudioBuffer<float> kicks (double rate, double hz, double decay, float rumble, int beats, double click = 0)
    {
        const double beat = 60 / 128.0, pi = juce::MathConstants<double>::pi;
        juce::AudioBuffer<float> b (2, static_cast<int> (beats * beat * rate));
        b.clear();
        juce::Random random (3);

        for (int k = 0; k < beats; ++k)
        {
            const auto start = static_cast<int> (k * beat * rate);
            double phase = 0;

            for (int i = 0; i < static_cast<int> (beat * rate) && start + i < b.getNumSamples(); ++i)
            {
                const double t = i / rate;
                phase += 2 * pi * (hz + 80 * std::exp (-t / 0.01)) / rate;
                float v = static_cast<float> (0.8 * std::sin (phase) * std::exp (-t / decay));

                if (t < 0.004)
                    v += static_cast<float> (click * (random.nextFloat() * 2 - 1));

                if (rumble > 0 && t > 0.3 * beat)
                    v += static_cast<float> (rumble * std::sin (2 * pi * 45 * t) * std::sin (pi * (t - 0.3 * beat) / (0.7 * beat)));

                b.setSample (0, start + i, v);
                b.setSample (1, start + i, v);
            }
        }

        return b;
    }
}

TEST_CASE ("Kicks are measured the same way everywhere, and compared in words")
{
    constexpr double rate = 44100;
    const double beat = 60 / 128.0;

    // A one-shot: one hit, no beat.
    auto one = measureKick (kicks (rate, 50, 0.08, 0, 1), rate);
    REQUIRE (one.has_value());
    INFO (one->describe());
    CHECK (std::abs (one->bodyHz - 50) < 4);
    CHECK (one->decayMs > 60);
    CHECK (one->decayMs < 250);
    CHECK_FALSE (one->rumbleDb.has_value());
    CHECK (one->width < 0.05f);

    // In a groove on a grid: the rumble between kicks and where they play.
    KickTiming timing;
    timing.beat = beat;
    timing.downbeat = 0.0;
    auto groove = measureKick (kicks (rate, 50, 0.08, 0.3f, 16), rate, timing);
    REQUIRE (groove.has_value());
    INFO (groove->describe());
    CHECK (groove->hits >= 14);
    CHECK (groove->pattern == "x...x...x...x...");
    REQUIRE (groove->rumbleDb.has_value());
    CHECK (*groove->rumbleDb > -20);

    // Without one, nothing between the kicks is no rumble.
    auto dry = measureKick (kicks (rate, 50, 0.08, 0, 16), rate, timing);
    CHECK_FALSE (dry->rumbleDb.has_value());

    // Higher, shorter and clickier: said so, and further from it than itself.
    auto other = measureKick (kicks (rate, 60, 0.03, 0.3f, 16, 0.6), rate, timing);
    const auto same = compareKicks (*groove, *groove);
    const auto apart = compareKicks (*other, *groove);
    INFO (apart.toJson().dump());
    CHECK (same.score > 99);
    CHECK (apart.score < 80);
    CHECK (apart.differences["semitones"].get<double>() > 2);
    CHECK (apart.differences["decayMs"].get<double>() < 0);
    CHECK (apart.differences["clickDb"].get<double>() > 3);

    // A one-shot against a groove with rumble: the missing rumble is a note, not a cost.
    const auto shot = compareKicks (*one, *groove);
    CHECK_THAT (shot.notes.back(), Catch::Matchers::ContainsSubstring ("no rumble"));

    CHECK (KickSound::fromJson (Json::parse (groove->toJson().dump())).pattern == groove->pattern);
    CHECK (noteName (55) == "A1");
}

TEST_CASE ("Kicks like a reference's are found, auditioned, put to the user and measured in the song")
{
    const auto dir = freshDir ("maps-match");
    constexpr double rate = 44100;

    auto writeKick = [&] (const fs::path& file, const juce::AudioBuffer<float>& audio)
    {
        fs::create_directories (file.parent_path());
        juce::File out (file.string());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.createOutputStream().release(), rate, 2, 24, {}, 0));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    };

    // The library: a kick like the reference's, and one higher and shorter.
    writeKick (dir / "samples" / "Deep Kick 50.wav", kicks (rate, 50, 0.08, 0, 1));
    writeKick (dir / "samples" / "Short Kick 62.wav", kicks (rate, 62, 0.025, 0, 1));
    library::SampleLibrary library (dir / "library.json");
    library.addFolder (dir / "samples");
    library.startScan();
    library.waitForScan();

    listening::Listening::Files files { dir / "no-models", dir / "sounds.bin", dir / "styles", dir / "taste.json", dir / "Maps", dir / "kicks.json" };
    listening::Listening listening (library, files);
    listening.startIndexing();
    listening.getKicks().wait();
    REQUIRE (listening.getKicks().size() == 2);

    // A reference whose kick is the deep one's, with a rumble.
    KickTiming timing;
    timing.beat = 60 / 128.0;
    timing.downbeat = 0.0;
    SongMap reference = someMap ("Hard Techno", "Reference");
    reference.track.tempo = 128;
    reference.track.kick = StyleProfile::Track::Kick { *measureKick (kicks (rate, 50, 0.08, 0.3f, 16), rate, timing), 0, 8, true };
    listening.getStyles().getMaps().put (reference);

    Session session (Session::createEmptyProject());
    ApiHandler handler (session, test::getEngineContext(), &library, nullptr, nullptr, &listening);
    std::vector<Json> notifications;
    handler.setBroadcast ([&] (const std::string& m) { notifications.push_back (Json::parse (m)); });

    Json response;
    ApiHandler::Client client;
    client.send = [&] (const std::string& m) { response = Json::parse (m); };

    auto call = [&] (const std::string& method, Json params)
    {
        response = test::callApi (handler, client, method, params);
        INFO (response.dump());
        CHECK_FALSE (response.contains ("error"));
        return response["result"];
    };

    auto found = call ("match.kicks", { { "id", reference.id() } });
    INFO (found.dump());
    REQUIRE (found["results"].size() == 2);
    CHECK (found["results"][0]["name"] == "Deep Kick 50.wav");
    CHECK (found["results"][0]["score"].get<double>() > found["results"][1]["score"].get<double>());
    CHECK (found["reference"]["clean"] == true);

    // Heard in a groove: written to a file to listen to, and measured like the reference.
    const auto deep = found["results"][0]["path"].get<std::string>();
    const auto shortKick = found["results"][1]["path"].get<std::string>();
    auto auditioned = call ("match.audition", { { "id", reference.id() }, { "paths", { deep, shortKick } } });
    REQUIRE (auditioned["results"].size() == 2);
    CHECK (fs::exists (auditioned["results"][0]["audition"].get<std::string>()));
    CHECK (auditioned["results"][0]["score"].get<double>() > auditioned["results"][1]["score"].get<double>());
    CHECK (auditioned["results"][1]["changes"].dump().find ("transpose") != std::string::npos);

    // Put to the user: they pick the first, which then fits the style.
    const auto offered = call ("picks.offer", { { "title", "Which kick?" }, { "style", "Hard Techno" },
                                                { "candidates", { { { "path", deep } }, { { "path", shortKick } } } } });
    REQUIRE_FALSE (notifications.empty());
    CHECK (notifications.back()["method"] == "picks");
    CHECK (call ("picks.get", { { "id", offered["id"] } })["status"] == "waiting");
    call ("picks.choose", { { "id", offered["id"] }, { "choice", 0 } });
    CHECK (call ("picks.get", { { "id", offered["id"] } })["choice"] == 0);
    const auto ratings = listening.getTaste().forStyle ("Hard Techno");
    REQUIRE (ratings.size() == 1);
    CHECK (ratings[0].fits);

    // "Other": the user says what they want instead; no verdict on these.
    const auto again = call ("picks.offer", { { "title", "Which hats?" }, { "style", "Hard Techno" },
                                              { "candidates", { { { "path", deep } }, { { "path", shortKick } } } } });
    response = test::callApi (handler, client, "picks.choose", { { "id", again["id"] }, { "choice", 0 }, { "note", "hats only" } });
    CHECK (response.contains ("error"));        // a note goes with "Other" only
    call ("picks.choose", { { "id", again["id"] }, { "choice", -1 }, { "note", "  Only hat loops, not whole drum loops " } });
    const auto other = call ("picks.get", { { "id", again["id"] } });
    CHECK (other["choice"] == -1);
    CHECK (other["note"] == "Only hat loops, not whole drum loops");
    CHECK (listening.getTaste().forStyle ("Hard Techno").size() == 1);

    // The window closed without a choice: not this part for now, and no verdict on these either.
    const auto closed = call ("picks.offer", { { "title", "Which clap?" }, { "style", "Hard Techno" },
                                               { "candidates", { { { "path", deep } }, { { "path", shortKick } } } } });
    response = test::callApi (handler, client, "picks.choose", { { "id", closed["id"] }, { "choice", -1 }, { "closed", true }, { "more", true } });
    CHECK (response.contains ("error"));        // closed is an answer of its own
    call ("picks.choose", { { "id", closed["id"] }, { "choice", -1 }, { "closed", true } });
    CHECK (call ("picks.get", { { "id", closed["id"] } })["closed"] == true);
    CHECK (notifications.back()["params"]["closed"] == true);
    CHECK (listening.getTaste().forStyle ("Hard Techno").size() == 1);

    // Closed after a pick made elsewhere: the pick stands.
    call ("picks.choose", { { "id", offered["id"] }, { "choice", -1 }, { "closed", true } });
    CHECK (call ("picks.get", { { "id", offered["id"] } })["choice"] == 0);

    // The song's own kick, the deep one on every beat, measured against the reference.
    model::Reader r;
    std::vector<model::Command> commands;

    for (auto json : {
             Json::parse (R"({ "type": "addChannel", "channel": { "id": "kick", "instrument": { "type": "sampler", "layers": [
                               { "id": "l", "pitchTracking": false, "sample": ")" + deep + R"(" } ] } } })"),
             Json::parse (R"({ "type": "addPattern", "pattern": { "id": "p", "notes": [
                               { "channel": "kick", "start": 0, "length": 240, "velocity": 127 }, { "channel": "kick", "start": 960, "length": 240, "velocity": 127 },
                               { "channel": "kick", "start": 1920, "length": 240, "velocity": 127 }, { "channel": "kick", "start": 2880, "length": 240, "velocity": 127 } ] } })"),
             Json::parse (R"({ "type": "addClip", "clip": { "type": "pattern", "id": "c", "row": 0, "start": 0, "length": 30720, "pattern": "p" } })"),
             Json::parse (R"({ "type": "setTempo", "tempo": 128 })") })
    {
        readValue (r, json, commands.emplace_back());
    }

    session.execute (commands);
    auto measured = call ("match.channel", { { "id", reference.id() }, { "channel", "kick" } });
    INFO (measured.dump());
    CHECK (measured["score"].get<double>() > 60);
    CHECK (measured["changes"].dump().find ("rumble") != std::string::npos);
}

TEST_CASE ("While songs are being heard, the queue follows the ticks")
{
    const auto dir = freshDir ("maps-ticks");
    const auto refs = dir / "References";

    for (auto name : { "a", "b", "c" })
        writeFile (refs / "Hard Techno" / (std::string (name) + ".wav"), 1000);

    library::SampleLibrary library (dir / "library.json");
    listening::Listening::Files files { dir / "no-models", dir / "sounds.bin", dir / "styles", dir / "taste.json", dir / "Maps", dir / "kicks.json" };
    listening::Listening listening (library, files);
    Session session (Session::createEmptyProject());
    ApiHandler handler (session, test::getEngineContext(), &library, nullptr, nullptr, &listening);
    handler.referencesFolder = refs;

    Json response;
    ApiHandler::Client client;
    client.send = [&] (const std::string& m) { response = Json::parse (m); };

    auto call = [&] (const std::string& method, Json params = Json::object())
    {
        response = test::callApi (handler, client, method, params);
        INFO (response.dump());
        CHECK_FALSE (response.contains ("error"));
        return response["result"];
    };

    auto queued = [&]
    {
        std::vector<std::string> ids;
        const auto status = call ("maps.status");

        for (auto& item : status["queued"])
            ids.push_back (item["id"]);

        return ids;
    };

    call ("maps.hear", { { "ids", { "Hard Techno/a", "Hard Techno/b" } } });
    CHECK (queued() == std::vector<std::string> { "Hard Techno/a", "Hard Techno/b" });

    // Unticked: out of the queue. Ticked: into it.
    call ("maps.setInUse", { { "ids", { "Hard Techno/b" } }, { "inUse", false } });
    CHECK (queued() == std::vector<std::string> { "Hard Techno/a" });
    call ("maps.setInUse", { { "ids", { "Hard Techno/c" } }, { "inUse", true } });
    CHECK (queued() == std::vector<std::string> { "Hard Techno/a", "Hard Techno/c" });

    // With nothing being heard, a tick only means in use.
    call ("maps.clearQueue");
    call ("maps.setInUse", { { "ids", { "Hard Techno/b" } }, { "inUse", true } });
    CHECK (queued().empty());
}

TEST_CASE ("A kick is heard under a rumble nearly as loud as it")
{
    // Hard techno: the rumble fills the space between the kicks almost at their level.
    constexpr double rate = 22050;
    const auto groove = kicks (rate, 52, 0.06, 0.7f, 64);
    TrackStems stems;
    stems.sampleRate = rate;
    stems.drums.resize (static_cast<size_t> (groove.getNumSamples()));

    for (size_t i = 0; i < stems.drums.size(); ++i)
        stems.drums[i] = groove.getSample (0, static_cast<int> (i));

    stems.mix = stems.drums;
    stems.bass.assign (stems.drums.size(), 0.0f);
    stems.other = stems.bass;
    stems.vocals = stems.bass;

    const auto a = hearArrangement (stems, 128.0);
    INFO (a.describe());
    const auto& kick = a.parts.at ("kick").on;
    INFO (a.toJson()["parts"]["kick"].dump());
    CHECK (std::count (kick.begin(), kick.end(), true) >= a.bars - 1);
    CHECK (a.sections.front().role != "break");
}

namespace
{
    /** The downloaded models, when this Mac has them (CI doesn't). */
    const Models* modelsForFigures()
    {
        static const Models models (Models::getDefaultFolder());
        return models.getStatus().ready ? &models : nullptr;
    }
}

TEST_CASE ("The figures the music and the bass play are heard from a track's stems")
{
    auto* models = modelsForFigures();

    if (models == nullptr)
        SKIP ("The listening models aren't downloaded");

    // 48 bars at 128 BPM: kicks throughout; from bar 17 a stab on 16ths 1, 4, 7 (G3) and 11 (D4)
    // of every bar; from bar 25 a bass note (G1) on beats 1 and 3.
    constexpr double rate = Separator::sampleRate, tempo = 128;
    const double beat = 60 / tempo, pi = juce::MathConstants<double>::pi;
    const int bars = 48;
    const auto total = static_cast<int> (bars * 4 * beat * rate);
    Separator::Stems stems;

    for (auto* b : { &stems.drums, &stems.bass, &stems.other, &stems.vocals })
    {
        b->setSize (2, total);
        b->clear();
    }

    auto tone = [&] (juce::AudioBuffer<float>& into, double hz, double at, double seconds, float gain, double decay)
    {
        const auto start = static_cast<int> (at * rate);

        for (int i = 0; i < static_cast<int> (seconds * rate) && start + i < total; ++i)
        {
            const double t = i / rate;
            const auto v = static_cast<float> (gain * std::exp (-t / decay) * (std::sin (2 * pi * hz * t) + 0.3 * std::sin (4 * pi * hz * t)));
            into.addSample (0, start + i, v);
            into.addSample (1, start + i, v);
        }
    };

    for (int b = 0; b < bars; ++b)
    {
        for (int k = 0; k < 4; ++k)
            tone (stems.drums, 50, (b * 4 + k) * beat, 0.3, 0.8f, 0.06);

        if (b >= 16)
            for (int step : { 0, 3, 6, 10 })
                tone (stems.other, step == 10 ? 293.66 : 196.0, b * 4 * beat + step * beat / 4, beat / 4, 0.4f, 0.08);

        if (b >= 24)
            for (int k : { 0, 2 })
                tone (stems.bass, 49.0, (b * 4 + k) * beat, beat, 0.5f, 0.5);
    }

    TrackStems mono;
    mono.sampleRate = rate;
    auto take = [&] (const juce::AudioBuffer<float>& b) { return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + total); };
    mono.drums = take (stems.drums);
    mono.bass = take (stems.bass);
    mono.other = take (stems.other);
    mono.vocals = take (stems.vocals);
    mono.mix.resize (static_cast<size_t> (total));

    for (int i = 0; i < total; ++i)
        mono.mix[static_cast<size_t> (i)] = mono.drums[static_cast<size_t> (i)] + mono.bass[static_cast<size_t> (i)] + mono.other[static_cast<size_t> (i)];

    const auto a = hearArrangement (mono, tempo);
    REQUIRE (a.bars == bars);

    NoteTranscriber transcriber (*models);
    const auto figures = hearFigures (transcriber, a, stems);
    INFO (describeFigures (figures));

    auto of = [&] (const std::string& part)
    {
        std::vector<StyleProfile::Track::Figure> list;

        for (auto& f : figures)
            if (f.part == part)
                list.push_back (f);

        return list;
    };

    const auto music = of ("music"), bass = of ("bass");
    REQUIRE (! music.empty());
    REQUIRE (! bass.empty());
    CHECK (music.front().bar >= 16);
    CHECK (bass.front().bar >= 24);

    // The stab's rhythm and its two notes, in the first bar of the figure.
    auto at = [] (const StyleProfile::Track::Figure& f, int64_t tick)
    {
        std::vector<int> keys;

        for (auto& n : f.notes)
            if (n.start == tick)
                keys.push_back (n.key);

        return keys;
    };

    CHECK (at (music.front(), 0) == std::vector<int> { 55 });
    CHECK (at (music.front(), 3 * 240) == std::vector<int> { 55 });
    CHECK (at (music.front(), 6 * 240) == std::vector<int> { 55 });
    CHECK (at (music.front(), 10 * 240) == std::vector<int> { 62 });
    CHECK (at (music.front(), 1 * 240).empty());
    CHECK (at (bass.front(), 0) == std::vector<int> { 31 });
    CHECK (at (bass.front(), 2 * 960) == std::vector<int> { 31 });

    // Kept in a map and read back, and described for the agent.
    StyleProfile::Track track;
    track.figures = figures;
    const auto copy = StyleProfile::Track::fromJson (Json::parse (track.toJson().dump()));
    REQUIRE (copy.figures.size() == figures.size());
    CHECK (copy.figures.front().notes.size() == figures.front().notes.size());
    CHECK_THAT (describeFigures (copy.figures), Catch::Matchers::ContainsSubstring ("|G3 . . G3 . . G3 . . . D4"));
}
