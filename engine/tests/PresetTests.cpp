#include "effects/BuiltinEffects.h"
#include "session/Presets.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace gk;
using model::Json;

TEST_CASE ("Factory presets name real parameters, in range")
{
    for (auto& [kind, list] : presets::factory().items())
    {
        if (kind == "sampler")
        {
            for (auto& preset : list)
            {
                INFO (preset["name"]);
                model::Reader reader;

                if (preset.contains ("filter"))
                {
                    model::SamplerFilter filter;
                    CHECK_NOTHROW (readValue (reader, preset["filter"], filter));
                }

                if (preset.contains ("envelope"))
                {
                    model::Envelope envelope;
                    CHECK_NOTHROW (readValue (reader, preset["envelope"], envelope));
                }
            }

            continue;
        }

        model::BuiltinEffectKind effectKind {};
        model::Reader reader;
        REQUIRE_NOTHROW (readValue (reader, Json (kind), effectKind));
        auto& info = effects::getEffectInfo (effectKind);

        for (auto& preset : list)
        {
            for (auto& [id, value] : preset["params"].items())
            {
                INFO (kind << " preset " << preset["name"] << ": " << id);
                auto param = info.findParam (id);
                REQUIRE (param != nullptr);
                CHECK (value.get<double>() >= param->min);
                CHECK (value.get<double>() <= param->max);
            }
        }
    }
}

TEST_CASE ("User presets are saved, listed and deleted")
{
    const auto folder = test::tempDir() / "presets";
    std::filesystem::remove_all (folder);

    presets::save ("reverb", { { "name", "My/Room" }, { "params", { { "decay", 1.1 } } }, { "mix", 0.2 } }, folder);
    auto listed = presets::list ("reverb", folder);
    REQUIRE (listed["user"].size() == 1);
    CHECK (listed["user"][0]["name"] == "My-Room");
    CHECK (listed["user"][0]["params"]["decay"] == 1.1);
    CHECK (listed["factory"].size() > 0);

    presets::remove ("reverb", "My-Room", folder);
    CHECK (presets::list ("reverb", folder)["user"].empty());
    CHECK_THROWS (presets::list ("nothing", folder));
}
