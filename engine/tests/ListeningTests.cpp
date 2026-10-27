#include "TestHelpers.h"

#include "listening/Clap.h"
#include "listening/Separator.h"
#include "listening/Styles.h"
#include "listening/Taste.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gk::listening;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    /** A falling-pitch blip with a decay, 0.75 s at 48 kHz: the same signal the expected values
        below were computed from with transformers' ClapFeatureExtractor and the ONNX models. */
    std::vector<float> chirp()
    {
        std::vector<float> s (36000);

        for (size_t i = 0; i < s.size(); ++i)
        {
            const auto t = static_cast<double> (i) / 48000.0;
            s[i] = static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 400.0 * t) * t) * std::exp (-t * 4.0));
        }

        return s;
    }

    /** The downloaded models, when this Mac has them (CI doesn't). */
    const Models* downloadedModels()
    {
        static const Models models (Models::getDefaultFolder());
        return models.getStatus().ready ? &models : nullptr;
    }
}

TEST_CASE ("CLAP's features match its feature extractor")
{
    auto s = chirp();
    auto features = clapFeatures (s.data(), s.size());
    REQUIRE (features.size() == 1001 * 64);

    auto at = [&] (int frame, int band) { return features[static_cast<size_t> (frame * 64 + band)]; };
    CHECK (at (0, 0) == Approx (22.5902).margin (0.01));
    CHECK (at (10, 5) == Approx (-30.5243).margin (0.01));
    CHECK (at (200, 30) == Approx (-88.5996).margin (0.05));
    CHECK (at (500, 63) == Approx (-100.0).margin (0.01));
}

TEST_CASE ("A file comes out of a .tgz")
{
    const auto dir = gk::test::tempDir() / "tgz";
    fs::create_directories (dir / "pkg" / "lib");
    juce::File ((dir / "pkg" / "lib" / "a.txt").string()).replaceWithText ("first");
    juce::File ((dir / "pkg" / "lib" / "b.bin").string()).replaceWithText (juce::String::repeatedString ("0123456789", 300));

    juce::ChildProcess tar;
    REQUIRE (tar.start (juce::StringArray { "tar", "czf", (dir / "pkg.tgz").string(), "-C", dir.string(), "pkg" }));
    tar.waitForProcessToFinish (10000);

    extractFromTarGz (dir / "pkg.tgz", "pkg/lib/b.bin", dir / "b.bin");
    CHECK (juce::File ((dir / "b.bin").string()).loadFileAsString() == juce::String::repeatedString ("0123456789", 300));
    CHECK_THROWS (extractFromTarGz (dir / "pkg.tgz", "pkg/lib/missing", dir / "c"));
}

TEST_CASE ("The tokenizer reads text like RoBERTa's")
{
    auto* models = downloadedModels();

    if (models == nullptr)
        SKIP ("The listening models aren't downloaded");

    Tokenizer tokenizer (models->clapVocabulary(), models->clapMerges());
    CHECK (tokenizer.encode ("a dark, distorted kick drum") == std::vector<int64_t> { 0, 102, 2933, 6, 30587, 3151, 12638, 2 });
    CHECK (tokenizer.encode ("Hi-hat's   crisp\n909  groove!! 140bpm")
           == std::vector<int64_t> { 0, 30086, 12, 8849, 18, 1437, 1437, 17766, 50118, 39517, 1437, 27071, 12846, 9680, 428, 1685, 2 });
    CHECK (tokenizer.encode ("Ümlaut café — ok") == std::vector<int64_t> { 0, 3849, 48, 18517, 4255, 26059, 93, 15983, 2 });
}

TEST_CASE ("CLAP embeds sounds and words as the reference implementation does")
{
    auto* models = downloadedModels();

    if (models == nullptr)
        SKIP ("The listening models aren't downloaded");

    Clap clap (*models);
    auto s = chirp();
    auto sound = clap.embedAudio (s.data(), s.size());
    const float expectedSound[] = { -0.14638f, -0.01893f, -0.04782f, -0.05696f, 0.00331f, 0.01578f, 0.03083f, 0.03053f };

    for (size_t i = 0; i < 8; ++i)
        CHECK (sound[i] == Approx (expectedSound[i]).margin (0.005)); // the int8 model amplifies rounding

    auto words = clap.embedText ("a dark, distorted kick drum");
    const float expectedWords[] = { -0.03383f, 0.06169f, -0.01045f, -0.0388f, -0.02294f, 0.01916f, -0.04176f, 0.09407f };

    for (size_t i = 0; i < 8; ++i)
        CHECK (words[i] == Approx (expectedWords[i]).margin (0.005));

    CHECK (similarity (sound, sound) == Approx (1.0f).margin (1e-4));
}

TEST_CASE ("The separator puts a kick in the drums and a held low note in the bass")
{
    auto* models = downloadedModels();

    if (models == nullptr)
        SKIP ("The listening models aren't downloaded");

    // Four seconds at 120 BPM: a kick on each beat over a held 55 Hz saw.
    const int rate = 44100, length = 4 * rate;
    juce::AudioBuffer<float> mix (2, length);
    mix.clear();

    for (int i = 0; i < length; ++i)
    {
        const auto t = static_cast<double> (i) / rate;
        const auto beat = std::fmod (t, 0.5);
        const auto kick = std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 150.0 * std::exp (-beat * 30.0)) * beat) * std::exp (-beat * 12.0);
        const auto saw = 0.3 * (2.0 * std::fmod (t * 55.0, 1.0) - 1.0);

        for (int c = 0; c < 2; ++c)
            mix.setSample (c, i, static_cast<float> (0.6 * kick + saw));
    }

    Separator separator (*models);
    auto stems = separator.separate (mix);
    REQUIRE (stems.drums.getNumSamples() == length);

    const auto drums = stems.drums.getRMSLevel (0, 0, length), bass = stems.bass.getRMSLevel (0, 0, length);
    CHECK (drums > 0.05f);
    CHECK (bass > 0.05f);
}


TEST_CASE ("The groove of a drum pattern: tempo, swing and density")
{
    // 16ths at 140 BPM for 8 s; the off-beat ones late by half a 16th's half (swing 0.5).
    auto pattern = [] (double swing)
    {
        std::vector<double> onsets;
        const auto sixteenth = 60.0 / 140.0 / 4.0;

        for (int i = 0; i * sixteenth < 8.0; ++i)
            onsets.push_back (0.1 + i * sixteenth + (i % 2 == 1 ? swing * sixteenth / 2.0 : 0.0));

        return onsets;
    };

    auto straight = measureGroove (pattern (0), 8.2);
    CHECK (straight.tempo == Approx (140).margin (0.6));
    REQUIRE (straight.swing);
    CHECK (*straight.swing == Approx (0).margin (0.05));
    CHECK (straight.density == Approx (4).margin (0.3));

    auto swung = measureGroove (pattern (0.5), 8.2, 140.0);
    REQUIRE (swung.swing);
    CHECK (*swung.swing == Approx (0.5).margin (0.05));

    CHECK (measureGroove ({ 0.1, 0.5 }, 1.0).tempo == 0); // too few to tell

    // A kick on every beat and a hat on every off-beat at 144 BPM is 144, not half time.
    std::vector<double> fast;

    for (int i = 0; i * (60.0 / 144.0 / 2.0) < 8.0; ++i)
        fast.push_back (0.05 + i * (60.0 / 144.0 / 2.0));

    CHECK (measureGroove (fast, 8.1).tempo == Approx (144).margin (0.6));

    // Stray onsets between the 16ths leave straight time straight.
    auto messy = pattern (0);
    juce::Random random (7);

    for (int i = 0; i < 25; ++i)
        messy.push_back (0.1 + random.nextDouble() * 8.0);

    std::sort (messy.begin(), messy.end());
    auto stillStraight = measureGroove (messy, 8.2, 140.0);
    REQUIRE (stillStraight.swing);
    CHECK (*stillStraight.swing < 0.1);
}

TEST_CASE ("Verdicts on samples are kept per style")
{
    const auto file = gk::test::tempDir() / "taste.json";
    fs::remove (file);

    {
        Taste taste (file);
        taste.rate ("/a.wav", "Techno", true);
        taste.rate ("/b.wav", "Techno", false);
        taste.rate ("/c.wav", "", false);
        taste.rate ("/a.wav", "Detroit", false);
        taste.rate ("/b.wav", "Techno", std::nullopt); // forgotten
    }

    Taste reloaded (file);
    auto techno = reloaded.forStyle ("techno");
    REQUIRE (techno.size() == 2);
    CHECK (std::any_of (techno.begin(), techno.end(), [] (auto& r) { return r.path == "/a.wav" && r.fits; }));
    CHECK (std::any_of (techno.begin(), techno.end(), [] (auto& r) { return r.path == "/c.wav" && ! r.fits; }));
    CHECK (reloaded.all().size() == 3);
}

TEST_CASE ("A style profile keeps its sounds and measures fit against the other tracks")
{
    auto unit = [] (size_t axis, float tilt = 0)
    {
        Embedding e {};
        e[axis] = 1.0f;
        e[axis + 1] = tilt;
        return average ({ e });
    };

    StyleProfile p;
    p.name = "Test";
    p.folder = "/refs";

    for (int t = 0; t < 3; ++t)
    {
        StyleProfile::Track track;
        track.path = "/refs/" + std::to_string (t) + ".mp3";
        track.tempo = 140 + t;
        track.swing = 0.1 * t;
        track.density = 3;
        track.lufs = -8;
        track.balance = { -10, -12 };
        track.levels[Role::bass] = -6;
        track.sounds[Role::hats] = { unit (10, 0.1f * static_cast<float> (t)) };
        p.tracks.push_back (track);
    }

    p.typicalFit[Role::hats] = 0.99f;

    auto copy = StyleProfile::fromJson (gk::model::Json::parse (p.toJson().dump()));
    REQUIRE (copy.tracks.size() == 3);
    CHECK (copy.tracks[2].sounds[Role::hats].front()[11] == Approx (p.tracks[2].sounds[Role::hats].front()[11]));
    CHECK (copy.typicalFit[Role::hats] == Approx (0.99f));
    CHECK (copy.has (Role::hats));
    CHECK_FALSE (copy.has (Role::bass));

    // A sound like the hats fits; one unlike them doesn't; leaving its own track out lowers fit.
    CHECK (copy.fit (Role::hats, unit (10)) > 0.95f);
    CHECK (copy.fit (Role::hats, unit (100)) < 0.05f);
    CHECK (copy.fit (Role::hats, copy.tracks[0].sounds[Role::hats].front(), 0) < copy.fit (Role::hats, copy.tracks[0].sounds[Role::hats].front()));

    auto m = copy.measurements();
    CHECK (m["tempo"]["median"].get<double>() == Approx (141));
    CHECK (m["tempo"]["min"].get<double>() == Approx (140));
    CHECK (m["levels"]["bass"].get<double>() == Approx (-6));

    CHECK (roleOf ("hat", true) == Role::hats);
    CHECK (roleOf ("hat", false) == Role::hatHit);
    CHECK (roleOf ("clap", true) == Role::drums);
    CHECK (roleOf ("synth", false) == Role::music);
    CHECK (roleFromName ("percHit") == Role::percHit);
}
