#include "TestHelpers.h"

#include "library/SampleLibrary.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gk::library;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    SampleFeatures featuresOf (const std::string& fixture)
    {
        return analyseSample (gk::analysis::loadAudio (gk::test::fixture (fixture)));
    }

    /** Short clicks on every beat: a stand-in for a drum loop. */
    void writeClickLoop (const fs::path& file, double bpm, int beats)
    {
        const double rate = 48000;
        const auto beatSamples = 60.0 / bpm * rate;
        juce::AudioBuffer<float> buffer (1, static_cast<int> (std::round (beats * beatSamples)));
        buffer.clear();

        for (int b = 0; b < beats; ++b)
            for (int i = 0; i < 200; ++i)
                buffer.setSample (0, static_cast<int> (b * beatSamples) + i, (b % 2 == 0 ? 0.8f : 0.4f) * std::exp (-i / 40.0f));

        fs::create_directories (file.parent_path());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (juce::File (file.string())),
                                                                              rate, 1, 16, {}, 0));
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }
}

TEST_CASE ("Names are split into words")
{
    CHECK (tokenize ("BigKick_01 (Hard).wav") == std::vector<std::string> { "big", "kick", "01", "hard", "wav" });
    CHECK (tokenize ("808-Sub C#m") == std::vector<std::string> { "808", "sub", "c#m" });
}

TEST_CASE ("Keys are parsed from names")
{
    CHECK (parseKey ("Am") == "A minor");
    CHECK (parseKey ("C#min") == "C# minor");
    CHECK (parseKey ("Bb") == "Bb major");
    CHECK (parseKey ("Fmaj") == "F major");
    CHECK_FALSE (parseKey ("a"));
    CHECK_FALSE (parseKey ("kick"));
}

TEST_CASE ("Samples are classified by name and folder")
{
    SampleFeatures none;

    auto c = classify ("/Samples/Drums/Kicks/Big Kick 01.wav", none);
    CHECK (c.category == "kick");
    CHECK_FALSE (c.loop);

    c = classify ("/Samples/Loops/Drum Loop 124bpm Am.wav", none);
    CHECK (c.category == "drums");
    CHECK (c.loop);
    CHECK (c.tempo == 124);
    CHECK (c.key == "A minor");

    c = classify ("/Samples/Bass/Reese_C#m.wav", none);
    CHECK (c.category == "bass");
    CHECK (c.key == "C# minor");

    CHECK (classify ("/Samples/808 Kick.wav", none).category == "kick");
    CHECK (classify ("/Samples/Top Loops/Shaker_128.wav", none).tempo == 128);
    CHECK (classify ("/Samples/Hats/x.wav", none).category == "hat");
}

TEST_CASE ("Features describe the sound")
{
    auto kick = featuresOf ("kick.wav");
    CHECK (kick.durationSeconds == Approx (0.3));
    CHECK (kick.bands.sub + kick.bands.low > 0.8f);
    CHECK (kick.centroidHz < 300);
    CHECK (kick.attackMs < 20);

    auto hat = featuresOf ("hat.wav");
    CHECK (hat.centroidHz > 5000);
    CHECK (hat.bands.high > 0.7f);
    CHECK_FALSE (hat.pitch);

    auto bass = featuresOf ("bass_a2.wav");
    REQUIRE (bass.pitch);
    CHECK (bass.pitch->key == 45);
    CHECK (bass.pitch->hz == Approx (110.0).margin (0.5));
    CHECK (bass.pitch->confidence > 0.9f);
    REQUIRE (bass.integratedLufs);

    auto beep = featuresOf ("beep.wav");
    REQUIRE (beep.pitch);
    CHECK (beep.pitch->key == 83); // 1 kHz is B5 + 21 cents
    CHECK (beep.pitch->cents == Approx (21.4).margin (1.0));
}

TEST_CASE ("Unnamed samples are classified by sound")
{
    CHECK (classify ("/x/untitled1.wav", featuresOf ("kick.wav")).category == "kick");
    CHECK (classify ("/x/untitled2.wav", featuresOf ("hat.wav")).category == "hat");
    CHECK (classify ("/x/untitled3.wav", featuresOf ("bass_a2.wav")).category == "bass");
    CHECK (classify ("/x/untitled3.wav", featuresOf ("kick.wav")).categorySource == "sound");
}

TEST_CASE ("Loop tempo is estimated from the audio")
{
    auto file = gk::test::tempDir() / "loop-tempo" / "Untitled Loop.wav";
    writeClickLoop (file, 124, 8);

    auto entry = SampleLibrary::analyseFile (file);
    REQUIRE (entry.features.estimatedTempo);
    CHECK (*entry.features.estimatedTempo == Approx (124).margin (0.5));
    CHECK (entry.classification.loop);
    CHECK (entry.classification.tempo == entry.features.estimatedTempo);
    CHECK (entry.features.onsetCount == 8);
}

TEST_CASE ("The library scans folders, searches and persists")
{
    auto root = gk::test::tempDir() / "library";
    auto index = gk::test::tempDir() / "library-index.json";
    fs::remove_all (root);
    fs::remove (index);

    fs::create_directories (root / "Drums" / "Kicks");
    fs::create_directories (root / "Bass");
    fs::copy_file (gk::test::fixture ("kick.wav"), root / "Drums" / "Kicks" / "Punchy Kick.wav");
    fs::copy_file (gk::test::fixture ("hat.wav"), root / "Drums" / "Closed Hat.wav");
    fs::copy_file (gk::test::fixture ("bass_a2.wav"), root / "Bass" / "Sine Bass A.wav");
    writeClickLoop (root / "Loops" / "Click Loop 124bpm Am.wav", 124, 8);

    {
        SampleLibrary library (index);
        library.addFolder (root);
        REQUIRE (library.startScan());
        library.waitForScan();

        auto progress = library.getProgress();
        CHECK_FALSE (progress.running);
        CHECK (progress.filesFound == 4);
        CHECK (progress.analysed == 4);
        CHECK (library.size() == 4);

        auto kicks = library.search ({ "kick" });
        REQUIRE (kicks.size() == 1);
        CHECK (kicks[0].path.ends_with ("Punchy Kick.wav"));

        CHECK (library.search ({ "drums" }).size() == 2);   // by folder
        CHECK (library.search ({ "punch" }).size() == 1);   // by prefix
        CHECK (library.search ({ "kick snare" }).empty());  // every word must match

        SampleQuery loops;
        loops.loop = true;
        loops.tempo = 62; // half time matches
        loops.key = "Am";
        auto found = library.search (loops);
        REQUIRE (found.size() == 1);

        SampleQuery bass;
        bass.category = "bass";
        auto basses = library.search (bass);
        REQUIRE (basses.size() == 1);

        auto json = SampleLibrary::toJson (basses[0], false);
        CHECK (json["rootKey"] == 45);
        CHECK (json["description"].get<std::string>().find ("pitched A2") != std::string::npos);
    }

    // A new instance reads the index; a rescan only looks at what changed.
    fs::remove (root / "Drums" / "Closed Hat.wav");

    SampleLibrary library (index);
    CHECK (library.size() == 4);
    CHECK (library.getFolders().size() == 1);

    library.startScan();
    library.waitForScan();
    CHECK (library.getProgress().unchanged == 3);
    CHECK (library.getProgress().analysed == 0);
    CHECK (library.size() == 3);
}
