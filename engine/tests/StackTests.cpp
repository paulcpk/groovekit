// The multi-sampler: layers summed at equal loudness no louder than the loudest alone, loops laid
// over one clip, and its commands.

#include "TestHelpers.h"
#include "analysis/Analysis.h"
#include "compiler/EditCompiler.h"
#include "render/Render.h"
#include "sampler/Stack.h"
#include "session/Session.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gk;
using model::Json;

namespace
{
    Json samplerOf (const std::string& file, std::optional<double> sourceTempo = {})
    {
        Json layer { { "id", "s" }, { "sample", test::fixture (file).string() }, { "pitchTracking", false } };

        if (sourceTempo)
            layer["sourceTempo"] = *sourceTempo;

        return { { "type", "sampler" }, { "layers", Json::array ({ layer }) } };
    }

    Json song (const Json& instrument, bool clip = false)
    {
        Json project {
            { "schemaVersion", 5 }, { "tempo", 120 },
            { "channels", Json::array ({ { { "id", "c" }, { "insert", "master" }, { "instrument", instrument } } }) },
            { "patterns", Json::array ({ { { "id", "p" }, { "notes", Json::array ({ { { "channel", "c" }, { "start", 0 }, { "length", 480 }, { "key", 60 }, { "velocity", 127 } } }) } } }) },
            { "playlist", { { "rows", Json::array() }, { "clips", Json::array() } } },
            { "mixer", { { "inserts", Json::array ({ { { "id", "master" } } }) } } },
        };

        if (clip)
            project["playlist"]["clips"].push_back ({ { "type", "audio" }, { "id", "a" }, { "row", 0 }, { "start", 0 }, { "length", 3840 }, { "channel", "c" } });
        else
            project["playlist"]["clips"].push_back ({ { "type", "pattern" }, { "id", "k" }, { "row", 0 }, { "start", 0 }, { "length", 3840 }, { "pattern", "p" } });

        return project;
    }

    analysis::Audio render (const Json& json, const std::string& name)
    {
        const auto project = model::parseProject (json);
        RenderOptions options;
        options.end = 3840 * 2;
        const auto file = test::tempDir() / (name + ".wav");
        auto compiled = compileProject (test::getEngine(), project, { test::tempDir(), tracktion::Edit::forRendering });
        renderToWav (compiled, project, file, options);
        return analysis::loadAudio (file);
    }

    Json stackOf (const std::string& mode, std::vector<Json> layers, double drive = 0)
    {
        return { { "type", "stack" }, { "mode", mode }, { "drive", drive }, { "layers", layers } };
    }

    model::StackInstrument asStack (const Json& instrument)
    {
        return std::get<model::StackInstrument> (model::parseProject (song (instrument)).channels[0].instrument);
    }

    Json layer (const std::string& id, Json sampler, Json extra = Json::object())
    {
        Json l { { "id", id }, { "sampler", std::move (sampler) } };

        for (auto& [k, v] : extra.items())
            l[k] = v;

        return l;
    }
}

TEST_CASE ("A multi-sampler of hits is no louder than its loudest layer alone")
{
    const auto single = analysis::peakDb (render (song (samplerOf ("kick.wav")), "stack-single"));
    REQUIRE (single > -30);

    // Three of the same kick lined up would add up to almost 10 dB more; the output keeps it at one's.
    const auto three = stackOf ("hits", { layer ("a", samplerOf ("kick.wav")), layer ("b", samplerOf ("kick.wav")), layer ("c", samplerOf ("kick.wav")) });
    CHECK (analysis::peakDb (render (song (three), "stack-three")) == Catch::Approx (single).margin (0.5));

    // Mixed down and muted layers count for less, or nothing.
    auto quieter = three;
    quieter["layers"][1]["mix"] = 0.5;
    quieter["layers"][2]["mute"] = true;
    CHECK (analysis::peakDb (render (song (quieter), "stack-quieter")) == Catch::Approx (single).margin (0.5));

    // Drive saturates without getting louder, and nothing passes full scale.
    auto driven = three;
    driven["drive"] = 1;
    const auto drivenPeak = analysis::peakDb (render (song (driven), "stack-driven"));
    CHECK (drivenPeak <= single + 0.3);
    CHECK (drivenPeak <= 0.0);

    // A quiet sample is matched to the loud one: the hat alone in the stack is as loud as the kick alone.
    const auto mixStack = sampler::mixStack (asStack (stackOf ("hits", { layer ("k", samplerOf ("kick.wav")), layer ("h", samplerOf ("hat.wav")) })), {}, 120);
    CHECK (mixStack.layers.at ("k").matchDb + mixStack.layers.at ("k").loudnessDb
           == Catch::Approx (mixStack.layers.at ("h").matchDb + mixStack.layers.at ("h").loudnessDb).margin (0.1));
}

TEST_CASE ("A multi-sampler of loops plays every layer over one clip, each looping")
{
    // A one-beat-long master at its tempo, and a layer with none fitted to half of it (speed 2).
    const auto loops = stackOf ("loops", {
        layer ("m", samplerOf ("clicks_120bpm.wav", 120.0), { { "master", true } }),
        layer ("b", samplerOf ("beep.wav"), { { "speed", 2 } }),
    });

    const auto timings = sampler::loopTimings (asStack (loops), {}, 120);
    CHECK (timings.at ("m").cycle == Catch::Approx (1.0).margin (0.01));
    CHECK (timings.at ("b").cycle == Catch::Approx (0.5).margin (0.01));

    // The clip is a bar (2 s), twice the master: both play to its end, and stop there.
    const auto audio = render (song (loops, true), "stack-loops");
    CHECK (analysis::peakDb (audio, { 1.5, 2.0, {} }) > -40);
    CHECK (analysis::peakDb (audio, { 2.2, 4.0, {} }) < -90);

    // Played as a single sampler, the master alone stops after its one time round.
    const auto alone = render (song (samplerOf ("clicks_120bpm.wav", 120.0), true), "stack-loops-alone");
    CHECK (analysis::peakDb (alone, { 1.05, 2.0, {} }) < -90);
}

TEST_CASE ("Multi-sampler commands")
{
    Session session (Session::createEmptyProject());
    auto json = song (stackOf ("loops", { layer ("a", samplerOf ("clicks_120bpm.wav", 120.0), { { "master", true } }) }), true);
    session.execute ({ model::Command (model::AddChannel { model::parseProject (json).channels[0], {} }) }, {});
    const auto id = session.getProject().channels.back().id;

    // A second master takes the place of the first.
    session.execute ({ model::Command (model::AddStackLayer { id, asStack (stackOf ("loops", { layer ("b", samplerOf ("beep.wav"), { { "master", true } }) })).layers[0], {} }) }, {});
    auto stack = [&] { return std::get<model::StackInstrument> (session.getProject().channels.back().instrument); };
    CHECK_FALSE (stack().layers[0].master);
    CHECK (stack().layers[1].master);

    session.execute ({ model::Command (model::UpdateStackLayer { id, "a", Json { { "master", true }, { "mix", 0.5 } } }) }, {});
    CHECK (stack().layers[0].master);
    CHECK_FALSE (stack().layers[1].master);
    CHECK (stack().layers[0].mix == 0.5);

    // A layer's sampler is changed by naming the layer.
    session.execute ({ model::Command (model::UpdateSamplerLayer { id, "b", "s", Json { { "transpose", 3 } } }) }, {});
    CHECK (stack().layers[1].sampler.layers[0].transpose == 3);
    session.execute ({ model::Command (model::UpdateInstrument { id, "b", Json { { "polyphony", 4 } } }) }, {});
    CHECK (stack().layers[1].sampler.polyphony == 4);
    session.execute ({ model::Command (model::UpdateInstrument { id, {}, Json { { "drive", 0.3 } } }) }, {});
    CHECK (stack().drive == 0.3);

    // Without the layer, a sampler command says which is missing.
    CHECK_THROWS (session.execute ({ model::Command (model::UpdateSamplerLayer { id, {}, "s", Json { { "transpose", 1 } } }) }, {}));

    session.execute ({ model::Command (model::RemoveStackLayer { id, "b" }) }, {});
    CHECK (stack().layers.size() == 1);

    // Notes play a multi-sampler of hits, audio clips one of loops.
    auto hits = json;
    hits["channels"][0]["instrument"]["mode"] = "hits";
    CHECK_THROWS (model::validateOrThrow (model::parseProject (hits)));
    CHECK_NOTHROW (model::validateOrThrow (model::parseProject (json)));
}
