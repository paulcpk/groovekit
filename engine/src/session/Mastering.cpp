#include "Mastering.h"

#include "analysis/Analysis.h"
#include "compiler/EditCompiler.h"
#include "effects/BuiltinEffects.h"

namespace gk
{

using namespace model;

namespace
{
    constexpr double tolerance = 0.2;
    constexpr int maxPasses = 8;

    Insert* findMaster (Project& project)
    {
        for (auto& insert : project.mixer.inserts)
            if (insert.id == masterInsertId)
                return &insert;

        return nullptr;
    }

    BuiltinEffect* findLimiter (Project& project)
    {
        if (auto* master = findMaster (project))
            for (auto& effect : master->effects)
                if (auto b = std::get_if<BuiltinEffect> (&effect); b != nullptr && b->id == masterLimiterId)
                    return b;

        return nullptr;
    }

    /** Whether the limiter is the last thing the master's sound goes through: the last effect
        on master, fully heard, the fader at unity, no sends and nothing automated there. Then one
        render without it is what the limiter gets at any input gain. */
    bool limiterIsLast (Project& project)
    {
        const auto* master = findMaster (project);

        if (master == nullptr || master->effects.empty() || master->volumeDb != 0 || master->pan != 0 || ! master->sends.empty())
            return false;

        const auto* last = std::get_if<BuiltinEffect> (&master->effects.back());

        if (last == nullptr || last->id != masterLimiterId || last->bypass || last->mix != 1)
            return false;

        for (auto& clip : project.playlist.clips)
            if (auto* ac = std::get_if<AutomationClip> (&clip); ac != nullptr && ac->target.insert == masterInsertId)
                return false;

        return true;
    }

    struct Pass
    {
        float lufs, truePeak;
    };

    /** The limiter run over audio in memory, as it would run on master. Its output comes out its
        lookahead late (which a render takes out again), so that much silence follows the audio
        and the start is dropped. */
    Pass runLimiter (tracktion::Plugin& plugin, const analysis::Audio& dry, int blockSize)
    {
        const auto n = dry.buffer.getNumSamples();
        plugin.baseClassInitialise ({ tracktion::TimePosition(), dry.sampleRate, blockSize });
        const auto latency = static_cast<int> (std::lround (plugin.getLatencySeconds() * dry.sampleRate));

        analysis::Audio out;
        out.sampleRate = dry.sampleRate;
        out.buffer.setSize (2, n + latency);
        out.buffer.clear();

        for (int ch = 0; ch < 2; ++ch)
            out.buffer.copyFrom (ch, 0, dry.buffer, std::min (ch, dry.buffer.getNumChannels() - 1), 0, n);

        for (int start = 0; start < out.buffer.getNumSamples(); start += blockSize)
        {
            tracktion::PluginRenderContext context (&out.buffer, juce::AudioChannelSet::stereo(), start,
                                                    std::min (blockSize, out.buffer.getNumSamples() - start), nullptr, 0.0,
                                                    {}, true, false, true, false);
            plugin.applyToBuffer (context);
        }

        plugin.baseClassDeinitialise();

        const auto aligned = analysis::slice (out, { latency / dry.sampleRate, {}, {} });
        return { analysis::integratedLufs (aligned), analysis::truePeakDb (aligned) };
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

        if (auto* master = findMaster (project))
            master->effects.push_back (limiter);
    }

    auto& limiter = *findLimiter (project);
    limiter.params["ceilingDb"] = options.ceilingDb;

    if (options.releaseMs)
        limiter.params["releaseMs"] = *options.releaseMs;

    const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
    const auto file = projectDir / "renders" / "mastering.wav";
    std::filesystem::create_directories (file.parent_path());
    const CompileOptions compileOptions { projectDir, tracktion::Edit::forRendering };

    auto gain = limiter.params.contains ("inputGainDb") ? limiter.params["inputGainDb"] : 0.0;
    float lufs = analysis::silenceDb, truePeak = analysis::silenceDb;
    std::optional<std::pair<double, double>> previous; // (gain, loudness) of the pass before
    int renders = 0, passes = 0;

    // With the limiter last on master, the song is rendered once without it and the limiter
    // alone runs over that in memory for every gain tried; otherwise every pass renders the song.
    std::optional<CompiledEdit> compiled;       // kept while the offline limiter, made in its Edit, lives
    tracktion::Plugin::Ptr offlineLimiter;
    analysis::Audio dry;
    std::function<void (double)> measureAt;

    if (limiterIsLast (project))
    {
        auto without = project;
        findMaster (without)->effects.pop_back();
        compiled.emplace (compileProject (engine, without, compileOptions));
        renderToWav (*compiled, without, file, options.render);
        ++renders;
        dry = analysis::loadAudio (file);
        offlineLimiter = effects::createEffect (*compiled->edit, limiter);

        measureAt = [&] (double inputGain)
        {
            limiter.params["inputGainDb"] = inputGain;
            effects::applyParams (*offlineLimiter, limiter);
            const auto pass = runLimiter (*offlineLimiter, dry, options.render.blockSize);
            lufs = pass.lufs;
            truePeak = pass.truePeak;
        };
    }
    else
    {
        measureAt = [&] (double inputGain)
        {
            limiter.params["inputGainDb"] = inputGain;
            auto edit = compileProject (engine, project, compileOptions);
            renderToWav (edit, project, file, options.render);
            ++renders;

            auto audio = analysis::loadAudio (file);
            lufs = analysis::integratedLufs (audio);
            truePeak = analysis::truePeakDb (audio);
        };
    }

    while (passes < maxPasses)
    {
        measureAt (gain);
        ++passes;

        if (lufs <= analysis::silenceDb)
            throw std::runtime_error ("The song is silent; nothing to master");

        const auto difference = options.targetLufs - lufs;

        if (std::abs (difference) <= tolerance)
            break;

        // Once the limiter works hard, each dB of gain adds less than a dB of loudness: step by
        // the slope measured between the last two passes (secant), not by the raw difference.
        auto step = difference;

        if (previous && std::abs (gain - previous->first) > 1.0e-6)
        {
            const auto slope = (lufs - previous->second) / (gain - previous->first);

            if (slope > 0.05)
                step = difference / slope;
        }

        previous = std::make_pair (gain, static_cast<double> (lufs));
        const auto next = std::clamp (gain + std::clamp (step, -12.0, 12.0), -36.0, 36.0);

        if (std::abs (next - gain) < 1.0e-3)
            break; // at the end of the range

        gain = next;
    }

    offlineLimiter = nullptr;
    compiled.reset();
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
        { "passes", passes },
    };
}

} // namespace gk
