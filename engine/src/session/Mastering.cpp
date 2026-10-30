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
    bool limiterIsLast (const Project& project)
    {
        const auto* master = findInsert (project, masterInsertId);

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

namespace
{
    /** What a mastering run keeps between its steps. */
    struct MasterRun
    {
        Project project;
        bool hadLimiter = false, limiterLast = false;
        std::filesystem::path file;
        CompileOptions compileOptions;
        MasterOptions options;
        double gain = 0;
        float lufs = analysis::silenceDb, truePeak = analysis::silenceDb;
        int renders = 0, passes = 0;

        // The fast way: the song once without the limiter, which then runs over it in memory.
        std::optional<CompiledEdit> compiled;       // kept while the offline limiter, made in its Edit, lives
        tracktion::Plugin::Ptr offlineLimiter;
        analysis::Audio dry;

        BuiltinEffect& limiter()    { return *findLimiter (project); }

        /** Tries gains until the loudness is within tolerance of the target, each measured by measureAt. */
        void loop (const std::function<void (double)>& measureAt)
        {
            std::optional<std::pair<double, double>> previous; // (gain, loudness) of the pass before

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
        }
    };
}

std::shared_ptr<Steps> masterToLoudnessSteps (tracktion::Engine& engine, Session& session, const MasterOptions& options)
{
    auto steps = std::make_shared<Steps>();
    auto run = std::make_shared<MasterRun>();
    run->options = options;

    // The song as it is, the limiter added if it's missing, its ceiling and release set; with the
    // limiter last on master the song is compiled once without it (fast way), otherwise every
    // pass renders the song, which compiles each time: that stays here on the message thread.
    steps->here ([run, &engine, &session]
    {
        auto& project = run->project;
        project = session.getProject();
        run->hadLimiter = findLimiter (project) != nullptr;

        if (! run->hadLimiter)
        {
            BuiltinEffect limiter;
            limiter.id = masterLimiterId;
            limiter.kind = BuiltinEffectKind::limiter;

            if (auto* master = findMaster (project))
                master->effects.push_back (limiter);
        }

        auto& limiter = run->limiter();
        limiter.params["ceilingDb"] = run->options.ceilingDb;

        if (run->options.releaseMs)
            limiter.params["releaseMs"] = *run->options.releaseMs;

        const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
        run->file = projectDir / "renders" / "mastering.wav";
        std::filesystem::create_directories (run->file.parent_path());
        run->compileOptions = { projectDir, tracktion::Edit::forRendering };
        run->gain = limiter.params.contains ("inputGainDb") ? limiter.params["inputGainDb"] : 0.0;
        run->limiterLast = limiterIsLast (project);

        if (run->limiterLast)
        {
            auto without = project;
            findMaster (without)->effects.pop_back();
            run->compiled.emplace (compileProject (engine, without, run->compileOptions));
            run->offlineLimiter = effects::createEffect (*run->compiled->edit, limiter);
            return;
        }

        run->loop ([&] (double inputGain)
        {
            run->limiter().params["inputGainDb"] = inputGain;
            auto edit = compileProject (engine, project, run->compileOptions);
            renderToWav (edit, project, run->file, run->options.render);
            ++run->renders;

            auto audio = analysis::loadAudio (run->file);
            run->lufs = analysis::integratedLufs (audio);
            run->truePeak = analysis::truePeakDb (audio);
        });
    });

    steps->pooled ([run]
    {
        if (! run->limiterLast)
            return;

        auto without = run->project;
        findMaster (without)->effects.pop_back();
        renderToWav (*run->compiled, without, run->file, run->options.render);
        ++run->renders;
        run->dry = analysis::loadAudio (run->file);

        run->loop ([&] (double inputGain)
        {
            auto& limiter = run->limiter();
            limiter.params["inputGainDb"] = inputGain;
            effects::applyParams (*run->offlineLimiter, limiter);
            const auto pass = runLimiter (*run->offlineLimiter, run->dry, run->options.render.blockSize);
            run->lufs = pass.lufs;
            run->truePeak = pass.truePeak;
        });
    });

    auto* s = steps.get();

    steps->here ([run, s, &session]
    {
        run->offlineLimiter = nullptr;
        run->compiled.reset();
        std::filesystem::remove (run->file);

        const auto& options = run->options;
        auto params = Json { { "inputGainDb", run->gain }, { "ceilingDb", options.ceilingDb } };

        if (options.releaseMs)
            params["releaseMs"] = *options.releaseMs;

        Command command;

        if (run->hadLimiter)
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

        s->result = {
            { "inputGainDb", round (run->gain) },
            { "integratedLufs", round (run->lufs) },
            { "truePeakDb", round (run->truePeak) },
            { "targetLufs", options.targetLufs },
            { "reachedTarget", std::abs (options.targetLufs - run->lufs) <= tolerance },
            { "renders", run->renders },
            { "passes", run->passes },
        };
    });

    return steps;
}

Json masterToLoudness (tracktion::Engine& engine, Session& session, const MasterOptions& options)
{
    auto steps = masterToLoudnessSteps (engine, session, options);
    steps->runNow();
    return steps->result;
}

} // namespace gk
