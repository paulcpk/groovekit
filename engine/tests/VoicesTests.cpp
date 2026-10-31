// Voices: the melodic parts of a track through it, how they pair, a style's library of them, and
// parts written from it.

#include "listening/VoiceLibrary.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace gk;
using namespace gk::listening;

namespace
{
    /** A figure (steps in its bars, one key each) played over bars [from, to). */
    void play (std::vector<GridNote>& notes, const std::vector<std::pair<int, int>>& figure, int figureBars, int from, int to, int steps = 1)
    {
        for (int bar = from; bar < to; bar += figureBars)
            for (auto [step, key] : figure)
                if (bar * 16 + step < to * 16)
                    notes.push_back ({ bar * 16 + step, steps, key, 100 });

        std::sort (notes.begin(), notes.end(), [] (auto& x, auto& y) { return x.step != y.step ? x.step < y.step : x.key < y.key; });
    }

    Voice voiceOf (const std::string& role, const std::vector<GridNote>& notes, int bars)
    {
        Voice v;
        v.role = role;
        v.part = role == "bass" ? "bass" : "music";
        v.runs = foldRuns (notes, bars);
        return v;
    }
}

TEST_CASE ("A voice's notes fold into runs of a figure, with how each run changes")
{
    std::vector<GridNote> notes;
    const std::vector<std::pair<int, int>> stab { { 2, 53 }, { 6, 53 }, { 10, 56 }, { 14, 53 } };
    std::vector<std::pair<int, int>> up;

    for (auto [step, key] : stab)
        up.push_back ({ step, key + 2 });

    play (notes, stab, 1, 0, 8);
    play (notes, up, 1, 8, 12);
    play (notes, stab, 1, 12, 16);

    // A note heard a step late once, and a fill: still the same figure.
    notes[5].step += 1;

    auto runs = foldRuns (notes, 16);
    REQUIRE (runs.size() == 3);
    CHECK (runs[0].from == 0);
    CHECK (runs[0].to == 8);
    CHECK (runs[0].bars == 1);
    CHECK (runs[0].notes.size() == 4);
    CHECK (runs[1].from == 8);
    CHECK (runs[1].change == "transposed +2");
    CHECK (runs[2].change == "as bars 1-8");

    Voice v;
    v.runs = runs;
    CHECK (v.playsIn (9));
    CHECK_FALSE (v.playsIn (16));
    REQUIRE (v.notesIn (9).size() == 4);
    CHECK (v.notesIn (9)[0].key == 55);
}

TEST_CASE ("A figure that repeats every two bars folds into two-bar runs")
{
    std::vector<GridNote> notes;
    play (notes, { { 0, 77 }, { 2, 77 }, { 4, 80 }, { 16, 84 }, { 18, 82 }, { 20, 80 } }, 2, 0, 8);

    auto runs = foldRuns (notes, 8);
    REQUIRE (runs.size() == 1);
    CHECK (runs[0].bars == 2);
    CHECK (runs[0].notes.size() == 6);
}

TEST_CASE ("A part that plays after another's phrase answers it; parts alternating note by note interlock")
{
    std::vector<GridNote> call, answer, bass, stab;
    play (call, { { 0, 77 }, { 2, 77 }, { 4, 80 } }, 2, 0, 8, 2);
    play (answer, { { 16, 84 }, { 18, 82 }, { 20, 80 } }, 2, 0, 8, 2);
    play (bass, { { 1, 29 }, { 3, 29 }, { 5, 29 }, { 7, 29 }, { 9, 29 }, { 11, 29 }, { 13, 29 }, { 15, 29 } }, 1, 0, 8);
    play (stab, { { 2, 53 }, { 6, 53 }, { 10, 53 }, { 14, 53 } }, 1, 0, 8);

    // The answer first: the second of the pair is the one that answers.
    auto pairs = pairVoices ({ voiceOf ("pluck", answer, 8), voiceOf ("lead", call, 8) }, 8);
    REQUIRE (pairs.size() == 1);
    CHECK (pairs[0].answers);
    CHECK (pairs[0].a == 1);
    CHECK (pairs[0].b == 0);
    CHECK (pairs[0].answerAfter == 10);

    pairs = pairVoices ({ voiceOf ("bass", bass, 8), voiceOf ("stab", stab, 8) }, 8);
    REQUIRE (pairs.size() == 1);
    CHECK_FALSE (pairs[0].answers);
    CHECK (pairs[0].together == 0.0f);
    CHECK (pairs[0].fills == 1.0f);
    REQUIRE_FALSE (pairs[0].intervals.empty());
}

TEST_CASE ("A song's voices are its pitched channels through the playlist")
{
    model::Project p;
    model::Channel bass, stab, kick;
    bass.id = "bass", bass.part = model::ChannelPart::bass;
    stab.id = "stab", stab.part = model::ChannelPart::synth;
    kick.id = "kick", kick.part = model::ChannelPart::kick;
    p.channels = { bass, stab, kick };

    model::Pattern pattern;
    pattern.id = "groove";
    pattern.length = 3840;

    for (int step : { 2, 6, 10, 14 })
    {
        pattern.notes.push_back ({ "bass", step * 240 - 240, 240, 29, 100 });
        pattern.notes.push_back ({ "stab", step * 240, 240, 53, 100 });
        pattern.notes.push_back ({ "stab", step * 240, 240, 60, 100 });
        pattern.notes.push_back ({ "kick", (step - 2) * 240, 240, 60, 100 });
    }

    p.patterns = { pattern };
    model::PatternClip clip;
    clip.id = "c";
    clip.pattern = "groove";
    clip.start = 3840 * 2;
    clip.length = 3840 * 4;
    p.playlist.clips = { clip };
    p.key = "F minor";

    auto song = songVoices (p);
    REQUIRE (song.voices.voices.size() == 2);
    CHECK (song.channels == std::vector<std::string> { "bass", "stab" });
    CHECK (song.bars == 6);
    CHECK (song.voices.voices[0].role == "bass");
    CHECK (song.voices.voices[1].role == "stab");
    REQUIRE (song.voices.voices[1].runs.size() == 1);
    CHECK (song.voices.voices[1].runs[0].from == 2);
    CHECK (song.voices.voices[1].runs[0].to == 6);
    REQUIRE (song.voices.key);
    CHECK (music::keyName (song.voices.key->key) == "F minor");
}

TEST_CASE ("A style's voices write parts in the song's key, fitted to a partner, and find what a song lacks")
{
    VoiceLibrary lib;
    lib.style = "Test";
    lib.tracks = 4;

    VoiceLibrary::Role pluck;
    pluck.name = "pluck";
    pluck.tracks = 4;
    pluck.notesPerBar = 3;
    pluck.figures.push_back ({ 1, { { 8, 1, 72, 100 }, { 10, 1, 75, 100 }, { 11, 1, 77, 100 } }, 0.6f, 3 });

    VoiceLibrary::Role stab;
    stab.name = "stab";
    stab.tracks = 4;
    stab.notesPerBar = 3;
    stab.figures.push_back ({ 1, { { 0, 1, 60, 100 }, { 3, 1, 60, 100 }, { 6, 1, 60, 100 } }, 0.8f, 4 });

    lib.roles = { stab, pluck };
    lib.pairs.push_back ({ "stab", "pluck", 3, 0.0f, 1.0f, 1.0f, 1, {} });
    lib.atOnce.push_back ({ "drop", 2, 3 });

    WriteOptions options;
    options.role = "pluck";
    options.key = music::parseKey ("F minor");
    options.count = 3;

    auto written = writeVoices (lib, options);
    REQUIRE (written.size() == 3);
    CHECK (written[0].notes.front().key == 77);        // C5 moved up a fourth: F5

    for (auto& w : written)
        for (auto& n : w.notes)
            CHECK (options.key->contains (n.key % 12));

    // Answering a stab that plays the first half of the bar: in its silence after its phrase.
    options.partner = { { 0, 1, 53, 100 }, { 3, 1, 53, 100 }, { 6, 1, 53, 100 } };
    options.partnerBars = 1;
    options.partnerRole = "stab";
    options.count = 1;
    written = writeVoices (lib, options);
    REQUIRE (written.size() == 1);
    CHECK (written[0].relation == "answer");

    for (auto& n : written[0].notes)
        CHECK (n.step >= 7);

    // A song with a stab and nothing answering it.
    SongVoices song;
    song.bars = 8;
    std::vector<GridNote> notes;
    play (notes, { { 0, 53 }, { 3, 53 }, { 6, 53 } }, 1, 0, 8);
    song.voices.voices = { voiceOf ("stab", notes, 8) };
    song.channels = { "stab" };

    auto findings = checkVoices (song, lib, music::parseKey ("F minor"));
    REQUIRE_FALSE (findings.empty());
    CHECK_THAT (findings[0], Catch::Matchers::ContainsSubstring ("has no answer"));
}
