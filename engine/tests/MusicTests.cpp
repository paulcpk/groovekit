#include "music/Keys.h"
#include "music/Notes.h"

#include <juce_core/juce_core.h>

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
