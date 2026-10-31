// A check of the kick matching on a real library, run by hand: [.kickcheck], with
// GROOVEKIT_KICK_INDEX naming a kick-sounds.json. Real one-shot kicks are each played in a groove
// with hats, claps on two and four and a rumble, rebuilt from it as a reference's kick is, and
// ranked against every kick in the index: the kick itself should come first.
// GROOVEKIT_KICK_RUMBLE sets the rumble's level (default 6, 0 none), GROOVEKIT_KICK_ALONE leaves
// out the hats and claps.
#include "TestHelpers.h"
#include "analysis/Analysis.h"
#include "listening/KickSound.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iostream>
#include <random>

using namespace gk;
using namespace gk::listening;
using model::Json;

TEST_CASE ("Real kicks come back first from a groove they're buried in", "[.kickcheck]")
{
    const auto* file = std::getenv ("GROOVEKIT_KICK_INDEX");
    REQUIRE (file != nullptr);
    std::ifstream in (file);
    const auto j = Json::parse (in);

    std::vector<std::pair<std::string, KickSound>> kicks;

    for (auto& e : j["samples"])
        if (e.contains ("sound") && e["sound"].contains ("print"))
            kicks.emplace_back (e.value ("path", ""), KickSound::fromJson (e["sound"]));

    std::vector<size_t> shots;

    for (size_t i = 0; i < kicks.size(); ++i)
        if (kicks[i].second.pattern.empty() && juce::String (kicks[i].first).containsIgnoreCase ("kick"))
            shots.push_back (i);

    std::mt19937 random (7);
    std::shuffle (shots.begin(), shots.end(), random);
    shots.resize (std::min<size_t> (shots.size(), 40));

    const char* names[] = { "all", "early", "body", "sweep", "measured", "early+body" };
    std::vector<std::vector<int>> ranks (std::size (names));
    std::vector<float> own, twentieth, fifth;
    constexpr double rate = 44100, beat = 60 / 150.0;
    const float rumbleLevel = std::getenv ("GROOVEKIT_KICK_RUMBLE") != nullptr ? std::stof (std::getenv ("GROOVEKIT_KICK_RUMBLE")) : 6.0f;
    const bool others = std::getenv ("GROOVEKIT_KICK_ALONE") == nullptr;

    for (auto index : shots)
    {
        const auto shot = analysis::loadAudio (kicks[index].first, 1.0);
        juce::AudioBuffer<float> groove (2, static_cast<int> (16 * beat * rate));
        groove.clear();
        juce::Random noise (static_cast<int> (index));
        float rumble = 0;

        for (int k = 0; k < 16; ++k)
        {
            const auto start = static_cast<int> (k * beat * rate);

            for (int i = 0; i < static_cast<int> (beat * rate) && start + i < groove.getNumSamples(); ++i)
            {
                const double t = i / rate;
                float v = 0;
                const auto at = static_cast<int> (t * shot.sampleRate);

                if (at < shot.buffer.getNumSamples())
                    v = shot.buffer.getSample (0, at);

                // A low, dark rumble swelling after each kick, hats on the off-beats, claps on two and four.
                rumble = 0.995f * rumble + 0.005f * (noise.nextFloat() * 2 - 1);
                v += static_cast<float> (rumbleLevel * rumble * std::sin (juce::MathConstants<double>::pi * t / beat) * (t > 0.12 ? 1 : 0));

                if (others && t > beat / 2 && t < beat / 2 + 0.03)
                    v += 0.15f * (noise.nextFloat() * 2 - 1);

                if (others && k % 2 == 1 && t < 0.08)
                    v += static_cast<float> (0.3 * (noise.nextFloat() * 2 - 1) * std::exp (-t / 0.02));

                for (int c = 0; c < 2; ++c)
                    groove.setSample (c, start + i, v);
            }
        }

        KickTiming timing;
        timing.beat = beat;
        timing.downbeat = 0.0;
        const auto rebuilt = rebuildKick (groove, rate, timing);

        if (! rebuilt)
            continue;

        KickSound reference = *measureKick (groove, rate, timing);
        reference.print = printKick (rebuilt->audio, rate, rebuilt->seconds);

        if (! reference.print)
            continue;

        std::vector<std::vector<float>> scores (std::size (names));

        for (auto& [path, sound] : kicks)
        {
            const auto c = kickMatchCosts (sound, reference, {});
            MatchWeights w;
            scores[0].push_back (w.score (c));
            scores[1].push_back (-c[MatchWeights::early].value_or (9));
            scores[2].push_back (-c[MatchWeights::body].value_or (9));
            scores[3].push_back (-c[MatchWeights::sweep].value_or (9));
            scores[4].push_back (-c[MatchWeights::measured].value_or (9));
            scores[5].push_back (-c[MatchWeights::early].value_or (9) - c[MatchWeights::body].value_or (9));
        }

        {
            auto sorted = scores[0];
            std::sort (sorted.rbegin(), sorted.rend());
            own.push_back (scores[0][index]), fifth.push_back (sorted[4]), twentieth.push_back (sorted[19]);
        }

        for (size_t s = 0; s < scores.size(); ++s)
        {
            const auto mine = scores[s][index];
            ranks[s].push_back (static_cast<int> (std::count_if (scores[s].begin(), scores[s].end(), [&] (float v) { return v > mine; })));
        }
    }

    for (auto* v : { &own, &fifth, &twentieth })
        std::sort (v->begin(), v->end());

    std::cout << "score of the kick itself: median " << own[own.size() / 2] << ", quartile " << own[own.size() / 4]
              << "; fifth best " << fifth[fifth.size() / 2] << "; twentieth best " << twentieth[twentieth.size() / 2] << "\n";

    for (size_t s = 0; s < ranks.size(); ++s)
    {
        auto r = ranks[s];
        std::sort (r.begin(), r.end());
        const auto top1 = std::count (r.begin(), r.end(), 0), top10 = std::count_if (r.begin(), r.end(), [] (int x) { return x < 10; });
        std::cout << names[s] << ": median rank " << r[r.size() / 2] << ", first " << top1 << "/" << r.size() << ", top ten " << top10 << "/" << r.size() << "\n";
    }
}
