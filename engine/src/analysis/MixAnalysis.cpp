#include "MixAnalysis.h"

#include "Analysis.h"

namespace gk::analysis
{

using namespace model;

namespace
{
    constexpr float maskingShare = 0.25f;

    struct Source
    {
        std::string id;                 // "kick", or "audio:drums" for audio clips into an insert
        std::function<void (Project&)> isolate;
    };

    std::vector<Source> findSources (const Project& project)
    {
        std::vector<Source> sources;

        auto muteAllAudioClips = [] (Project& p, const std::optional<Id>& exceptInsert)
        {
            for (auto& clip : p.playlist.clips)
                if (auto ac = std::get_if<AudioClip> (&clip); ac != nullptr && (! exceptInsert || ac->insert != *exceptInsert))
                    ac->mute = true;
        };

        for (auto& c : project.channels)
        {
            if (c.mute)
                continue;

            sources.push_back ({ c.id, [id = c.id, muteAllAudioClips] (Project& p)
            {
                for (auto& other : p.channels)
                    if (other.id != id)
                        other.mute = true;

                muteAllAudioClips (p, {});
            } });
        }

        std::set<Id> audioInserts;

        for (auto& clip : project.playlist.clips)
            if (auto ac = std::get_if<AudioClip> (&clip); ac != nullptr && ! ac->mute)
                audioInserts.insert (ac->insert);

        for (auto& insert : audioInserts)
            sources.push_back ({ "audio:" + insert, [insert, muteAllAudioClips] (Project& p)
            {
                for (auto& c : p.channels)
                    c.mute = true;

                muteAllAudioClips (p, insert);
            } });

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

Json analyseMix (tracktion::Engine& engine, const Project& project, const CompileOptions& compileOptions,
                 const RenderOptions& renderOptions, const std::filesystem::path& workDir)
{
    std::filesystem::create_directories (workDir);

    auto renderProject = [&] (const Project& p, const std::string& name)
    {
        auto file = workDir / (name + ".wav");
        auto compiled = compileProject (engine, p, compileOptions);
        renderToWav (compiled, project, file, renderOptions);
        auto audio = loadAudio (file);
        std::filesystem::remove (file);
        return audio;
    };

    auto mix = renderProject (project, "mix");
    auto mixBands = bandEnergies (mix);

    struct Stem
    {
        std::string id;
        std::vector<SpectrumBand> bands;
        Json json;
    };

    std::vector<Stem> stems;
    int index = 0;

    for (auto& source : findSources (project))
    {
        auto isolated = project;
        source.isolate (isolated);
        auto audio = renderProject (isolated, "stem-" + std::to_string (index++));

        const auto peak = peakDb (audio);

        if (peak < -90)
            continue; // the source doesn't play in this range

        auto bands = bandEnergies (audio);
        auto share = Json::array();

        for (size_t b = 0; b < bands.size(); ++b)
        {
            const auto mixPower = toPower (mixBands[b].db);
            share.push_back (mixPower > 0 ? round (std::min (1.0, toPower (bands[b].db) / mixPower), 0.01) : 0.0);
        }

        stems.push_back ({ source.id, bands, {
            { "source", source.id },
            { "integratedLufs", round (integratedLufs (audio), 0.1) },
            { "peakDb", round (peak, 0.1) },
            { "bandShare", share },
        } });
    }

    // Bands that matter: within 30 dB of the mix's strongest band.
    float strongest = silenceDb;

    for (auto& b : mixBands)
        strongest = std::max (strongest, b.db);

    auto masking = Json::array();

    for (size_t i = 0; i < stems.size(); ++i)
    {
        for (size_t j = i + 1; j < stems.size(); ++j)
        {
            auto bands = Json::array();

            for (size_t b = 0; b < mixBands.size(); ++b)
            {
                const auto mixPower = toPower (mixBands[b].db);

                if (mixPower <= 0 || mixBands[b].db < strongest - 30)
                    continue;

                const auto a = toPower (stems[i].bands[b].db) / mixPower;
                const auto c = toPower (stems[j].bands[b].db) / mixPower;

                if (a >= maskingShare && c >= maskingShare)
                    bands.push_back (bandName (mixBands[b]));
            }

            if (! bands.empty())
                masking.push_back ({ { "a", stems[i].id }, { "b", stems[j].id }, { "bands", bands } });
        }
    }

    auto bandNames = Json::array();

    for (auto& b : mixBands)
        bandNames.push_back (bandName (b));

    auto stemJson = Json::array();

    for (auto& s : stems)
        stemJson.push_back (s.json);

    return {
        { "mix", analyse (mix, true) },
        { "bands", bandNames },
        { "stems", stemJson },
        { "masking", masking },
    };
}

} // namespace gk::analysis
