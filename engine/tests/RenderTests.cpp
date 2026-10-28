// Renders every tests/songs/<name>/song.json and checks the result against expect.json.
//
// expect.json fields (all optional):
//   render:          { "tailSeconds", "sampleRate", "start", "end" } passed to the renderer
//   durationSeconds, peakDb, rmsDb, integratedLufs:  whole-file values
//   onsets:          onset times in seconds (threshold -40 dBFS)
//   segments:        [{ "start", "end", "channel", "peakDb", "rmsDb", "silent" }]
//   tolerance:       { "db" (default 0.05), "seconds" (default 0.0005), "lufs" (default 0.1) }
// A value may be a number (checked within the tolerance) or a [min, max] range.

#include "TestHelpers.h"

#include "analysis/Analysis.h"
#include "compiler/EditCompiler.h"
#include "render/Render.h"
#include "render/Stretch.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace gk;
using Catch::Approx;
using model::Json;

namespace
{
    struct Tolerances
    {
        double db = 0.05, seconds = 0.0005, lufs = 0.1;
    };

    void checkValue (const std::string& what, double actual, const Json& expected, double tolerance)
    {
        INFO (what << ": actual " << actual << ", expected " << expected.dump());

        if (expected.is_array())
        {
            CHECK (actual >= expected[0].get<double>());
            CHECK (actual <= expected[1].get<double>());
        }
        else
        {
            CHECK (std::abs (actual - expected.get<double>()) <= tolerance);
        }
    }

    void checkRender (const std::filesystem::path& songDir)
    {
        auto expect = Json::parse (std::ifstream (songDir / "expect.json"));
        auto project = model::loadProject (songDir / "song.json");

        Tolerances tol;

        if (auto t = expect.find ("tolerance"); t != expect.end())
        {
            tol.db = t->value ("db", tol.db);
            tol.seconds = t->value ("seconds", tol.seconds);
            tol.lufs = t->value ("lufs", tol.lufs);
        }

        RenderOptions options;

        if (auto r = expect.find ("render"); r != expect.end())
        {
            options.tailSeconds = r->value ("tailSeconds", 0.0);
            options.sampleRate = r->value ("sampleRate", options.sampleRate);

            if (r->contains ("start")) options.start = (*r)["start"].get<int64_t>();
            if (r->contains ("end"))   options.end = (*r)["end"].get<int64_t>();
        }

        auto output = test::tempDir() / (songDir.filename().string() + ".wav");
        auto compiled = compileProject (test::getEngine(), project, { songDir, tracktion::Edit::forRendering });
        renderToWav (compiled, project, output, options);

        auto audio = analysis::loadAudio (output);
        REQUIRE (audio.buffer.getNumChannels() == 2);

        if (expect.contains ("durationSeconds"))
            checkValue ("duration", audio.getDurationSeconds(), expect["durationSeconds"], tol.seconds);

        if (expect.contains ("peakDb"))
            checkValue ("peak", analysis::peakDb (audio), expect["peakDb"], tol.db);

        if (expect.contains ("rmsDb"))
            checkValue ("rms", analysis::rmsDb (audio), expect["rmsDb"], tol.db);

        if (expect.contains ("integratedLufs"))
            checkValue ("integrated loudness", analysis::integratedLufs (audio), expect["integratedLufs"], tol.lufs);

        if (expect.contains ("onsets"))
        {
            auto actual = analysis::detectOnsets (audio);
            auto& expected = expect["onsets"];

            INFO ("onsets: actual " << Json (actual).dump() << ", expected " << expected.dump());
            REQUIRE (actual.size() == expected.size());

            for (size_t i = 0; i < actual.size(); ++i)
                checkValue ("onset " + std::to_string (i), actual[i], expected[i], tol.seconds);
        }

        for (auto& segment : expect.value ("segments", Json::array()))
        {
            analysis::Range range { segment.value ("start", 0.0), {}, {} };

            if (segment.contains ("end"))     range.end = segment["end"].get<double>();
            if (segment.contains ("channel")) range.channel = segment["channel"].get<int>();

            const auto label = "segment " + segment.dump();

            if (segment.value ("silent", false))
                checkValue (label + " peak", analysis::peakDb (audio, range), Json::array ({ -1000, -90 }), 0);

            if (segment.contains ("peakDb"))
                checkValue (label + " peak", analysis::peakDb (audio, range), segment["peakDb"], tol.db);

            if (segment.contains ("rmsDb"))
                checkValue (label + " rms", analysis::rmsDb (audio, range), segment["rmsDb"], tol.db);
        }
    }
}

TEST_CASE ("Render tests")
{
    const auto songsDir = test::repoRoot() / "tests" / "songs";
    REQUIRE (std::filesystem::is_directory (songsDir));

    std::vector<std::filesystem::path> songs;

    for (auto& entry : std::filesystem::directory_iterator (songsDir))
        if (std::filesystem::exists (entry.path() / "expect.json"))
            songs.push_back (entry.path());

    std::sort (songs.begin(), songs.end());
    REQUIRE (! songs.empty());

    for (auto& song : songs)
        DYNAMIC_SECTION (song.filename().string())
            checkRender (song);
}

TEST_CASE ("Resampling changes speed and pitch together, without aliasing")
{
    constexpr double rate = 48000;
    const auto sine = [&] (double hz, int frames)
    {
        juce::AudioBuffer<float> b (1, frames);

        for (int i = 0; i < frames; ++i)
            b.setSample (0, i, static_cast<float> (0.5 * std::sin (2 * juce::MathConstants<double>::pi * hz * i / rate)));

        return b;
    };

    // A 1 kHz tone twice as fast: half as long and an octave up.
    const auto faster = resampleAudio (sine (1000, 48000), rate, 2);
    CHECK (faster.getNumSamples() == 24000);
    int crossings = 0;

    for (int i = 2001; i < 22000; ++i)
        crossings += (faster.getSample (0, i - 1) < 0) != (faster.getSample (0, i) < 0);

    CHECK (crossings == Approx (2 * 2000 * 20000 / rate).margin (2));
    CHECK (faster.getMagnitude (0, 2000, 20000) == Approx (0.5f).margin (0.03f));

    // Twice as slow: twice as long, an octave down.
    CHECK (resampleAudio (sine (1000, 4800), rate, 0.5).getNumSamples() == 9600);

    // An 8 kHz tone 8 times as fast would land at 64 kHz: taken out rather than folded back down.
    CHECK (resampleAudio (sine (8000, 48000), rate, 8).getMagnitude (0, 1000, 4000) < 0.01f);
}
