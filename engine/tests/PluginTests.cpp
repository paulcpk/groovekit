// Hosting real plugins: Apple's built-in Audio Units, which every Mac has.

#include "TestHelpers.h"

#include "analysis/Analysis.h"
#include "api/ApiHandler.h"
#include "live/LiveEngine.h"
#include "plugins/PluginCatalog.h"
#include "render/Render.h"

#include <catch2/catch_approx.hpp>
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

TEST_CASE ("Plugin state is captured into the project and restored")
{
    auto lowpass = findByName (appleUnits(), "AULowpass");
    REQUIRE (lowpass);

    auto dir = test::tempDir() / "plugin-state";
    std::filesystem::remove_all (dir);
    auto session = Session::open (dir);

    auto doc = Json::parse (std::ifstream (test::repoRoot() / "tests" / "songs" / "beeps" / "song.json"));
    doc["channels"][0]["instrument"]["layers"][0]["sample"] = test::fixture ("beep.wav").string();
    doc["mixer"]["inserts"][0]["effects"] = Json::array ({ { { "type", "plugin" }, { "id", "lp" },
        { "plugin", { { "format", "AU" }, { "uid", lowpass->createIdentifierString().toStdString() }, { "name", "AULowpass" },
                      { "params", { { "Cutoff Frequency", 0.0 } } } } } } });

    model::Patch replace;
    replace.ops.push_back ({ model::PatchOpOp::replace, "/channels", doc["channels"] });
    replace.ops.push_back ({ model::PatchOpOp::replace, "/patterns", doc["patterns"] });
    replace.ops.push_back ({ model::PatchOpOp::replace, "/playlist", doc["playlist"] });
    replace.ops.push_back ({ model::PatchOpOp::replace, "/mixer", doc["mixer"] });
    session->execute ({ replace });

    LiveEngine live (test::getEngineContext(), *session, test::tempDir() / "recording.json");
    ApiHandler handler (*session, test::getEngineContext(), nullptr, &live, &appleUnits());

    Json response;
    ApiHandler::Client client;
    client.send = [&] (const std::string& m) { response = Json::parse (m); };
    handler.handleMessage (Json { { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "plugins.captureState" },
                                  { "params", { { "insert", "master" }, { "effect", "lp" } } } }.dump(), client);

    REQUIRE (response.contains ("result"));
    const auto state = response["result"]["state"].get<std::string>();
    CHECK (state.starts_with ("plugins/"));
    CHECK (std::filesystem::exists (dir / state));
    CHECK (session->getDocument()["mixer"]["inserts"][0]["effects"][0]["plugin"]["state"] == state);

    // Without the parameter, the saved state alone keeps the filter closed.
    model::Patch dropParams;
    dropParams.ops.push_back ({ model::PatchOpOp::remove, "/mixer/inserts/0/effects/0/plugin/params", {} });
    session->execute ({ dropParams });

    auto compiled = compileProject (test::getEngine(), session->getProject(), { dir, tracktion::Edit::forRendering });
    auto file = test::tempDir() / "plugin-state.wav";
    renderToWav (compiled, session->getProject(), file, {});
    CHECK (analysis::rmsDb (analysis::loadAudio (file), { 0, 0.1, {} }) < -30);
}

TEST_CASE ("GrooveKit's synth is registered without a scan, and plays")
{
    auto triad = findByName (appleUnits(), "Triad");
    REQUIRE (triad);
    CHECK (PluginCatalog::toJson (*triad)["instrument"] == true);
    CHECK (triad->manufacturerName == "GrooveKit");

    // Listed once, and found again if a project names it at another file (a build, the app).
    const auto plugins = appleUnits().getPlugins();
    CHECK (std::count_if (plugins.begin(), plugins.end(), [] (auto& d) { return d.name == "Triad"; }) == 1);
    const auto uid = triad->createIdentifierString();
    const auto moved = uid.upToLastOccurrenceOf ("-", false, false).upToLastOccurrenceOf ("-", true, false) + "1234abcd" + uid.fromLastOccurrenceOf ("-", true, false);
    REQUIRE (moved != uid);
    REQUIRE (appleUnits().find (moved.toStdString()));
    CHECK (appleUnits().find (moved.toStdString())->fileOrIdentifier == triad->fileOrIdentifier);
    CHECK (! appleUnits().find ((moved + "0").toStdString()));

    auto render = [&] (double volume)
    {
        auto doc = Json::parse (std::ifstream (test::repoRoot() / "tests" / "songs" / "beeps" / "song.json"));
        doc["channels"][0]["instrument"] = { { "type", "plugin" },
            { "plugin", { { "format", "VST3" }, { "uid", triad->createIdentifierString().toStdString() }, { "name", "Triad" },
                          { "params", { { "Volume", volume } } } } } };

        auto project = model::parseProject (doc);
        auto compiled = compileProject (test::getEngine(), project, { test::tempDir(), tracktion::Edit::forRendering });
        auto file = test::tempDir() / "triad.wav";
        renderToWav (compiled, project, file, {});
        return analysis::rmsDb (analysis::loadAudio (file), { 0, 0.1, {} });
    };

    // Its volume runs -36 to +6 dB: halfway is -15 dB, 21 dB above the bottom.
    const auto loud = render (0.5), quiet = render (0.0);
    CHECK (loud > -30);
    CHECK (loud - quiet == Catch::Approx (21).margin (0.5));
}

TEST_CASE ("A plugin's preset is chosen by name, with or without its category")
{
    auto quartet = findByName (appleUnits(), "Quartet");
    REQUIRE (quartet);

    auto render = [&] (const std::string& preset)
    {
        auto doc = Json::parse (std::ifstream (test::repoRoot() / "tests" / "songs" / "beeps" / "song.json"));
        doc["channels"][0]["instrument"] = { { "type", "plugin" },
            { "plugin", { { "format", "VST3" }, { "uid", quartet->createIdentifierString().toStdString() }, { "name", "Quartet" }, { "preset", preset } } } };
        auto project = model::parseProject (doc);
        auto compiled = compileProject (test::getEngine(), project, { test::tempDir(), tracktion::Edit::forRendering });
        auto file = test::tempDir() / "quartet.wav";
        renderToWav (compiled, project, file, {});
        return analysis::rmsDb (analysis::loadAudio (file), { 0, 0.1, {} });
    };

    CHECK (render ("Stabs: Rave Stab") > -40);
    CHECK (render ("rave stab") > -40);
    CHECK_THROWS_WITH (render ("Nothing Like It"), Catch::Matchers::ContainsSubstring ("no preset"));
}

TEST_CASE ("Every one of GrooveKit's effects takes a burst through each of its presets, and its editor opens")
{
    std::vector<juce::PluginDescription> effects;

    for (auto& d : appleUnits().getPlugins())
        if (d.manufacturerName == "GrooveKit" && ! d.isInstrument)
            effects.push_back (d);

    REQUIRE (effects.size() >= 2);

    for (auto& effect : effects)
    {
        INFO (effect.name);
        juce::String error;
        auto instance = test::getEngine().getPluginManager().pluginFormatManager.createPluginInstance (effect, 48000, 512, error);
        REQUIRE (instance != nullptr);
        instance->setPlayConfigDetails (2, 2, 48000, 512);
        instance->prepareToPlay (48000, 512);
        REQUIRE (instance->getNumPrograms() > 10);

        for (int p = 0; p < instance->getNumPrograms(); ++p)
        {
            INFO (instance->getProgramName (p));
            instance->setCurrentProgram (p);
            instance->reset();

            // A short burst, then silence: the effect's tail is what comes after.
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            float peak = 0, tail = 0;
            bool finite = true;

            // Five seconds: enough for a repeat a bar long to come round. A frozen preset takes
            // nothing in, so it has no tail to check.
            const bool frozen = instance->getProgramName (p).contains ("Frozen");

            for (int block = 0; block < 480; ++block)
            {
                buffer.clear();

                if (block == 0)
                    for (int i = 0; i < 256; ++i)
                        for (int ch = 0; ch < 2; ++ch)
                            buffer.setSample (ch, i, 0.5f * std::sin (i * 0.3f) * (1 - i / 256.0f));

                instance->processBlock (buffer, midi);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        finite = finite && std::isfinite (buffer.getSample (ch, i));

                peak = std::max (peak, buffer.getMagnitude (0, buffer.getNumSamples()));

                if (block >= 2)
                    tail = std::max (tail, buffer.getMagnitude (0, buffer.getNumSamples()));
                else if (block == 0)
                    tail = std::max (tail, buffer.getMagnitude (300, buffer.getNumSamples() - 300));
            }

            CHECK (finite);
            CHECK (peak > 0.01f);
            CHECK (peak < 4.0f);

            if (! frozen)
                CHECK (tail > 0.0005f);
        }

        std::unique_ptr<juce::AudioProcessorEditor> editor (instance->createEditor());
        REQUIRE (editor != nullptr);
        CHECK (editor->getWidth() > 600);
        editor.reset();
        instance->releaseResources();
    }
}

TEST_CASE ("Every one of GrooveKit's synths plays each of its presets, and its editor opens")
{
    std::vector<juce::PluginDescription> synths;

    for (auto& d : appleUnits().getPlugins())
        if (d.manufacturerName == "GrooveKit" && d.isInstrument)
            synths.push_back (d);

    REQUIRE (synths.size() >= 2);

  for (auto& synth : synths)
  {
    INFO (synth.name);
    juce::String error;
    auto instance = test::getEngine().getPluginManager().pluginFormatManager.createPluginInstance (synth, 48000, 512, error);
    REQUIRE (instance != nullptr);
    instance->setPlayConfigDetails (0, 2, 48000, 512);
    instance->prepareToPlay (48000, 512);
    REQUIRE (instance->getNumPrograms() > 10);

    for (int p = 0; p < instance->getNumPrograms(); ++p)
    {
        INFO (instance->getProgramName (p));
        instance->setCurrentProgram (p);

        // A chord held for half a second, then let go.
        juce::AudioBuffer<float> buffer (2, 512);
        float peak = 0;
        bool finite = true;

        for (int block = 0; block < 60; ++block)
        {
            juce::MidiBuffer midi;

            if (block == 0)
                for (auto note : { 48, 55, 60 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);

            if (block == 47)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);

            buffer.clear();
            instance->processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    finite = finite && std::isfinite (buffer.getSample (ch, i));

            peak = std::max (peak, buffer.getMagnitude (0, buffer.getNumSamples()));
        }

        CHECK (finite);
        CHECK (peak > 0.01f);
        CHECK (peak < 4.0f);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (instance->createEditor());
    REQUIRE (editor != nullptr);
    CHECK (editor->getWidth() > 800);
    editor.reset();
    instance->releaseResources();
  }
}
