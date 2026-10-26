#include "Mastering.h"

#include "analysis/Analysis.h"
#include "compiler/EditCompiler.h"

namespace gk
{

using namespace model;

namespace
{
    constexpr double tolerance = 0.2;
    constexpr int maxRenders = 6;

    BuiltinEffect* findLimiter (Project& project)
    {
        for (auto& insert : project.mixer.inserts)
            if (insert.id == masterInsertId)
                for (auto& effect : insert.effects)
                    if (auto b = std::get_if<BuiltinEffect> (&effect); b != nullptr && b->id == masterLimiterId)
                        return b;

        return nullptr;
    }
}

Json masterToLoudness (tracktion::Engine& engine, Session& session, const MasterOptions& options)
{
    auto project = session.getProject();
    const bool hadLimiter = findLimiter (project) != nullptr;

    if (! hadLimiter)
    {
        BuiltinEffect limiter;
        limiter.id = masterLimiterId;
        limiter.kind = BuiltinEffectKind::limiter;

        for (auto& insert : project.mixer.inserts)
            if (insert.id == masterInsertId)
                insert.effects.push_back (limiter);
    }

    auto& limiter = *findLimiter (project);
    limiter.params["ceilingDb"] = options.ceilingDb;

    if (options.releaseMs)
        limiter.params["releaseMs"] = *options.releaseMs;

    const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
    const auto file = projectDir / "renders" / "mastering.wav";
    std::filesystem::create_directories (file.parent_path());

    auto gain = limiter.params.contains ("inputGainDb") ? limiter.params["inputGainDb"] : 0.0;
    float lufs = analysis::silenceDb, truePeak = analysis::silenceDb;
    int renders = 0;

    while (renders < maxRenders)
    {
        limiter.params["inputGainDb"] = gain;

        auto compiled = compileProject (engine, project, { projectDir, tracktion::Edit::forRendering });
        renderToWav (compiled, project, file, options.render);
        ++renders;

        auto audio = analysis::loadAudio (file);
        lufs = analysis::integratedLufs (audio);
        truePeak = analysis::truePeakDb (audio);

        if (lufs <= analysis::silenceDb)
            throw std::runtime_error ("The song is silent; nothing to master");

        const auto difference = options.targetLufs - lufs;

        if (std::abs (difference) <= tolerance)
            break;

        const auto next = std::clamp (gain + difference, -24.0, 24.0);

        if (next == gain)
            break; // at the end of the range

        gain = next;
    }

    std::filesystem::remove (file);

    auto params = Json { { "inputGainDb", gain }, { "ceilingDb", options.ceilingDb } };

    if (options.releaseMs)
        params["releaseMs"] = *options.releaseMs;

    Command command;

    if (hadLimiter)
    {
        command = UpdateEffect { masterInsertId, masterLimiterId, Json { { "params", params } } };
    }
    else
    {
        BuiltinEffect effect;
        effect.id = masterLimiterId;
        effect.kind = BuiltinEffectKind::limiter;

        for (auto& [key, value] : params.items())
            effect.params[key] = value.get<double>();

        command = AddEffect { masterInsertId, effect, std::nullopt };
    }

    auto execute = options.execute;

    if (! execute.label)
        execute.label = "Master to " + juce::String (options.targetLufs, 1).toStdString() + " LUFS";

    session.execute ({ command }, execute);

    auto round = [] (double v) { return std::round (v * 100.0) / 100.0; };

    return {
        { "inputGainDb", round (gain) },
        { "integratedLufs", round (lufs) },
        { "truePeakDb", round (truePeak) },
        { "targetLufs", options.targetLufs },
        { "reachedTarget", std::abs (options.targetLufs - lufs) <= tolerance },
        { "renders", renders },
    };
}

} // namespace gk
