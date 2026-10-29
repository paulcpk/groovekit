#include "MixAnalysis.h"

#include "Analysis.h"
#include "mixer/Slots.h"

namespace gk::analysis
{

using namespace model;

namespace
{
    constexpr float maskingShare = 0.25f;

    struct Source
    {
        std::string id;                 // the channel
        std::function<void (Project&)> isolate;
    };

    /** Every channel heard on its own; audio channels carry the audio clips. */
    std::vector<Source> findSources (const Project& project)
    {
        std::vector<Source> sources;

        for (auto& c : project.channels)
        {
            if (c.mute)
                continue;

            sources.push_back ({ c.id, [id = c.id] (Project& p)
            {
                for (auto& other : p.channels)
                {
                    other.mute = other.id != id;
                    other.solo = false;
                }
            } });
        }

        return sources;
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
        std::vector<SpectrumBand> mixBands;
        std::vector<Stem> stems;       // the sources that play in the range
    };

    Audio renderAudio (tracktion::Engine& engine, const Project& p, const CompileOptions& compileOptions,
                       const RenderOptions& renderOptions, const std::filesystem::path& file)
    {
        auto compiled = compileProject (engine, p, compileOptions);
        renderToWav (compiled, p, file, renderOptions);
        auto audio = loadAudio (file);
        std::filesystem::remove (file);
        return audio;
    }

    Renders renderStems (tracktion::Engine& engine, const Project& project, const CompileOptions& compileOptions,
                         const RenderOptions& renderOptions, const std::filesystem::path& workDir)
    {
        // A folder of its own, so analyses running at once don't share files.
        const auto dir = workDir / juce::Uuid().toDashedString().toStdString();
        std::filesystem::create_directories (dir);

        struct Cleanup { std::filesystem::path dir; ~Cleanup() { std::error_code e; std::filesystem::remove_all (dir, e); } } cleanup { dir };

        Renders r;
        r.mix = renderAudio (engine, project, compileOptions, renderOptions, dir / "mix.wav");
        r.mixBands = bandEnergies (r.mix);
        int index = 0;

        for (auto& source : findSources (project))
        {
            auto isolated = project;
            source.isolate (isolated);
            auto audio = renderAudio (engine, isolated, compileOptions, renderOptions, dir / ("stem-" + std::to_string (index++) + ".wav"));
            const auto peak = peakDb (audio);

            if (peak < -90)
                continue; // the source doesn't play in this range

            r.stems.push_back ({ source.id, bandEnergies (audio), integratedLufs (audio), peak });
        }

        return r;
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

Json analyseMix (tracktion::Engine& engine, const Project& project, const CompileOptions& compileOptions,
                 const RenderOptions& renderOptions, const std::filesystem::path& workDir)
{
    auto r = renderStems (engine, project, compileOptions, renderOptions, workDir);
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
        { "mix", analyse (r.mix, true) },
        { "bands", bandNames },
        { "stems", stemJson },
        { "masking", masking },
    };
}

Json checkMix (tracktion::Engine& engine, const Project& project, const CompileOptions& compileOptions,
               const RenderOptions& renderOptions, const std::filesystem::path& workDir, const MixCheckContext& context)
{
    auto r = renderStems (engine, project, compileOptions, renderOptions, workDir);
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

    for (auto& b : spectralBalance (r.mix))
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
        { "mix", { { "integratedLufs", round (integratedLufs (r.mix), 0.1) }, { "truePeakDb", round (truePeakDb (r.mix), 0.1) } } },
        { "references", haveReferences },
    };
}

Json placeInMix (tracktion::Engine& engine, const Project& project, const Id& channel, const CompileOptions& compileOptions,
                 const RenderOptions& renderOptions, const std::filesystem::path& file)
{
    std::filesystem::create_directories (file.parent_path());

    auto compiled = compileProject (engine, project, compileOptions);
    renderToWav (compiled, project, file, renderOptions);
    const auto mix = loadAudio (file);

    auto solo = project;

    for (auto& c : solo.channels)
    {
        c.mute = c.id != channel;
        c.solo = false;
    }

    auto aloneFile = file;
    aloneFile.replace_filename (file.stem().string() + "-alone.wav");
    const auto alone = renderAudio (engine, solo, compileOptions, renderOptions, aloneFile);

    Json result { { "file", file.string() } };

    if (peakDb (alone) < -90)
    {
        result["silent"] = true;
        return result;
    }

    const auto mixBands = bandEnergies (mix);
    const auto own = bandEnergies (alone);
    double total = 0;

    for (auto& b : own)
        total += toPower (b.db);

    // How much of what it plays is covered: in each band, the rest's share, weighted by how much
    // of the channel is in that band.
    double covered = 0;
    auto covers = Json::array(), holds = Json::array();

    for (size_t b = 0; b < own.size(); ++b)
    {
        const auto c = toPower (own[b].db);
        const auto rest = std::max (0.0, toPower (mixBands[b].db) - c);
        const auto weight = total > 0 ? c / total : 0.0;
        const auto overlap = c + rest > 0 ? rest / (c + rest) : 0.0;
        covered += weight * overlap;

        if (weight >= 0.1 && overlap >= 0.5)
            covers.push_back (bandName (mixBands[b]));

        if (weight >= 0.15)
            holds.push_back (bandName (mixBands[b]));
    }

    result["fit"] = std::round (100 * (1 - covered));
    result["covers"] = covers;
    result["holds"] = holds;
    result["levelDb"] = round (integratedLufs (alone) - integratedLufs (mix), 0.1);
    return result;
}

} // namespace gk::analysis
