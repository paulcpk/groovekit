#include "music/Keys.h"
#include "music/Notes.h"
#include "music/Versions.h"

#include <juce_core/juce_core.h>

#include <filesystem>
#include <map>
#include <set>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gk::music;
using Catch::Approx;

namespace
{
    constexpr double voiceRate = 16000;

    double hz (double midi)  { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

    /** A voice-like tone (a few harmonics) at a MIDI pitch, faded in and out over 15 ms. */
    void sing (std::vector<float>& out, double midi, double seconds, double sampleRate = voiceRate, float level = 0.3f)
    {
        const auto n = static_cast<int> (seconds * sampleRate), fade = static_cast<int> (0.015 * sampleRate);
        double phase = 0;

        for (int i = 0; i < n; ++i)
        {
            phase += hz (midi) / sampleRate;
            const auto p = juce::MathConstants<double>::twoPi * phase;
            const auto envelope = std::min ({ 1.0, i / static_cast<double> (fade), (n - i) / static_cast<double> (fade) });
            out.push_back (static_cast<float> (level * envelope * (std::sin (p) + 0.5 * std::sin (2 * p) + 0.25 * std::sin (3 * p))));
        }
    }

    void rest (std::vector<float>& out, double seconds, double sampleRate = voiceRate)
    {
        out.insert (out.end(), static_cast<size_t> (seconds * sampleRate), 0.0f);
    }

    /** Speech-like: short syllables gliding about in pitch. */
    void speak (std::vector<float>& out, double seconds)
    {
        juce::Random random (7);
        double phase = 0;

        for (double t = 0; t < seconds; t += 0.13)
        {
            const auto from = 50 + random.nextDouble() * 6, to = 50 + random.nextDouble() * 6;
            const auto n = static_cast<int> (0.1 * voiceRate);

            for (int i = 0; i < n; ++i)
            {
                phase += hz (from + (to - from) * i / n) / voiceRate;
                const auto envelope = std::sin (juce::MathConstants<double>::pi * i / n);
                out.push_back (static_cast<float> (0.3 * envelope * std::sin (juce::MathConstants<double>::twoPi * phase)));
            }

            rest (out, 0.03);
        }
    }
}

TEST_CASE ("Keys are named, compared and moved between")
{
    REQUIRE (parseKey ("Am"));
    CHECK (*parseKey ("Am") == Key { 9, true });
    CHECK (*parseKey ("F# major") == Key { 6, false });
    CHECK (*parseKey ("ebm") == Key { 3, true });
    CHECK (! parseKey ("H minor"));
    CHECK (keyName ({ 10, true }) == "Bb minor");

    // A minor is C major's notes; D minor is a fifth away; F# major is far.
    CHECK (semitonesBetween ({ 9, true }, { 0, false }) == 0);
    CHECK (semitonesBetween ({ 2, true }, { 9, true }) == -5);
    CHECK (compatible ({ 9, true }, { 0, false }));
    CHECK (compatible ({ 9, true }, { 2, true }));
    CHECK (! compatible ({ 9, true }, { 6, false }));

    // Snapping: C# isn't in A minor.
    CHECK (Key { 9, true }.snap (61) == 60);
    CHECK (Key { 9, true }.snap (69) == 69);
}

TEST_CASE ("A key is heard in audio")
{
    // An A minor progression: Am, F, G, Am, with the tonic in the bass.
    std::vector<float> audio;
    const double rate = 44100;

    for (auto chord : { std::vector<double> { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 43, 55, 59, 62 }, { 45, 57, 60, 64 } })
    {
        const auto n = static_cast<int> (rate);

        for (int i = 0; i < n; ++i)
        {
            double v = 0;

            for (auto note : chord)
                v += std::sin (juce::MathConstants<double>::twoPi * hz (note) * i / rate);

            audio.push_back (static_cast<float> (0.15 * v));
        }
    }

    const auto chroma = chromaOf (audio.data(), static_cast<int> (audio.size()), rate);
    const auto estimate = estimateKey (chroma);
    REQUIRE (estimate);
    CHECK (estimate->key == Key { 9, true });
    CHECK (estimate->confidence > 0.3f);

    // Noise has no key to speak of.
    juce::Random random (3);
    std::vector<float> noise (static_cast<size_t> (rate));
    for (auto& s : noise) s = random.nextFloat() * 0.4f - 0.2f;
    const auto noiseKey = estimateKey (chromaOf (noise.data(), static_cast<int> (noise.size()), rate));
    CHECK ((! noiseKey || noiseKey->confidence < 0.3f));
}

TEST_CASE ("A hummed melody becomes notes")
{
    std::vector<float> audio;
    rest (audio, 0.2);

    // "da da da daa": C4 D4 E4 C4, with short gaps for the consonants.
    for (auto [midi, length] : { std::pair { 60.0, 0.3 }, { 62.0, 0.3 }, { 64.0, 0.3 }, { 60.0, 0.6 } })
    {
        sing (audio, midi + 0.2, length); // a little sharp, as people are
        rest (audio, 0.04);
    }

    const auto frames = trackPitch (audio.data(), static_cast<int> (audio.size()), voiceRate);
    const auto notes = segmentNotes (frames);
    REQUIRE (notes.size() == 4);
    CHECK (notes[0].pitch == Approx (60.2f).margin (0.15f));
    CHECK (notes[1].pitch == Approx (62.2f).margin (0.15f));
    CHECK (notes[2].pitch == Approx (64.2f).margin (0.15f));
    CHECK (notes[3].end - notes[3].start == Approx (0.6).margin (0.05));
    CHECK (notes[1].start == Approx (0.54).margin (0.03));
}

TEST_CASE ("The sung part of a voice message is found after the words")
{
    std::vector<float> audio;
    speak (audio, 2.0);
    rest (audio, 0.4);
    const auto sungFrom = static_cast<double> (audio.size()) / voiceRate;

    for (auto midi : { 57.0, 60.0, 62.0, 60.0, 57.0 })
    {
        sing (audio, midi, 0.25);
        rest (audio, 0.05);
    }

    const auto part = findSungPart (trackPitch (audio.data(), static_cast<int> (audio.size()), voiceRate));
    REQUIRE (part);
    CHECK (part->start == Approx (sungFrom).margin (0.06));

    // Words alone have no melody in them.
    std::vector<float> words;
    speak (words, 3.0);
    CHECK (! findSungPart (trackPitch (words.data(), static_cast<int> (words.size()), voiceRate)));
}

TEST_CASE ("Notes are placed on the song's grid, in its key and register")
{
    // Hummed at 100 BPM in D major: 8ths and a quarter, D E F# D, a bit loose.
    std::vector<HeardNote> heard {
        { 1.00, 1.28, 62.1f, 1 }, { 1.31, 1.58, 64.0f, 1 }, { 1.59, 1.88, 66.2f, 1 }, { 1.90, 2.48, 62.0f, 1 },
        { 2.50, 2.78, 69.0f, 1 }, { 2.80, 3.10, 67.0f, 1 },
    };

    FitOptions options;
    options.songTempo = 140;
    options.key = Key { 0, false };     // C major
    options.lowest = 48;
    options.highest = 72;

    const auto melody = fitMelody (heard, options);
    CHECK (melody.sourceTempo == Approx (100).margin (3));
    REQUIRE (melody.notes.size() == 6);

    // 8ths are 480 ticks.
    CHECK (melody.notes[0].start == 0);
    CHECK (melody.notes[1].start == 480);
    CHECK (melody.notes[2].start == 960);
    CHECK (melody.notes[3].start == 1440);
    CHECK (melody.notes[3].length == 960);

    // D major down to C major: D E F# D is C D E C.
    CHECK (melody.notes[0].key == 60);
    CHECK (melody.notes[1].key == 62);
    CHECK (melody.notes[2].key == 64);
    CHECK (melody.transposed == -2);
    CHECK (melody.length == 3840);

    SECTION ("a bassline goes down into its register")
    {
        options.lowest = 28;
        options.highest = 48;
        const auto bass = fitMelody (heard, options);
        CHECK (bass.notes[0].key == 36);
        CHECK (bass.transposed == -26);
    }
}

TEST_CASE ("A hummed line comes back in versions: in key, on the scale, the style's rhythm, simpler, faster, slower, answering")
{
    // Hummed at 100 BPM, a little off key (F# and C# in C minor): 8ths, a quarter, 8ths.
    std::vector<HeardNote> heard {
        { 1.00, 1.28, 60.1f, 1 }, { 1.31, 1.58, 63.0f, 1 }, { 1.59, 1.88, 66.2f, 1 }, { 1.90, 2.48, 61.0f, 1 },
        { 2.50, 2.78, 67.0f, 1 }, { 2.80, 3.10, 65.0f, 1 },
    };

    FitOptions options;
    options.songTempo = 140;
    options.key = Key { 0, true };      // C minor
    const auto line = fitMelody (heard, options);
    auto rawOptions = options;
    rawOptions.snapToScale = false;
    rawOptions.sourceTempo = line.sourceTempo;
    const auto raw = fitMelody (heard, rawOptions);

    VersionOptions v;
    v.key = options.key;
    v.count = 10;
    v.styleRhythms.push_back ({ { 0, 3, 6, 8, 11, 14 }, 16 });
    v.partner = { { 0, 240, 36, 100 }, { 960, 240, 36, 100 }, { 1920, 240, 36, 100 }, { 2880, 240, 36, 100 } };
    v.partnerLength = 3840;
    v.partnerName = "Kick";

    const auto versions = melodyVersions (line, raw, v);
    REQUIRE (versions.size() >= 5);

    // As hummed first, closest to itself.
    CHECK (versions[0].name == "hummed");
    CHECK (versions[0].closeness == 100);

    std::map<std::string, MelodyVersion> byName;

    for (auto& version : versions)
    {
        byName[version.name] = version;

        // Every version in key, on the grid, within its length.
        for (auto& n : version.notes)
        {
            CHECK (options.key->contains (n.key % 12));
            CHECK (n.start % 60 == 0);
            CHECK (n.start + n.length <= version.length);
        }
    }

    // The style's rhythm: onsets on its 16ths, and it fits the style better than the hum does.
    REQUIRE (byName.contains ("groove"));
    for (auto& n : byName["groove"].notes)
        CHECK (std::set<int> { 0, 3, 6, 8, 11, 14 }.contains (static_cast<int> ((n.start / 240) % 16)));
    CHECK (byName["groove"].fit.value_or (0) > versions[0].fit.value_or (0));

    // Half speed is twice as long; the simpler one has fewer notes.
    REQUIRE (byName.contains ("half"));
    CHECK (byName["half"].length == 2 * versions[0].length);
    REQUIRE (byName.contains ("simpler"));
    CHECK (byName["simpler"].notes.size() < versions[0].notes.size() * 2);

    // Answering the kick: no note where it plays.
    REQUIRE (byName.contains ("answer"));
    for (auto& n : byName["answer"].notes)
        CHECK ((n.start / 240) % 4 != 0);
}

TEST_CASE ("Triplet and swung hums keep their feel, and the kinds of version picked rise")
{
    FitOptions options;
    options.songTempo = 120;
    options.sourceTempo = 120;          // a beat is half a second
    options.key = Key { 9, true };      // A minor

    // 8th-note triplets: three notes a beat, a little loose.
    std::vector<HeardNote> triplets;

    for (int i = 0; i < 12; ++i)
    {
        const auto t = 1.0 + i / 6.0 + (i % 2 ? 0.006 : -0.004);
        triplets.push_back ({ t, t + 0.14, static_cast<float> (57 + (i % 3) * 3), 1 });
    }

    const auto t = fitMelody (triplets, options);
    CHECK (t.feel == FitOptions::Feel::triplet);
    REQUIRE (t.notes.size() == 12);

    for (size_t i = 0; i < t.notes.size(); ++i)
        CHECK (t.notes[i].start == static_cast<int64_t> (i) * 320);

    // Swung 16ths: every second one two thirds of the way through its pair.
    std::vector<HeardNote> swung;

    for (int i = 0; i < 16; ++i)
    {
        const auto pair = i / 2, late = i % 2;
        const auto at = 1.0 + pair * 0.25 + late * 0.25 * 0.66;
        swung.push_back ({ at, at + 0.08, static_cast<float> (57 + (i % 4) * 2), 1 });
    }

    const auto s = fitMelody (swung, options);
    CHECK (s.feel == FitOptions::Feel::swing);
    CHECK (s.swingUnit == Approx (0.25));
    CHECK (s.swing == Approx (0.66).margin (0.03));
    REQUIRE (s.notes.size() == 16);
    CHECK (s.notes[0].start == 0);
    CHECK (s.notes[1].start == Approx (317).margin (20));      // 0.66 of a pair of 16ths (480 ticks)
    CHECK (s.notes[2].start == 480);

    // Told straight, it's put on 16ths.
    auto straight = options;
    straight.feel = FitOptions::Feel::straight;
    CHECK (fitMelody (swung, straight).feel == FitOptions::Feel::straight);

    // A straight line gets a shuffled version; picks steer which come first, and a kind passed
    // over many times is left out.
    std::vector<HeardNote> line {
        { 1.00, 1.10, 57, 1 }, { 1.125, 1.22, 60, 1 }, { 1.25, 1.35, 64, 1 }, { 1.375, 1.47, 62, 1 },
        { 1.50, 1.70, 60, 1 }, { 1.75, 1.85, 57, 1 }, { 1.875, 1.97, 55, 1 }, { 2.00, 2.40, 57, 1 },
    };
    const auto plain = fitMelody (line, options);
    REQUIRE (plain.feel == FitOptions::Feel::straight);

    VersionOptions v;
    v.key = options.key;
    v.count = 10;
    auto names = [] (const std::vector<MelodyVersion>& list)
    {
        std::vector<std::string> out;

        for (auto& x : list)
            out.push_back (x.name);

        return out;
    };

    const auto first = names (melodyVersions (plain, plain, v));
    CHECK (std::find (first.begin(), first.end(), "shuffle") != first.end());
    REQUIRE (first.size() >= 4);

    v.picks["double"] = { 6, 5 };
    v.picks["half"] = { 9, 0 };
    const auto learned = names (melodyVersions (plain, plain, v));
    CHECK (learned[0] == "hummed");
    CHECK (learned[1] == "double");
    CHECK (std::find (learned.begin(), learned.end(), "half") == learned.end());

    // Kept in a file.
    const auto file = std::filesystem::temp_directory_path() / "gk-melody-picks.json";
    std::filesystem::remove (file);
    addVersionPick (file, "simpler", { "hummed", "simpler", "double" });
    addVersionPick (file, "simpler", { "hummed", "simpler" });
    const auto kept = readVersionPicks (file);
    CHECK (kept.at ("simpler").chosen == 2);
    CHECK (kept.at ("simpler").shown == 2);
    CHECK (kept.at ("double").shown == 1);
    std::filesystem::remove (file);
}

TEST_CASE ("The top line of chords is the melody, the bottom one the bass")
{
    std::vector<HeardNote> chords {
        { 0.0, 1.0, 48, 1 }, { 0.0, 0.5, 64, 1 }, { 0.0, 1.0, 60, 1 }, { 0.5, 1.0, 67, 1 },
    };

    const auto top = topLine (chords);
    REQUIRE (top.size() == 2);
    CHECK (top[0].pitch == 64);
    CHECK (top[1].pitch == 67);

    const auto bottom = bottomLine (chords);
    REQUIRE (bottom.size() == 1);
    CHECK (bottom[0].pitch == 48);
    CHECK (bottom[0].end == 1.0);
}

TEST_CASE ("A melody is taken from a voice message only when it says there's one")
{
    CHECK (mentionsMelody ("This is the melody for the stab"));
    CHECK (mentionsMelody ("the bassline goes like da da da"));
    CHECK (mentionsMelody ("I'll hum it"));
    CHECK (mentionsMelody ("Humming: the lead"));
    CHECK (mentionsMelody ("Die Melodie ist so"));

    // What the user said to the agent in a long voice message, with no melody in it.
    CHECK_FALSE (mentionsMelody ("Let's reduce the volume on the hats and metal by 25%. And the human touch, you know, the stab needs reverb."));
    CHECK_FALSE (mentionsMelody ("Kick, low-pass, kick, high-pass, and smooth it out by reducing the high-end."));
}
