#include "MixAnalysis.h"

#include "Analysis.h"
#include "mixer/Slots.h"
#include "render/Encode.h"

namespace gk::analysis
{

using namespace model;

namespace
{
    constexpr float maskingShare = 0.25f;

    /** The channels heard on their own: every unmuted one (audio channels carry the audio clips). */
    std::vector<Id> findSources (const Project& project)
    {
        std::vector<Id> sources;

        for (auto& c : project.channels)
            if (! c.mute)
                sources.push_back (c.id);

        return sources;
    }

    /** The channels with something that can sound before a point: a heard note or audio clip
        starting before it. What starts earlier can still ring into the range (one-shots play
        out, releases tail), so only channels whose first sound comes later are ruled out. */
    std::set<Id> channelsSoundingBefore (const Project& project, Ticks end)
    {
        std::set<Id> sounding;

        for (auto& clip : project.playlist.clips)
        {
            const auto start = getClipStart (clip);

            if (start >= end || ! isClipAudible (project, clip))
                continue;

            const auto clipEnd = std::min (end, start + getClipLength (clip));

            if (auto* audio = std::get_if<AudioClip> (&clip))
            {
                sounding.insert (audio->channel);
            }
            else if (auto* pc = std::get_if<PatternClip> (&clip))
            {
                const auto* pattern = findPattern (project, pc->pattern);

                if (pattern == nullptr)
                    continue;

                // The pattern repeats from the clip's start (less its offset) until the clip ends.
                const auto length = std::max<TickLength> (1, getPatternLength (*pattern, project.timeSignature));

                for (auto round = start - pc->offset; round < clipEnd; round += length)
                    for (auto& note : pattern->notes)
                    {
                        const auto at = round + note.start;

                        if (at >= start && at < clipEnd)
                            sounding.insert (note.channel);
                    }
            }
        }

        return sounding;
    }

    std::string bandName (const SpectrumBand& b)
    {
        auto hz = [] (double f) { return f >= 1000 ? juce::String (f / 1000.0, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "k"
                                                   : juce::String (static_cast<int> (f)); };
        return (hz (b.fromHz) + "-" + hz (b.toHz) + " Hz").toStdString();
    }

    double toPower (float db)
    {
        return db <= silenceDb ? 0.0 : std::pow (10.0, db / 10.0);
    }

    double round (double v, double step)
    {
        const auto k = std::round (1.0 / step);
        return std::round (v * k) / k;
    }
}

namespace
{
    struct Stem
    {
        std::string id;
        std::vector<SpectrumBand> bands;
        float lufs = silenceDb, peak = silenceDb;
    };

    struct Renders
    {
        Audio mix;
        Measurements mixMeasured;      // measured once, for every report
        std::vector<SpectrumBand> mixBands;
        std::vector<Stem> stems;       // the sources that play in the range
    };

    Audio renderAudio (const CompiledEdit& compiled, const Project& p, const RenderOptions& renderOptions, const std::filesystem::path& file)
    {
        renderToWav (compiled, p, file, renderOptions);
        auto audio = loadAudio (file);
        std::filesystem::remove (file);
        return audio;
    }

    /** The renders of a mix analysis as steps: the Edit compiled and each stem's mutes set on the
        message thread, the renders and measurements pooled (Steps). The mix and every stem come
        from one Edit, so instruments and effects are made once: a stem is the channel's tracks
        alone unmuted (every channel solo cleared), as compiling the song with every other channel
        muted would set them. */
    struct StemRun
    {
        Project project;
        CompileOptions compileOptions;
        RenderOptions renderOptions;
        std::filesystem::path dir;       // of its own, so analyses running at once don't share files
        std::optional<CompiledEdit> compiled;
        Renders r;
        int index = 0;

        ~StemRun()
        {
            std::error_code e;
            std::filesystem::remove_all (dir, e);
        }
    };

    std::shared_ptr<StemRun> addStemSteps (Steps& steps, tracktion::Engine& engine, const Project& project, const CompileOptions& compileOptions,
                                           const RenderOptions& renderOptions, const std::filesystem::path& workDir)
    {
        auto run = std::make_shared<StemRun>();
        run->project = project;
        run->compileOptions = compileOptions;
        run->renderOptions = renderOptions;
        run->dir = workDir / juce::Uuid().toDashedString().toStdString();

        steps.here ([run, &engine]
        {
            std::filesystem::create_directories (run->dir);
            run->compiled.emplace (compileProject (engine, run->project, run->compileOptions));
        });

        steps.pooled ([run]
        {
            run->r.mix = renderAudio (*run->compiled, run->project, run->renderOptions, run->dir / "mix.wav");
            run->r.mixMeasured = measure (run->r.mix);
            run->r.mixBands = run->r.mixMeasured.bands.energies;
        });

        // Channels with nothing before the range's end (plus the tail) can't be heard in it.
        const auto rangeEnd = renderOptions.end.value_or (getSongEnd (project))
                              + static_cast<Ticks> (std::ceil (renderOptions.tailSeconds * project.tempo / 60.0 * ticksPerQuarter));
        const auto sounding = channelsSoundingBefore (project, rangeEnd);

        for (auto& source : findSources (project))
        {
            if (! sounding.contains (source))
                continue;

            steps.here ([run, source]
            {
                auto& compiled = *run->compiled;

                auto tracksOf = [&] (const Id& channel)
                {
                    std::vector<tracktion::AudioTrack*> tracks;

                    if (auto it = compiled.channelTracks.find (channel); it != compiled.channelTracks.end())
                        tracks.push_back (it->second);

                    if (auto it = compiled.audioChannelTracks.find (channel); it != compiled.audioChannelTracks.end())
                        tracks.insert (tracks.end(), it->second.begin(), it->second.end());

                    return tracks;
                };

                for (auto& c : run->project.channels)
                    for (auto* track : tracksOf (c.id))
                        track->setMute (c.id != source);
            });

            steps.pooled ([run, source]
            {
                auto audio = renderAudio (*run->compiled, run->project, run->renderOptions, run->dir / ("stem-" + std::to_string (run->index++) + ".wav"));
                const auto peak = peakDb (audio);

                if (peak < -90)
                    return; // the source doesn't play in this range

                run->r.stems.push_back ({ source, bandEnergies (audio), integratedLufs (audio), peak });
            });
        }

        // The Edit goes once the renders are done (on the message thread, as Edits must).
        steps.here ([run] { run->compiled.reset(); });
        return run;
    }

    /** A stem's share of the mix's energy in each band, 0 to 1. */
    std::vector<double> sharesOf (const Stem& stem, const std::vector<SpectrumBand>& mixBands)
    {
        std::vector<double> share;

        for (size_t b = 0; b < mixBands.size(); ++b)
        {
            const auto mixPower = toPower (mixBands[b].db);
            share.push_back (mixPower > 0 ? std::min (1.0, toPower (stem.bands[b].db) / mixPower) : 0.0);
        }

        return share;
    }

    std::string percent (double share)
    {
        return std::to_string (static_cast<int> (std::round (share * 100))) + " %";
    }

    std::string joinNames (const std::vector<std::string>& names)
    {
        std::string s;

        for (size_t i = 0; i < names.size(); ++i)
            s += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];

        return s;
    }
}

namespace
{
Json analyseMixReport (const Renders& r)
{
    std::vector<std::vector<double>> shares;
    auto stemJson = Json::array();

    for (auto& stem : r.stems)
    {
        shares.push_back (sharesOf (stem, r.mixBands));
        auto share = Json::array();

        for (auto v : shares.back())
            share.push_back (round (v, 0.01));

        stemJson.push_back ({
            { "source", stem.id },
            { "integratedLufs", round (stem.lufs, 0.1) },
            { "peakDb", round (stem.peak, 0.1) },
            { "bandShare", share },
        });
    }

    // Bands that matter: within 30 dB of the mix's strongest band.
    float strongest = silenceDb;

    for (auto& b : r.mixBands)
        strongest = std::max (strongest, b.db);

    auto masking = Json::array();

    for (size_t i = 0; i < r.stems.size(); ++i)
    {
        for (size_t j = i + 1; j < r.stems.size(); ++j)
        {
            auto bands = Json::array();

            for (size_t b = 0; b < r.mixBands.size(); ++b)
            {
                if (toPower (r.mixBands[b].db) <= 0 || r.mixBands[b].db < strongest - 30)
                    continue;

                if (shares[i][b] >= maskingShare && shares[j][b] >= maskingShare)
                    bands.push_back (bandName (r.mixBands[b]));
            }

            if (! bands.empty())
                masking.push_back ({ { "a", r.stems[i].id }, { "b", r.stems[j].id }, { "bands", bands } });
        }
    }

    auto bandNames = Json::array();

    for (auto& b : r.mixBands)
        bandNames.push_back (bandName (b));

    return {
        { "mix", analyse (r.mix, r.mixMeasured) },
        { "bands", bandNames },
        { "stems", stemJson },
        { "masking", masking },
    };
}

Json checkMixReport (const Renders& r, const MixCheckContext& context)
{
    const auto bandCount = r.mixBands.size();

    std::vector<std::vector<double>> shares;

    for (auto& stem : r.stems)
        shares.push_back (sharesOf (stem, r.mixBands));

    float strongest = silenceDb;

    for (auto& b : r.mixBands)
        strongest = std::max (strongest, b.db);

    auto audible = [&] (size_t b) { return toPower (r.mixBands[b].db) > 0 && r.mixBands[b].db >= strongest - 30; };

    auto partOf = [&] (const std::string& id) -> std::optional<ChannelPart>
    {
        auto it = context.parts.find (id);
        return it != context.parts.end() ? std::optional (it->second) : std::nullopt;
    };

    auto label = [&] (size_t s)
    {
        const auto part = partOf (r.stems[s].id);
        return r.stems[s].id + (part ? " (" + std::string (toString (*part)) + ")" : "");
    };

    // The stems carrying a band most, for saying where a problem comes from.
    auto carriers = [&] (std::initializer_list<size_t> bands, double atLeast)
    {
        std::vector<std::pair<double, size_t>> list;

        for (size_t s = 0; s < r.stems.size(); ++s)
        {
            double share = 0;

            for (auto b : bands)
                share = std::max (share, shares[s][b]);

            if (share >= atLeast)
                list.emplace_back (share, s);
        }

        std::sort (list.begin(), list.end(), std::greater<>());
        list.resize (std::min<size_t> (list.size(), 3));
        return list;
    };

    auto problems = Json::array();

    auto add = [&] (double severity, const char* kind, std::vector<std::string> channels, std::string what, std::string fix)
    {
        problems.push_back ({ { "severity", round (std::clamp (severity, 0.0, 10.0), 0.1) }, { "kind", kind }, { "channels", channels },
                              { "what", what }, { "fix", fix } });
    };

    // Low end outside the parts that hold it.
    for (size_t s = 0; s < r.stems.size(); ++s)
    {
        const auto part = partOf (r.stems[s].id);

        if (part && mixer::isLowPart (*part))
            continue;

        std::optional<size_t> worst;

        for (size_t b : { size_t { 0 }, size_t { 1 } })
            if (audible (b) && (! worst || shares[s][b] > shares[s][*worst]))
                worst = b;

        if (worst && shares[s][*worst] >= 0.1)
            add (5 + 10 * shares[s][*worst], "lowEnd", { r.stems[s].id },
                 label (s) + " carries " + percent (shares[s][*worst]) + " of the mix's " + bandName (r.mixBands[*worst]) + ", the kick's and bass's room",
                 "raise its slot's low cut to about 150 Hz (mix_slots set), or more if it still muddies the kick");
    }

    // Low parts sharing the low end with nothing ducking one under the other.
    for (size_t i = 0; i < r.stems.size(); ++i)
        for (size_t j = i + 1; j < r.stems.size(); ++j)
        {
            const auto a = partOf (r.stems[i].id), b = partOf (r.stems[j].id);

            if (! a || ! b || ! mixer::isLowPart (*a) || ! mixer::isLowPart (*b))
                continue;

            std::vector<std::string> bands;

            for (size_t band : { size_t { 0 }, size_t { 1 } })
                if (audible (band) && shares[i][band] >= maskingShare && shares[j][band] >= maskingShare)
                    bands.push_back (bandName (r.mixBands[band]));

            if (bands.empty())
                continue;

            const auto aKick = *a == ChannelPart::kick || *a == ChannelPart::drums;
            const auto bKick = *b == ChannelPart::kick || *b == ChannelPart::drums;

            if (aKick != bKick)
            {
                const auto kick = aKick ? i : j, other = aKick ? j : i;

                if (context.ducked.contains (r.stems[other].id))
                    continue;

                add (7.5, "lowShared", { r.stems[kick].id, r.stems[other].id },
                     label (kick) + " and " + label (other) + " both fill " + joinNames (bands) + " and nothing ducks " + r.stems[other].id
                         + ": they blur into one boom and the kick loses its punch",
                     "duck " + r.stems[other].id + " under the kick: a compressor first on its insert keyed by the kick channel (thresholdDb -30, ratio 10, "
                     "attackMs 0.5, releaseMs 150), or move its slot so each has its own band (the kick's body above, the rumble or bass's below)");
            }
            else
            {
                add (6, "lowShared", { r.stems[i].id, r.stems[j].id },
                     label (i) + " and " + label (j) + " both fill " + joinNames (bands) + ": two parts holding the same low end",
                     "keep one there: cut the other's low end (its slot's low cut), or let only one play where both do");
            }
        }

    // Parts covering each other above the low end.
    for (size_t i = 0; i < r.stems.size(); ++i)
        for (size_t j = i + 1; j < r.stems.size(); ++j)
        {
            std::vector<std::string> bands;

            for (size_t b = 2; b < bandCount; ++b)
                if (audible (b) && shares[i][b] >= maskingShare && shares[j][b] >= maskingShare)
                    bands.push_back (bandName (r.mixBands[b]));

            if (bands.empty())
                continue;

            add (3.5 + 0.7 * static_cast<double> (bands.size()), "masking", { r.stems[i].id, r.stems[j].id },
                 label (i) + " and " + label (j) + " both carry " + joinNames (bands) + ": neither is heard clearly there",
                 "give the band to one: cut the other 3-4 dB there with a peak on its slot EQ (bands 2-5), narrow its slot (mix_slots set), "
                 "pan them apart, or let them take turns in the arrangement");
        }

    // The balance against the references', the top above all.
    const auto haveReferences = context.referenceBalance.size() == bandCount;
    std::vector<float> balance;

    for (auto& b : r.mixMeasured.bands.balance)
        balance.push_back (b.db);

    if (haveReferences && balance.size() == bandCount)
    {
        for (size_t b = 0; b < bandCount; ++b)
        {
            const auto diff = static_cast<double> (balance[b] - context.referenceBalance[b]);
            const auto top = b >= 7;
            const auto limit = top ? 3.0 : 4.0;

            if (std::abs (diff) < limit || ! audible (b))
                continue;

            std::vector<std::string> ids, names;

            for (auto& [share, s] : carriers ({ b }, 0.2))
            {
                ids.push_back (r.stems[s].id);
                names.push_back (label (s) + " " + percent (share));
            }

            const auto amount = juce::String (std::abs (diff), 1).toStdString() + " dB " + (diff > 0 ? "more " : "less ") + bandName (r.mixBands[b]);
            const auto from = names.empty() ? std::string() : ": mostly " + joinNames (names);

            if (top && diff > 0)
                add (4 + 0.6 * diff, "brightTop", ids, "the mix has " + amount + " than the references" + from + ". Too many highs make it harsh and tiring",
                     "tame it where it comes from: their slot's top shelf down 2-3 dB (mix_slots set topShelfDb) or the high cut lower, or their level down");
            else if (top)
                add (2.5 + 0.4 * std::abs (diff), "dullTop", ids, "the mix has " + amount + " than the references" + from,
                     "open the hats' or percussion's top (their slot's top shelf back to 0 or the high cut higher), or choose brighter hats");
            else
                add (2.5 + 0.4 * std::abs (diff), diff > 0 ? "tooMuch" : "tooLittle", ids, "the mix has " + amount + " than the references" + from,
                     diff > 0 ? "cut it on the parts that carry it (a wide peak on their slot EQ, or their level)"
                              : "the parts that should hold it are too quiet or cut too far: raise them or open their slot");
        }
    }

    // Highs that never move.
    const auto bars = static_cast<int> (r.mix.getDurationSeconds() / context.barSeconds);

    if (bars >= 8)
    {
        std::vector<double> perBar;

        for (int bar = 0; bar < bars; ++bar)
        {
            auto bands = bandEnergies (slice (r.mix, { bar * context.barSeconds, (bar + 1) * context.barSeconds, {} }));
            perBar.push_back (10 * std::log10 (toPower (bands[8].db) + toPower (bands[9].db) + 1e-20));
        }

        const auto loudest = *std::max_element (perBar.begin(), perBar.end());
        std::vector<double> playing;

        for (auto v : perBar)
            if (v > loudest - 30)
                playing.push_back (v);

        double mean = 0, variance = 0;

        for (auto v : playing)
            mean += v / static_cast<double> (playing.size());

        for (auto v : playing)
            variance += (v - mean) * (v - mean) / static_cast<double> (playing.size());

        const auto topShare = toPower (balance.size() == bandCount ? balance[8] : silenceDb) + toPower (balance.size() == bandCount ? balance[9] : silenceDb);

        if (playing.size() >= 8 && std::sqrt (variance) < 1.0 && topShare > 0.003)
        {
            std::vector<std::string> ids;

            for (auto& [share, s] : carriers ({ 8, 9 }, 0.2))
                ids.push_back (r.stems[s].id);

            add (3, "staticTop", ids, "the highs hold the same level for all " + std::to_string (playing.size()) + " bars (within "
                     + juce::String (std::sqrt (variance), 1).toStdString() + " dB): nothing breathes up there",
                 "move them: automate the hats' or percussion's slot high cut or level over 8-16 bars, open them into a drop, or duck them a little with the kick");
        }
    }

    std::sort (problems.begin(), problems.end(), [] (const Json& a, const Json& b) { return a["severity"].get<double>() > b["severity"].get<double>(); });

    auto channels = Json::array();

    for (size_t s = 0; s < r.stems.size(); ++s)
    {
        auto lives = Json::array();

        for (size_t b = 0; b < bandCount; ++b)
            if (audible (b) && shares[s][b] >= maskingShare)
                lives.push_back (bandName (r.mixBands[b]));

        const auto part = partOf (r.stems[s].id);
        channels.push_back ({ { "channel", r.stems[s].id }, { "part", part ? Json (toString (*part)) : Json() },
                              { "integratedLufs", round (r.stems[s].lufs, 0.1) }, { "peakDb", round (r.stems[s].peak, 0.1) }, { "holds", lives } });
    }

    return {
        { "problems", problems },
        { "channels", channels },
        { "mix", { { "integratedLufs", round (r.mixMeasured.loudness.integrated, 0.1) }, { "truePeakDb", round (r.mixMeasured.truePeakDb, 0.1) } } },
        { "references", haveReferences },
    };
}
} // namespace

std::shared_ptr<Steps> analyseMixSteps (tracktion::Engine& engine, const Project& project, const CompileOptions& compileOptions,
                                        const RenderOptions& renderOptions, const std::filesystem::path& workDir)
{
    auto steps = std::make_shared<Steps>();
    auto run = addStemSteps (*steps, engine, project, compileOptions, renderOptions, workDir);
    auto* s = steps.get();
    steps->pooled ([s, run] { s->result = analyseMixReport (run->r); });
    return steps;
}

Json analyseMix (tracktion::Engine& engine, const Project& project, const CompileOptions& compileOptions,
                 const RenderOptions& renderOptions, const std::filesystem::path& workDir)
{
    auto steps = analyseMixSteps (engine, project, compileOptions, renderOptions, workDir);
    steps->runNow();
    return steps->result;
}

std::shared_ptr<Steps> checkMixSteps (tracktion::Engine& engine, const Project& project, const CompileOptions& compileOptions,
                                      const RenderOptions& renderOptions, const std::filesystem::path& workDir, const MixCheckContext& context)
{
    auto steps = std::make_shared<Steps>();
    auto run = addStemSteps (*steps, engine, project, compileOptions, renderOptions, workDir);
    auto* s = steps.get();
    steps->pooled ([s, run, context] { s->result = checkMixReport (run->r, context); });
    return steps;
}

Json checkMix (tracktion::Engine& engine, const Project& project, const CompileOptions& compileOptions,
               const RenderOptions& renderOptions, const std::filesystem::path& workDir, const MixCheckContext& context)
{
    auto steps = checkMixSteps (engine, project, compileOptions, renderOptions, workDir, context);
    steps->runNow();
    return steps->result;
}

Json checkArrangement (const Project& project, const std::map<std::string, ChannelPart>& parts, int maxParts)
{
    const auto bar = ticksPerBar (project.timeSignature);
    const auto bars = static_cast<int> ((getSongEnd (project) + bar - 1) / bar);
    std::vector<std::set<std::string>> playing (static_cast<size_t> (std::max (0, bars)));

    auto mark = [&] (const std::string& channel, Ticks from, Ticks to)
    {
        if (auto* c = findChannel (project, channel); c == nullptr || c->mute)
            return;

        for (auto b = from / bar; b * bar < to && b < bars; ++b)
            playing[static_cast<size_t> (b)].insert (channel);
    };

    for (auto& clip : project.playlist.clips)
    {
        if (isClipMuted (clip))
            continue;

        if (auto* row = findRow (project, getClipRow (clip)); row != nullptr && row->mute)
            continue;

        const auto start = getClipStart (clip), end = start + getClipLength (clip);

        if (auto* audio = std::get_if<AudioClip> (&clip))
            mark (audio->channel, start, end);
        else if (auto* pc = std::get_if<PatternClip> (&clip))
        {
            const auto* pattern = findPattern (project, pc->pattern);

            if (pattern == nullptr)
                continue;

            // The pattern repeats from the clip's start (less its offset) until the clip ends.
            const auto length = getPatternLength (*pattern, project.timeSignature);

            for (auto round = start - pc->offset; round < end; round += length)
                for (auto& note : pattern->notes)
                {
                    const auto at = round + note.start;

                    if (at >= start && at < end)
                        mark (note.channel, at, at + 1);
                }
        }
    }

    // Beds: effects under most of the song, felt more than heard.
    std::set<std::string> beds;

    for (auto& [channel, part] : parts)
        if (part == ChannelPart::fx && bars > 0)
        {
            const auto heard = std::count_if (playing.begin(), playing.end(), [&] (auto& set) { return set.contains (channel); });

            if (heard >= 0.8 * bars)
                beds.insert (channel);
        }

    std::vector<std::vector<std::string>> counted (playing.size());

    for (size_t b = 0; b < playing.size(); ++b)
        for (auto& c : playing[b])
            if (! beds.contains (c))
                counted[b].push_back (c);

    auto problems = Json::array();
    auto barsJson = Json::array();

    for (auto& c : counted)
        barsJson.push_back (c.size());

    // Runs of bars with too many parts at once.
    for (size_t b = 0; b < counted.size();)
    {
        if (counted[b].size() <= static_cast<size_t> (maxParts))
        {
            ++b;
            continue;
        }

        auto e = b;
        size_t most = 0;

        while (e < counted.size() && counted[e].size() > static_cast<size_t> (maxParts))
            most = std::max (most, counted[e++].size());

        problems.push_back ({ { "severity", std::min (9.0, 5.0 + static_cast<double> (most - static_cast<size_t> (maxParts))) }, { "kind", "tooManyParts" },
                              { "channels", counted[b] },
                              { "what", "bars " + std::to_string (b + 1) + "-" + std::to_string (e) + " have " + std::to_string (most) + " parts at once (at most "
                                        + std::to_string (maxParts) + "): too much at once, nothing stands out" },
                              { "fix", "when a part comes in, take one out: let one of them rest in these bars (mute its clips there), or move its entrance to the next section" } });
        b = e;
    }

    // Parts coming in together that weren't heard in the 16 bars before (the song's first bar aside).
    for (size_t b = 1; b < counted.size(); ++b)
    {
        std::vector<std::string> fresh;

        for (auto& c : counted[b])
        {
            bool before = false;

            for (size_t k = b >= 16 ? b - 16 : 0; k < b && ! before; ++k)
                before = std::find (counted[k].begin(), counted[k].end(), c) != counted[k].end();

            if (! before)
                fresh.push_back (c);
        }

        if (fresh.size() >= 2)
        {
            std::string names;

            for (size_t i = 0; i < fresh.size(); ++i)
                names += (i == 0 ? "" : i + 1 == fresh.size() ? " and " : ", ") + fresh[i];

            problems.push_back ({ { "severity", 4.0 + static_cast<double> (fresh.size()) }, { "kind", "entersTogether" }, { "channels", fresh },
                                  { "what", "at bar " + std::to_string (b + 1) + " " + names + " come in together: each new part should get its own entrance" },
                                  { "fix", "bring them in one at a time, a section (8 or 16 bars) apart" } });
        }
    }

    return { { "problems", problems }, { "bars", barsJson }, { "beds", beds } };
}

std::shared_ptr<Placement> preparePlacement (tracktion::Engine& engine, const Project& project, const Id& channel,
                                             const CompileOptions& compileOptions, const std::filesystem::path& file)
{
    std::filesystem::create_directories (file.parent_path());
    auto placement = std::make_shared<Placement>();
    placement->file = file;

    // The channel alone and the rest without it, both kept: played in sync, the rest can be faded
    // under the channel. The song is them together. Solos are left out: the whole song plays.
    auto& alone = placement->alone;
    auto& rest = placement->rest;
    alone = project;
    rest = project;

    for (auto& c : alone.channels)
    {
        c.mute = c.id != channel;
        c.solo = false;
    }

    for (auto& c : rest.channels)
    {
        c.mute = c.mute || c.id == channel;
        c.solo = false;
    }

    for (auto* p : { &alone, &rest })
    {
        for (auto& i : p->mixer.inserts)
            i.solo = false;

        for (auto& r : p->playlist.rows)
            r.solo = false;
    }

    auto sibling = [&] (const char* suffix)
    {
        auto f = file;
        f.replace_filename (file.stem().string() + suffix + ".wav");
        return f;
    };

    placement->aloneFile = sibling ("-alone");
    placement->restFile = sibling ("-rest");
    placement->aloneEdit.emplace (compileProject (engine, alone, compileOptions));
    placement->restEdit.emplace (compileProject (engine, rest, compileOptions));
    return placement;
}

Json renderPlacement (Placement& placement, const RenderOptions& renderOptions)
{
    const auto& file = placement.file;
    const auto& aloneFile = placement.aloneFile;
    const auto& restFile = placement.restFile;
    renderToWav (*placement.aloneEdit, placement.alone, aloneFile, renderOptions);
    renderToWav (*placement.restEdit, placement.rest, restFile, renderOptions);

    const auto own = loadAudio (aloneFile);
    const auto others = loadAudio (restFile);

    Audio mix;
    mix.sampleRate = own.sampleRate;
    mix.buffer.makeCopyOf (own.buffer);

    for (int ch = 0; ch < std::min (mix.buffer.getNumChannels(), others.buffer.getNumChannels()); ++ch)
        mix.buffer.addFrom (ch, 0, others.buffer, ch, 0, std::min (mix.buffer.getNumSamples(), others.buffer.getNumSamples()));

    writeAudio (mix.buffer, mix.sampleRate, file, {});

    Json result { { "file", file.string() }, { "alone", aloneFile.string() }, { "rest", restFile.string() } };

    if (peakDb (own) < -90)
    {
        result["silent"] = true;
        return result;
    }

    const auto ownBands = bandEnergies (own);
    const auto restBands = bandEnergies (others);
    double total = 0;

    for (auto& b : ownBands)
        total += toPower (b.db);

    // How much of what it plays is covered: in each band, the rest's share, weighted by how much
    // of the channel is in that band.
    double covered = 0;
    auto covers = Json::array(), holds = Json::array();

    for (size_t b = 0; b < ownBands.size(); ++b)
    {
        const auto c = toPower (ownBands[b].db);
        const auto r = toPower (restBands[b].db);
        const auto weight = total > 0 ? c / total : 0.0;
        const auto overlap = c + r > 0 ? r / (c + r) : 0.0;
        covered += weight * overlap;

        if (weight >= 0.1 && overlap >= 0.5)
            covers.push_back (bandName (ownBands[b]));

        if (weight >= 0.15)
            holds.push_back (bandName (ownBands[b]));
    }

    result["fit"] = std::round (100 * (1 - covered));
    result["covers"] = covers;
    result["holds"] = holds;
    result["levelDb"] = round (integratedLufs (own) - integratedLufs (mix), 0.1);
    return result;
}

Json placeInMix (tracktion::Engine& engine, const Project& project, const Id& channel, const CompileOptions& compileOptions,
                 const RenderOptions& renderOptions, const std::filesystem::path& file)
{
    auto placement = preparePlacement (engine, project, channel, compileOptions, file);
    return renderPlacement (*placement, renderOptions);
}

} // namespace gk::analysis
