#include "MasterMatch.h"

#include "Mastering.h"
#include "compiler/EditCompiler.h"
#include "render/Encode.h"

#include <fstream>

namespace gk
{

using namespace model;
using analysis::MasterProfile;

namespace
{
    constexpr int maxRenders = 5;           // the song's stretch with the chain, at most
    constexpr Ticks barTicks = 3840;

    /** The chain's EQ: a low shelf, five peaks and a high shelf, each covering a stretch of the
        third octaves it's set from. */
    struct EqBand { int type; double hz, q, from, to; };
    constexpr EqBand eqBands[] = { { 2, 80, 0.71, 0, 100 }, { 3, 150, 1.0, 100, 250 }, { 3, 400, 1.0, 250, 700 }, { 3, 1200, 1.0, 700, 2000 },
                                   { 3, 3000, 1.0, 2000, 5000 }, { 3, 7000, 1.0, 5000, 9000 }, { 5, 10000, 0.71, 9000, 30000 } };
    constexpr size_t numEq = std::size (eqBands);

    struct Chain
    {
        std::array<double, numEq> eq {};
        std::array<double, MasterProfile::numBands> threshold {}, ratio {}, width {};
        double monoBelow = 0;
        double limiterGain = 0, ceiling = -1;

        Chain()
        {
            ratio.fill (1);
            width.fill (100);
            threshold.fill (-18);
        }
    };

    /** The mean of reference minus song over the third octaves a band covers. */
    double toneGap (const MasterProfile& song, const MasterProfile& reference, double from, double to)
    {
        double sum = 0;
        int count = 0;

        for (size_t k = 0; k < song.tone.size() && k < reference.tone.size(); ++k)
            if (MasterProfile::toneHz (k) >= from && MasterProfile::toneHz (k) < to && MasterProfile::toneHz (k) >= 30)
                sum += reference.tone[k] - song.tone[k], ++count;

        return count > 0 ? sum / count : 0.0;
    }

    Insert& masterOf (Project& project)
    {
        for (auto& insert : project.mixer.inserts)
            if (insert.id == masterInsertId)
                return insert;

        throw std::runtime_error ("The song has no master insert");
    }

    bool isChainEffect (const Effect& e, bool withLimiter)
    {
        const auto& id = std::visit ([] (auto& x) -> const Id& { return x.id; }, e);
        return id == matchEqId || id == matchDynamicsId || id == matchStereoId || (withLimiter && id == masterLimiterId);
    }

    /** The chain's effects, in order, the limiter last. */
    std::vector<BuiltinEffect> effectsOf (const Chain& c)
    {
        std::vector<BuiltinEffect> out;

        BuiltinEffect eq;
        eq.id = matchEqId;
        eq.kind = BuiltinEffectKind::eq;

        for (size_t b = 0; b < numEq; ++b)
        {
            const auto n = "band" + std::to_string (b + 1);
            eq.params[n + "Type"] = eqBands[b].type;
            eq.params[n + "Freq"] = eqBands[b].hz;
            eq.params[n + "Q"] = eqBands[b].q;
            eq.params[n + "GainDb"] = std::round (c.eq[b] * 10) / 10;
        }

        out.push_back (eq);

        BuiltinEffect dynamics;
        dynamics.id = matchDynamicsId;
        dynamics.kind = BuiltinEffectKind::multiband;
        dynamics.params = { { "bands", 1 }, { "lowFreq", MasterProfile::crossovers[0] }, { "midFreq", MasterProfile::crossovers[1] },
                            { "highFreq", MasterProfile::crossovers[2] }, { "detect", 1 }, { "kneeDb", 6 } };

        for (size_t b = 0; b < MasterProfile::numBands; ++b)
        {
            const auto n = "band" + std::to_string (b + 1);
            dynamics.params[n + "ThresholdDb"] = std::round (c.threshold[b] * 10) / 10;
            dynamics.params[n + "Ratio"] = std::round (c.ratio[b] * 100) / 100;
            dynamics.params[n + "AttackMs"] = b == 0 ? 30 : 10;
            dynamics.params[n + "ReleaseMs"] = b == 0 ? 200 : 150;
            dynamics.params[n + "GainDb"] = 0;
            dynamics.params[n + "Width"] = std::round (c.width[b]);
        }

        out.push_back (dynamics);

        if (c.monoBelow > 0)
        {
            BuiltinEffect stereo;
            stereo.id = matchStereoId;
            stereo.kind = BuiltinEffectKind::stereo;
            stereo.params = { { "monoBelow", std::round (c.monoBelow) } };
            out.push_back (stereo);
        }

        BuiltinEffect limiter;
        limiter.id = masterLimiterId;
        limiter.kind = BuiltinEffectKind::limiter;
        limiter.params = { { "inputGainDb", std::round (c.limiterGain * 100) / 100 }, { "ceilingDb", c.ceiling } };
        out.push_back (limiter);

        return out;
    }

    /** Where the chain goes on master: where the limiter was, else at the end. */
    size_t chainIndex (const Insert& master)
    {
        size_t index = 0;

        for (auto& e : master.effects)
        {
            if (std::visit ([] (auto& x) { return x.id == masterLimiterId; }, e))
                return index;

            if (! isChainEffect (e, false))
                ++index;
        }

        return index;
    }

    Project withChain (const Project& song, const Chain& c)
    {
        auto project = song;
        auto& master = masterOf (project);
        const auto at = chainIndex (master);
        std::erase_if (master.effects, [] (auto& e) { return isChainEffect (e, true); });

        auto effects = effectsOf (c);
        master.effects.insert (master.effects.begin() + static_cast<long> (std::min (at, master.effects.size())), effects.begin(), effects.end());
        return project;
    }

    analysis::Audio levelled (const analysis::Audio& audio, float lufs, float target)
    {
        auto out = audio;
        out.buffer.applyGain (juce::Decibels::decibelsToGain (target - lufs));
        return out;
    }

    float round1 (double v)
    {
        return static_cast<float> (std::round (v * 10) / 10);
    }

    struct MatchRun
    {
        MatchMasterOptions options;
        Project song;                       // as it was, the chain's effects taken out
        CompileOptions compileOptions;
        std::filesystem::path folder;
        std::optional<CompiledEdit> compiled;

        std::vector<MasterProfile> references;
        MasterProfile reference, before, after;
        analysis::Audio beforeAudio, afterAudio, referenceAudio;
        Chain chain;
        std::optional<std::pair<double, double>> previous;     // (limiter gain, loudness) of the render before
        int renders = 0;
        bool done = false;
        Json balance = Json::array(), passes = Json::array();

        bool bassMissing = false;           // the reference has a bass part the song hasn't: the EQ won't chase it

        double target() const { return options.targetLufs.value_or (reference.lufs); }

        double eqLimit (size_t band) const
        {
            return bassMissing && eqBands[band].to <= 250 ? std::min (2.0, options.maxEqDb) : options.maxEqDb;
        }

        void renderTo (const Project& project, const std::filesystem::path& file, const RenderOptions& range)
        {
            renderToWav (*compiled, project, file, range);
        }

        /** The chain from how the song differs: the EQ, the bands' compression and width, mono bass. */
        void startChain()
        {
            chain.ceiling = options.ceilingDb;

            for (size_t b = 0; b < numEq; ++b)
                chain.eq[b] = std::clamp (0.8 * toneGap (before, reference, eqBands[b].from, eqBands[b].to), -eqLimit (b), eqLimit (b));

            for (size_t b = 0; b < MasterProfile::numBands; ++b)
            {
                // More movement than the reference's: compress it, from between its typical level and its peaks.
                const auto over = before.punch[b] - reference.punch[b];
                chain.threshold[b] = std::clamp (static_cast<double> (before.level[b]) + 0.35 * before.punch[b], -60.0, 0.0);
                chain.ratio[b] = over > 1 ? std::clamp (1 + over / 3.0, 1.0, 6.0) : 1.0;

                if (std::max (before.width[b], reference.width[b]) > -40)
                    chain.width[b] = std::clamp (100 * std::pow (10.0, (reference.width[b] - before.width[b]) / 20.0), 50.0, 150.0);
            }

            if (reference.monoBelowHz > 0 && (before.monoBelowHz == 0 || before.monoBelowHz < reference.monoBelowHz * 0.7f))
                chain.monoBelow = std::clamp (static_cast<double> (reference.monoBelowHz), 40.0, 300.0);

            chain.limiterGain = std::clamp (target() - before.lufs, -12.0, 24.0);
        }

        /** One step closer from what the last render measured; done when it's close. */
        void adjust()
        {
            const auto lufsGap = target() - after.lufs;
            double worstTone = 0, worstPunch = 0;

            for (size_t b = 0; b < numEq; ++b)
            {
                const auto gap = toneGap (after, reference, eqBands[b].from, eqBands[b].to);
                const auto limit = eqLimit (b);
                chain.eq[b] = std::clamp (chain.eq[b] + 0.8 * gap, -limit, limit);

                // A band held at its limit is as close as it gets.
                if (std::abs (chain.eq[b]) < limit - 0.05)
                    worstTone = std::max (worstTone, std::abs (gap));
            }

            for (size_t b = 0; b < MasterProfile::numBands; ++b)
            {
                const auto over = after.punch[b] - reference.punch[b];
                worstPunch = std::max (worstPunch, static_cast<double> (std::abs (over)));

                if (over > 1 || (over < -1 && chain.ratio[b] > 1))
                    chain.ratio[b] = std::clamp (chain.ratio[b] * (1 + over / 6.0), 1.0, 10.0);

                if (std::max (after.width[b], reference.width[b]) > -40)
                    chain.width[b] = std::clamp (chain.width[b] * std::pow (10.0, (reference.width[b] - after.width[b]) / 20.0), 50.0, 150.0);
            }

            // Once the limiter works hard, a dB in gives less than a dB out: step by the slope
            // between the last two renders.
            auto step = lufsGap;

            if (previous && std::abs (chain.limiterGain - previous->first) > 1e-6)
            {
                const auto slope = (after.lufs - previous->second) / (chain.limiterGain - previous->first);

                if (slope > 0.05)
                    step = lufsGap / slope;
            }

            previous = std::make_pair (chain.limiterGain, static_cast<double> (after.lufs));

            // Close enough: the loudness within 0.3 LU, the tone within a dB, the punch within 1.5 dB
            // (or as close as the capped EQ and the compressors get it).
            const auto close = std::abs (lufsGap) <= 0.3 && worstTone <= 1.0 && worstPunch <= 1.5;
            passes.push_back ({ { "lufs", after.lufs }, { "toneDb", round1 (worstTone) }, { "punchDb", round1 (worstPunch) } });

            if (close)
            {
                done = true;
                return;
            }

            chain.limiterGain = std::clamp (chain.limiterGain + std::clamp (step, -12.0, 12.0), -36.0, 36.0);
        }
    };

    MasterProfile referenceProfile (const MasterReference& r, const std::filesystem::path& cacheFolder, analysis::Audio* audio)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size (r.file, error);
        const auto key = r.file.string() + "@" + std::to_string (size) + "@" + std::to_string (r.from) + "-" + std::to_string (r.to) + "@1";
        const auto cached = cacheFolder / "profiles" / (juce::String::toHexString (static_cast<juce::int64> (std::hash<std::string> {} (key))).toStdString() + ".json");

        if (audio == nullptr)
        {
            try
            {
                if (std::ifstream in (cached); in)
                    return MasterProfile::fromJson (Json::parse (in));
            }
            catch (const std::exception&)
            {
                // Measured again.
            }
        }

        auto whole = analysis::loadAudio (r.file, r.to > 0 ? std::optional<double> (r.to) : std::nullopt);
        auto drop = analysis::slice (whole, { r.from, r.to > r.from ? std::optional<double> (r.to) : std::nullopt, {} });
        const auto profile = analysis::measureMaster (drop);

        if (audio != nullptr)
            *audio = std::move (drop);

        try
        {
            std::filesystem::create_directories (cached.parent_path());
            std::ofstream (cached, std::ios::trunc) << profile.toJson().dump();
        }
        catch (const std::exception&)
        {
            // Measured again next time.
        }

        return profile;
    }
}

std::optional<std::pair<double, double>> dropOf (const std::vector<std::string>& roles, const std::vector<std::pair<int, int>>& sections,
                                                 const std::vector<float>& barLoudness, double downbeat, double barSeconds)
{
    if (barSeconds <= 0 || barLoudness.empty())
        return {};

    const auto bars = static_cast<int> (barLoudness.size());

    auto loudness = [&] (int from, int to)
    {
        double sum = 0;

        for (int b = std::max (0, from); b < std::min (to, bars); ++b)
            sum += barLoudness[static_cast<size_t> (b)];

        return sum / std::max (1, std::min (to, bars) - std::max (0, from));
    };

    std::optional<std::pair<int, int>> best;

    for (size_t s = 0; s < sections.size() && s < roles.size(); ++s)
        if (roles[s] == "drop" && (! best || loudness (sections[s].first, sections[s].second) > loudness (best->first, best->second)))
            best = sections[s];

    if (! best)
    {
        for (int b = 0; b + std::min (16, bars) <= bars; ++b)
            if (! best || loudness (b, b + 16) > loudness (best->first, best->second))
                best = std::make_pair (b, std::min (b + 16, bars));
    }

    if (! best)
        return {};

    const auto to = std::min (best->second, best->first + 16);
    return std::make_pair (downbeat + best->first * barSeconds, downbeat + to * barSeconds);
}

std::shared_ptr<Steps> matchMasterSteps (tracktion::Engine& engine, Session& session, MatchMasterOptions options)
{
    if (options.references.empty())
        throw std::runtime_error ("No reference to master like");

    auto steps = std::make_shared<Steps>();
    auto run = std::make_shared<MatchRun>();
    run->options = std::move (options);

    // The song without the chain, compiled.
    steps->here ([run, &engine, &session]
    {
        run->song = session.getProject();
        masterOf (run->song);   // throws without one
        std::erase_if (masterOf (run->song).effects, [] (auto& e) { return isChainEffect (e, true); });

        const auto projectDir = session.getProjectDir().value_or (std::filesystem::temp_directory_path() / "groovekit");
        run->folder = projectDir / "renders" / "master-match";
        std::filesystem::create_directories (run->folder);
        run->compileOptions = { projectDir, tracktion::Edit::forRendering };
        run->compiled.emplace (compileProject (engine, run->song, run->compileOptions));
    });

    // The song's stretch (its loudest sixteen bars unless given), the references, and the parts'
    // balance against the first one's.
    steps->pooled ([run]
    {
        auto& options = run->options;
        auto range = options.render;
        const auto file = run->folder / "before.wav";

        if (! range.start || ! range.end)
        {
            run->renderTo (run->song, file, range);
            const auto whole = analysis::loadAudio (file);
            const auto barSeconds = 4 * 60 / run->song.tempo;
            std::vector<float> bars;

            for (double t = 0; t + barSeconds <= whole.getDurationSeconds() + 0.01; t += barSeconds)
                bars.push_back (analysis::rmsDb (whole, { t, t + barSeconds, {} }));

            if (bars.empty())
                throw std::runtime_error ("The song is shorter than a bar: nothing to master");

            const auto drop = dropOf ({}, {}, bars, 0, barSeconds);
            range.start = static_cast<Ticks> (std::lround (drop->first / barSeconds)) * barTicks;
            range.end = static_cast<Ticks> (std::lround (drop->second / barSeconds)) * barTicks;
        }

        range.sampleRate = 44100;
        options.render = range;
        run->renderTo (run->song, file, range);
        run->beforeAudio = analysis::loadAudio (file);
        run->before = analysis::measureMaster (run->beforeAudio);

        if (run->before.lufs <= analysis::silenceDb + 1)
            throw std::runtime_error ("The song is silent there; nothing to master");

        for (size_t i = 0; i < options.references.size(); ++i)
            run->references.push_back (referenceProfile (options.references[i], options.cacheFolder, i == 0 ? &run->referenceAudio : nullptr));

        run->reference = run->references.size() == 1 ? run->references[0] : MasterProfile::average (run->references);

        // What mastering can't fix: a part louder or quieter against the rest than in the reference.
        if (options.partLevels)
        {
            const auto song = options.partLevels (run->beforeAudio.buffer, run->beforeAudio.sampleRate);
            const auto theirs = options.partLevels (run->referenceAudio.buffer, run->referenceAudio.sampleRate);

            if (song && theirs)
            {
                for (auto& [part, level] : *song)
                {
                    const auto it = theirs->find (part);

                    if (it == theirs->end())
                        continue;

                    const auto gap = level - it->second;
                    Json j { { "part", part }, { "song", round1 (level) }, { "reference", round1 (it->second) } };

                    if (level < -35 && it->second > -25)
                    {
                        j["advice"] = "the song has hardly any " + part + " where the reference's sits " + std::to_string (std::lround (-it->second))
                                    + " dB under the mix: mastering can't add a part, so the EQ leaves that range mostly alone";
                        run->bassMissing = run->bassMissing || part == "bass";
                    }
                    else if (std::abs (gap) >= 2)
                        j["advice"] = "the " + part + " " + juce::String (std::abs (gap), 1).toStdString() + " dB " + (gap > 0 ? "louder" : "quieter")
                                    + " against the mix than in the reference: mastering moves the whole mix, so " + (gap > 0 ? "lower" : "raise")
                                    + " the " + part + " channels in the mix";

                    run->balance.push_back (j);
                }
            }
        }

        run->startChain();
    });

    // Render with the chain, measure, adjust: each try compiled here and rendered on the pool.
    for (int i = 0; i < maxRenders; ++i)
    {
        steps->here ([run, &engine]
        {
            if (! run->done)
                run->compiled.emplace (compileProject (engine, withChain (run->song, run->chain), run->compileOptions));
        });

        steps->pooled ([run, last = i + 1 == maxRenders]
        {
            if (run->done)
                return;

            const auto file = run->folder / "after.wav";
            run->renderTo (withChain (run->song, run->chain), file, run->options.render);
            ++run->renders;
            run->afterAudio = analysis::loadAudio (file);
            run->after = analysis::measureMaster (run->afterAudio);

            if (! last)
                run->adjust();
        });
    }

    auto* s = steps.get();

    // The chain applied as one change, and the stretches to compare at the same loudness.
    steps->here ([run, s, &session]
    {
        run->compiled.reset();

        auto project = session.getProject();
        auto& master = masterOf (project);
        const auto at = chainIndex (master);
        std::vector<Command> commands;

        for (auto& e : master.effects)
            if (isChainEffect (e, true))
                commands.push_back (RemoveEffect { masterInsertId, std::visit ([] (auto& x) { return x.id; }, e) });

        auto index = static_cast<Index> (at);

        for (auto& effect : effectsOf (run->chain))
            commands.push_back (AddEffect { masterInsertId, effect, index++ });

        auto execute = run->options.execute;

        if (! execute.label)
            execute.label = "Master like " + (run->options.references.size() == 1 ? run->options.references[0].title : std::string ("the style's references"));

        session.execute (commands, execute);

        // Before, after and the reference at the loudness of the quietest, so none clips.
        const auto level = std::min ({ run->before.lufs, run->after.lufs, run->references[0].lufs });
        Json files = Json::object();

        auto write = [&] (const char* name, const analysis::Audio& audio, float lufs)
        {
            const auto file = run->folder / (std::string (name) + "-matched.wav");
            writeAudio (levelled (audio, lufs, level).buffer, audio.sampleRate, file, {});
            files[name] = file.string();
        };

        write ("before", run->beforeAudio, run->before.lufs);
        write ("after", run->afterAudio, run->after.lufs);
        write ("reference", run->referenceAudio, run->references[0].lufs);
        std::filesystem::remove (run->folder / "before.wav");
        std::filesystem::remove (run->folder / "after.wav");

        auto remaining = Json::array();

        for (auto& d : analysis::compareMasters (run->after, run->reference))
            if (std::abs (d.size) >= 1.0f)
                remaining.push_back (d.what);

        auto eq = Json::array();

        for (size_t b = 0; b < numEq; ++b)
            if (std::abs (run->chain.eq[b]) >= 0.2)
                eq.push_back ({ { "hz", eqBands[b].hz }, { "gainDb", std::round (run->chain.eq[b] * 10) / 10 },
                                { "type", eqBands[b].type == 3 ? "peak" : eqBands[b].type == 2 ? "lowShelf" : "highShelf" } });

        auto bands = Json::array();

        for (size_t b = 0; b < MasterProfile::numBands; ++b)
            bands.push_back ({ { "band", MasterProfile::bandName (b) }, { "ratio", std::round (run->chain.ratio[b] * 100) / 100 },
                               { "thresholdDb", std::round (run->chain.threshold[b] * 10) / 10 }, { "width", std::round (run->chain.width[b]) } });

        auto titles = Json::array();

        for (auto& r : run->options.references)
            titles.push_back (r.title);

        Json result {
            { "references", titles },
            { "reference", run->reference.toJson() },
            { "before", run->before.toJson() },
            { "after", run->after.toJson() },
            { "targetLufs", round1 (run->target()) },
            { "close", run->done },
            { "remaining", remaining },
            { "chain", { { "eq", eq }, { "bands", bands }, { "monoBelowHz", run->chain.monoBelow },
                         { "limiter", { { "inputGainDb", std::round (run->chain.limiterGain * 100) / 100 }, { "ceilingDb", run->chain.ceiling } } } } },
            { "renders", run->renders },
            { "passes", run->passes },
            { "files", files },
            { "filesLufs", round1 (level) },
            { "start", *run->options.render.start },
            { "end", *run->options.render.end },
        };

        if (! run->balance.empty())
            result["balance"] = run->balance;

        if (run->reference.lufs > -9 && ! run->options.targetLufs)
            result["loudnessNote"] = "The reference is club-loud (" + juce::String (run->reference.lufs, 1).toStdString()
                                     + " LUFS): matched, but a mix not built for it pumps and flattens. Listen to after against before; give targetLufs (-9 to -11) for a gentler master";

        s->result = std::move (result);
    });

    return steps;
}

} // namespace gk
