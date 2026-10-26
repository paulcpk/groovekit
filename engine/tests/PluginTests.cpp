// Hosting real plugins: Apple's built-in Audio Units, which every Mac has.

#include "TestHelpers.h"

#include "analysis/Analysis.h"
#include "plugins/PluginCatalog.h"
#include "render/Render.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>

using namespace gk;
using model::Json;
using Catch::Matchers::ContainsSubstring;

namespace
{
    PluginCatalog& appleUnits()
    {
        static std::unique_ptr<PluginCatalog> instance;

        if (instance == nullptr)
        {
            instance = std::make_unique<PluginCatalog> (test::getEngine(), test::tempDir() / "plugins.xml");
            test::atShutdown ([] { instance.reset(); });
            auto& catalog = *instance;

            // Apple's units have the manufacturer code "appl" in their identifier.
            catalog.startScan ({}, [] (const juce::String& id) { return id.contains (",appl"); });

            while (catalog.getProgress().running)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }

        return *instance;
    }

    std::optional<juce::PluginDescription> findByName (PluginCatalog& catalog, const juce::String& name)
    {
        for (auto& d : catalog.getPlugins())
            if (d.name == name)
                return d;

        return {};
    }

    double renderPeak (const Json& insertEffects, const std::string& name)
    {
        auto doc = Json::parse (std::ifstream (test::repoRoot() / "tests" / "songs" / "beeps" / "song.json"));
        doc["mixer"]["inserts"][0]["effects"] = insertEffects;

        auto project = model::parseProject (doc);
        auto compiled = compileProject (test::getEngine(), project, { test::repoRoot() / "tests" / "songs" / "beeps", tracktion::Edit::forRendering });
        auto file = test::tempDir() / (name + ".wav");
        renderToWav (compiled, project, file, {});
        return analysis::rmsDb (analysis::loadAudio (file), { 0, 0.1, {} });
    }
}

TEST_CASE ("Scanning finds Apple's Audio Units")
{
    auto& catalog = appleUnits();
    auto lowpass = findByName (catalog, "AULowpass");
    REQUIRE (lowpass);

    auto json = PluginCatalog::toJson (*lowpass);
    CHECK (json["format"] == "AU");
    CHECK (json["instrument"] == false);
    CHECK (catalog.find (json["uid"].get<std::string>()));

    // The list survives a restart.
    PluginCatalog again (test::getEngine(), test::tempDir() / "plugins.xml");
    CHECK (again.find (json["uid"].get<std::string>()));
}

TEST_CASE ("A hosted plugin effect processes the mix")
{
    auto lowpass = findByName (appleUnits(), "AULowpass");
    REQUIRE (lowpass);

    auto effect = [&] (std::optional<double> cutoff)
    {
        Json plugin { { "format", "AU" }, { "uid", lowpass->createIdentifierString().toStdString() }, { "name", "AULowpass" } };

        if (cutoff)
            plugin["params"] = { { "Cutoff Frequency", *cutoff } };

        return Json::array ({ { { "type", "plugin" }, { "id", "lp" }, { "plugin", plugin } } });
    };

    const auto dry = renderPeak (Json::array(), "plugin-dry");
    const auto closed = renderPeak (effect (0.0), "plugin-closed");

    CHECK (dry > -10);
    CHECK (closed < dry - 20); // a 1 kHz beep through a lowpass at its lowest cutoff

    SECTION ("unknown plugins and parameters are reported")
    {
        auto doc = Json::parse (std::ifstream (test::repoRoot() / "tests" / "songs" / "beeps" / "song.json"));
        doc["mixer"]["inserts"][0]["effects"] = effect (0.5);
        doc["mixer"]["inserts"][0]["effects"][0]["plugin"]["params"] = { { "No such knob", 0.5 } };
        CHECK_THROWS_WITH (compileProject (test::getEngine(), model::parseProject (doc), { test::tempDir(), tracktion::Edit::forRendering }),
                           ContainsSubstring ("no parameter \"No such knob\""));

        doc["mixer"]["inserts"][0]["effects"][0]["plugin"]["uid"] = "AudioUnit-Nothing-0";
        CHECK_THROWS_WITH (compileProject (test::getEngine(), model::parseProject (doc), { test::tempDir(), tracktion::Edit::forRendering }),
                           ContainsSubstring ("isn't installed or hasn't been scanned"));
    }
}
