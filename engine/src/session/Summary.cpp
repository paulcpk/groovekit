#include "Summary.h"

#include <cmath>
#include <filesystem>
#include <map>

namespace gk
{

using namespace model;

namespace
{
    double toBars (Ticks t, const TimeSignature& ts)
    {
        return std::round (static_cast<double> (t) / static_cast<double> (ticksPerBar (ts)) * 1000.0) / 1000.0;
    }

    Json describeInstrument (const Instrument& instrument)
    {
        if (auto sampler = std::get_if<SamplerInstrument> (&instrument))
        {
            auto layers = Json::array();

            for (auto& l : sampler->layers)
            {
                // The path as the song has it, so describe_sample can find the file.
                Json layer { { "id", l.id }, { "sample", l.sample } };

                if (l.keyRange.low != 0 || l.keyRange.high != 127)
                    layer["keys"] = std::to_string (l.keyRange.low) + "-" + std::to_string (l.keyRange.high);

                if (l.velocityRange.low != 1 || l.velocityRange.high != 127)
                    layer["velocities"] = std::to_string (l.velocityRange.low) + "-" + std::to_string (l.velocityRange.high);

                if (! l.pitchTracking)
                    layer["pitchTracking"] = false;

                if (l.transpose != 0 || l.fineTune != 0)
                    layer["transpose"] = l.transpose + l.fineTune / 100.0;

                if (l.reverse)          layer["reverse"] = true;
                if (l.sourceTempo)      layer["sourceTempo"] = *l.sourceTempo;

                layers.push_back (std::move (layer));
            }

            Json j { { "type", "sampler" }, { "layers", layers } };

            if (sampler->filter.type != SamplerFilterType::off)
                j["filter"] = toString (sampler->filter.type);

            return j;
        }

        auto& plugin = std::get<PluginInstrument> (instrument).plugin;
        return { { "type", "plugin" }, { "name", plugin.name } };
    }

    Json describeClip (const Clip& clip, const TimeSignature& ts)
    {
        Json j {
            { "id", getClipId (clip) },
            { "startBar", toBars (getClipStart (clip), ts) + 1 },
            { "lengthBars", toBars (getClipLength (clip), ts) },
        };

        if (auto pc = std::get_if<PatternClip> (&clip))
            j["pattern"] = pc->pattern;
        else if (auto ac = std::get_if<AudioClip> (&clip))
            j["audio"] = ac->channel;
        else if (auto automation = std::get_if<AutomationClip> (&clip))
        {
            auto& t = automation->target;
            std::string target = toString (t.scope);

            for (auto& part : { t.channel, t.insert, t.effect, t.send })
                if (part)
                    target += ":" + *part;

            j["automation"] = target + "." + t.param;
        }

        if (isClipMuted (clip))
            j["mute"] = true;

        return j;
    }
}

Json summarizeProject (const Project& project)
{
    const auto& ts = project.timeSignature;
    const auto end = getSongEnd (project);

    Json summary {
        { "name", project.name },
        { "tempo", project.tempo },
        { "timeSignature", std::to_string (ts.numerator) + "/" + std::to_string (ts.denominator) },
        { "ticksPerBar", ticksPerBar (ts) },
        { "lengthBars", toBars (end, ts) },
        { "lengthSeconds", std::round (static_cast<double> (end) / ticksPerQuarter * 60.0 / project.tempo * 100.0) / 100.0 },
    };

    if (project.key)
        summary["key"] = *project.key;

    auto channels = Json::array();

    for (auto& c : project.channels)
    {
        Json j { { "id", c.id }, { "instrument", describeInstrument (c.instrument) }, { "insert", c.insert } };

        if (! c.name.empty())    j["name"] = c.name;
        if (c.volumeDb != 0)     j["volumeDb"] = c.volumeDb;
        if (c.pan != 0)          j["pan"] = c.pan;
        if (c.mute)              j["mute"] = true;
        if (c.group == ChannelGroup::arrangement)  j["group"] = "arrangement";

        channels.push_back (std::move (j));
    }

    auto patterns = Json::array();

    for (auto& p : project.patterns)
    {
        auto notes = Json::object();

        for (auto& n : p.notes)
            notes[n.channel] = notes.value (n.channel, 0) + 1;

        Json j { { "id", p.id }, { "lengthBars", toBars (getPatternLength (p, ts), ts) }, { "notesPerChannel", notes } };

        if (! p.name.empty())
            j["name"] = p.name;

        patterns.push_back (std::move (j));
    }

    auto tracks = Json::array();

    // Tracks with clips or a name, by number.
    std::map<Lane, Json> lanes;

    for (auto& clip : project.playlist.clips)
        lanes[getClipTrack (clip)].push_back (describeClip (clip, ts));

    for (size_t i = 0; i < project.playlist.tracks.size(); ++i)
        if (! project.playlist.tracks[i].name.empty() && ! lanes.contains (static_cast<Lane> (i)))
            lanes[static_cast<Lane> (i)] = Json::array();

    for (auto& [lane, clips] : lanes)
    {
        Json j { { "track", lane }, { "clips", clips } };

        if (auto t = findTrack (project, lane))
        {
            if (! t->name.empty())   j["name"] = t->name;
            if (t->mute)             j["mute"] = true;
            if (t->solo)             j["solo"] = true;
        }

        tracks.push_back (std::move (j));
    }

    auto inserts = Json::array();

    for (auto& i : project.mixer.inserts)
    {
        auto effects = Json::array();

        for (auto& e : i.effects)
        {
            Json j { { "id", getEffectId (e) } };

            if (auto b = std::get_if<BuiltinEffect> (&e))
            {
                j["kind"] = toString (b->kind);
                j["params"] = toJson (b->params);

                if (b->bypass)
                    j["bypass"] = true;
            }
            else
            {
                j["plugin"] = std::get<PluginEffect> (e).plugin.name;
            }

            effects.push_back (std::move (j));
        }

        auto sends = Json::array();

        for (auto& s : i.sends)
            sends.push_back (Json { { "id", s.id }, { "target", s.target }, { "levelDb", s.levelDb } });

        Json j { { "id", i.id } };

        if (! i.name.empty())                  j["name"] = i.name;
        if (i.id != masterInsertId)            j["output"] = i.output;
        if (i.volumeDb != 0)                   j["volumeDb"] = i.volumeDb;
        if (i.pan != 0)                        j["pan"] = i.pan;
        if (i.mute)                            j["mute"] = true;
        if (! effects.empty())                 j["effects"] = effects;
        if (! sends.empty())                   j["sends"] = sends;

        inserts.push_back (std::move (j));
    }

    summary["channels"] = channels;
    summary["patterns"] = patterns;
    summary["playlist"] = tracks;
    summary["mixer"] = inserts;
    return summary;
}

} // namespace gk
