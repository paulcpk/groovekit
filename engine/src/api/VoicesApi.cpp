#include "ApiHandler.h"
#include "Rpc.h"
#include "listening/Listening.h"
#include "listening/VoiceLibrary.h"

namespace gk
{

Json ApiHandler::callVoices (const std::string& method, const Json& params)
{
    if (listening == nullptr)
        throw RpcError (operationFailed, "listening isn't available in this engine");

    const auto& project = session.getProject();
    const auto stepTicks = model::ticksPerBar (project.timeSignature) / 16;

    // A style's library: the one named, or the song's.
    auto library = [&] (const std::string& param, bool required) -> std::optional<listening::VoiceLibrary>
    {
        auto name = params.value (param, std::string());

        if (name.empty() && param == "style" && project.style)
            name = *project.style;

        if (name.empty())
        {
            if (required)
                throw RpcError (invalidParams, "Name a style (styles list), or give the song one");

            return std::nullopt;
        }

        auto profile = listening->getStyles().get (name);

        if (profile == nullptr)
            throw RpcError (invalidParams, "No style named \"" + name + "\" with heard references (styles list)");

        return listening::buildVoiceLibrary (*profile);
    };

    auto songKey = [&] () -> std::optional<music::Key>
    {
        if (auto k = params.value ("key", std::string()); ! k.empty() && k != "song")
        {
            if (auto parsed = music::parseKey (k))
                return parsed;

            throw RpcError (invalidParams, "Not a key: \"" + k + "\" (e.g. \"F minor\")");
        }

        if (project.key)
            return music::parseKey (*project.key);

        return std::nullopt;
    };

    auto notesJson = [&] (const std::vector<listening::GridNote>& notes)
    {
        auto list = Json::array();

        for (auto& n : notes)
            list.push_back ({ { "start", n.step * stepTicks }, { "length", n.steps * stepTicks }, { "key", n.key }, { "velocity", n.velocity } });

        return list;
    };

    if (method == "voices.library")
    {
        auto lib = library ("style", true);
        return { { "style", lib->style }, { "library", lib->toJson() }, { "text", lib->describe() } };
    }

    if (method == "voices.song")
    {
        auto song = listening::songVoices (project);
        auto channels = Json::array();

        for (auto& c : song.channels)
            channels.push_back (c);

        // Each voice named by its channel in the text.
        auto voices = song.voices;

        for (size_t i = 0; i < voices.voices.size(); ++i)
            voices.voices[i].role += " on " + song.channels[i];

        return { { "voices", song.voices.toJson() }, { "channels", channels }, { "bars", song.bars }, { "text", listening::describeVoices (voices) } };
    }

    if (method == "voices.check")
    {
        auto lib = library ("style", true);
        auto findings = listening::checkVoices (listening::songVoices (project), *lib, songKey());
        std::string text = findings.empty() ? "Nothing stands out against " + lib->style + "'s voices.\n" : "";

        for (size_t i = 0; i < findings.size(); ++i)
            text += std::to_string (i + 1) + ". " + findings[i] + "\n";

        return { { "style", lib->style }, { "findings", findings }, { "text", text } };
    }

    if (method == "voices.write")
    {
        auto lib = library ("style", true);
        auto notesFrom = library ("notesFrom", false);

        listening::WriteOptions options;
        options.role = readParam<std::string> (params, "role");
        options.key = songKey();
        options.bars = params.value ("bars", 0);
        options.relation = params.value ("relation", std::string());
        options.variation = params.value ("variation", 0);
        options.count = std::clamp (params.value ("count", 3), 1, 8);
        options.notesFrom = notesFrom ? &*notesFrom : nullptr;

        if (! options.relation.empty() && options.relation != "answer" && options.relation != "interlock" && options.relation != "together")
            throw RpcError (invalidParams, "relation is answer, interlock or together");

        // The partner: a channel of the song, its longest-playing figure.
        if (auto partner = params.value ("partner", std::string()); ! partner.empty())
        {
            auto song = listening::songVoices (project);
            auto it = std::find (song.channels.begin(), song.channels.end(), partner);

            if (it == song.channels.end())
                throw RpcError (invalidParams, "\"" + partner + "\" has no pitched notes in the playlist (voices song lists the parts)");

            const auto& voice = song.voices.voices[static_cast<size_t> (it - song.channels.begin())];
            const listening::Voice::Run* longest = nullptr;

            for (auto& run : voice.runs)
                if (longest == nullptr || run.to - run.from > longest->to - longest->from)
                    longest = &run;

            if (longest != nullptr)
            {
                options.partner = longest->notes;
                options.partnerBars = longest->bars;
                options.partnerRole = voice.role;
            }
        }

        if (lib->tracks == 0)
            throw RpcError (operationFailed, lib->describe());

        auto written = listening::writeVoices (*lib, options);

        if (written.empty())
            throw RpcError (operationFailed, "No " + options.role + " figures in " + lib->style + "'s voices (voices library lists its roles)");

        auto candidates = Json::array();

        for (auto& w : written)
        {
            Json c { { "notes", notesJson (w.notes) }, { "length", w.bars * 16 * stepTicks }, { "bars", w.bars }, { "from", w.from },
                     { "figure", listening::figureText (w.notes, w.bars) } };

            if (! w.relation.empty())
                c["relation"] = w.relation;

            candidates.push_back (c);
        }

        Json result { { "candidates", candidates }, { "style", lib->style } };

        if (options.key)
            result["key"] = music::keyName (*options.key);

        return result;
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

} // namespace gk
