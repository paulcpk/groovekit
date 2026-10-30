#include "ApiHandler.h"
#include "Rpc.h"

#include "EngineContext.h"
#include "analysis/Analysis.h"
#include "analysis/MixAnalysis.h"
#include "compiler/EditCompiler.h"
#include "compiler/EditGraveyard.h"
#include "effects/BuiltinEffects.h"
#include "library/SampleLibrary.h"
#include "library/StarterPack.h"
#include "listening/Listening.h"
#include "listening/KickSound.h"
#include "listening/Maps.h"
#include "mixer/Slots.h"
#include "live/AudioDevices.h"
#include "live/LiveEngine.h"
#include "music/Notes.h"
#include "plugins/PluginCatalog.h"
#include "plugins/PluginWindows.h"
#include "render/Encode.h"
#include "render/Render.h"
#include "render/Stretch.h"
#include "session/Mastering.h"
#include "sampler/SamplerPlugin.h"
#include "guides/Guides.h"
#include "session/AgentNotes.h"
#include "session/Chats.h"
#include "session/Presets.h"
#include "session/RecentProjects.h"
#include "session/Summary.h"
#include "voice/Transcriber.h"

#include <juce_events/juce_events.h>

namespace gk
{

namespace
{
    /** One for the whole engine: the model stays loaded when another project opens. */
    voice::Transcriber& getTranscriber()
    {
        static voice::Transcriber transcriber (EngineContext::getAppDataFolder() / "models");
        return transcriber;
    }

    Json voiceStatusJson (const voice::Transcriber::Status& s)
    {
        Json j { { "ready", s.ready }, { "downloading", s.downloading }, { "progress", std::round (s.progress * 1000.0) / 1000.0 },
                 { "modelBytes", s.modelBytes } };

        if (! s.error.empty())
            j["error"] = s.error;

        return j;
    }
}

Json ApiHandler::callVoice (const std::string& method, const Json& params)
{
    auto& transcriber = getTranscriber();

    if (method == "voice.status")
    {
        // The UI asks when voice may be used soon: the model loads meanwhile.
        if (params.value ("prepare", false))
            transcriber.prepare();

        return voiceStatusJson (transcriber.getStatus());
    }

    if (method == "voice.download")
    {
        // Progress goes to every client from the message thread.
        transcriber.download ([alive = alive, this] (const voice::Transcriber::Status& s)
        {
            juce::MessageManager::callAsync ([alive, this, s]
            {
                if (*alive && broadcast)
                    broadcast (notification ("voiceModel", voiceStatusJson (s)).dump());
            });
        });

        return voiceStatusJson (transcriber.getStatus());
    }

    if (method == "voice.transcribe")
    {
        // 16-bit little-endian PCM at 16 kHz, mono, as base64.
        juce::MemoryOutputStream decoded;

        if (! juce::Base64::convertFromBase64 (decoded, juce::String (readParam<std::string> (params, "audio"))))
            throw RpcError (invalidParams, "audio must be base64");

        const auto* pcm = static_cast<const int16_t*> (decoded.getData());
        std::vector<float> samples (decoded.getDataSize() / 2);

        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float> (pcm[i]) / 32768.0f;

        if (! transcriber.getStatus().ready)
            throw RpcError (operationFailed, "The speech model isn't downloaded yet (voice.download)");

        // It takes a moment: the text arrives as a voiceTranscript notification with this job id.
        const auto job = juce::Uuid().toString().toStdString();
        const auto language = params.value ("language", std::string());

        std::thread ([alive = alive, this, job, language, samples = std::move (samples), &transcriber] () mutable
        {
            Json result { { "job", job }, { "recording", "voice-" + job.substr (0, 6) } };

            try
            {
                result["text"] = transcriber.transcribe (samples, language);

                // A melody hummed or sung in it ("for the lead: da da da") is noted, to be placed
                // with melody.voice, when what was said says one is there: the rise and fall of
                // speech alone can sound like a few notes.
                const auto frames = music::trackPitch (samples.data(), static_cast<int> (samples.size()), voice::Transcriber::sampleRate);

                if (auto sung = music::mentionsMelody (result["text"].get<std::string>()) ? music::findSungPart (frames) : std::nullopt)
                {
                    int count = 0;

                    for (auto& n : music::segmentNotes (frames))
                        if (n.start >= sung->start - 0.01 && n.end <= sung->end + 0.01)
                            ++count;

                    result["melody"] = { { "from", std::round (sung->start * 100) / 100 }, { "to", std::round (sung->end * 100) / 100 }, { "notes", count } };
                }
            }
            catch (const std::exception& e)
            {
                result["error"] = e.what();
            }

            juce::MessageManager::callAsync ([alive, this, result, samples = std::move (samples)] () mutable
            {
                if (! *alive)
                    return;

                voiceRecordings.emplace_back (result["recording"].get<std::string>(), std::move (samples));

                if (voiceRecordings.size() > 8)
                    voiceRecordings.erase (voiceRecordings.begin());

                if (broadcast)
                    broadcast (notification ("voiceTranscript", result).dump());
            });
        }).detach();

        return { { "job", job } };
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

namespace
{
    /** Where a melody is placed: the song's tempo and key (or the one asked for) and a register. */
    music::FitOptions fitOptions (const Json& params, const model::Project& project, bool snapByDefault)
    {
        music::FitOptions options;
        options.songTempo = project.tempo;

        const auto keyName = params.contains ("key") ? params["key"].get<std::string>() : project.key.value_or ("");

        if (! keyName.empty())
        {
            options.key = music::parseKey (keyName);

            if (! options.key)
                throw RpcError (invalidParams, "\"" + keyName + "\" isn't a key; name one like \"A minor\"");
        }

        options.snapToScale = params.value ("snapToScale", snapByDefault);

        if (auto r = params.value ("register", std::string()); ! r.empty())
        {
            static const std::map<std::string, std::pair<int, int>> registers {
                { "bass", { 28, 52 } }, { "chords", { 48, 72 } }, { "lead", { 60, 84 } }, { "high", { 72, 96 } },
            };

            auto it = registers.find (r);

            if (it == registers.end())
                throw RpcError (invalidParams, "register is bass, chords, lead or high");

            options.lowest = it->second.first;
            options.highest = it->second.second;
        }

        if (params.contains ("lowest"))     options.lowest = params["lowest"].get<int>();
        if (params.contains ("highest"))    options.highest = params["highest"].get<int>();

        return options;
    }

    Json melodyJson (const music::Melody& m, const music::FitOptions& options)
    {
        auto notes = Json::array();

        for (auto& n : m.notes)
            notes.push_back ({ { "start", n.start }, { "length", n.length }, { "key", n.key }, { "velocity", n.velocity } });

        Json j { { "notes", notes }, { "length", m.length }, { "heardTempo", std::round (m.sourceTempo * 10) / 10 },
                 { "transposed", m.transposed } };

        if (m.heardKey)
            j["heardKey"] = { { "key", music::keyName (m.heardKey->key) }, { "confidence", std::round (m.heardKey->confidence * 100) / 100 } };

        if (options.key)
            j["key"] = music::keyName (*options.key);

        return j;
    }
}

Json ApiHandler::callMelody (const std::string& method, const Json& params)
{
    if (method == "melody.voice")
    {
        if (voiceRecordings.empty())
            throw RpcError (operationFailed, "There's no voice message yet: record one with a melody hummed in it");

        auto it = voiceRecordings.end() - 1;

        if (auto id = params.value ("recording", std::string()); ! id.empty())
        {
            it = std::find_if (voiceRecordings.begin(), voiceRecordings.end(), [&] (auto& r) { return r.first == id; });

            if (it == voiceRecordings.end())
                throw RpcError (invalidParams, "no voice message \"" + id + "\" (only the last 8 are kept)");
        }

        const auto& samples = it->second;
        const auto frames = music::trackPitch (samples.data(), static_cast<int> (samples.size()), voice::Transcriber::sampleRate);
        auto sung = music::findSungPart (frames);

        if (params.contains ("from") || params.contains ("to"))
            sung = music::Span { params.value ("from", 0.0), params.value ("to", 1.0e9) };

        if (! sung)
            throw RpcError (operationFailed, "No melody was heard in that voice message: hum or sing it on \"da\", a note at a time");

        std::vector<music::HeardNote> notes;

        for (auto& n : music::segmentNotes (frames))
            if (n.start >= sung->start - 0.01 && n.end <= sung->end + 0.01)
                notes.push_back (n);

        auto options = fitOptions (params, session.getProject(), true);

        if (params.value ("tempo", std::string()) == "song")
            options.sourceTempo = session.getProject().tempo;

        auto j = melodyJson (music::fitMelody (notes, options), options);
        j["recording"] = it->first;
        j["from"] = sung->start;
        j["to"] = sung->end;
        return j;
    }

    if (method == "melody.reference")
    {
        if (listening == nullptr)
            throw RpcError (operationFailed, "this engine has no listening");

        const auto part = params.value ("part", std::string ("lead"));

        if (part != "lead" && part != "chords" && part != "bass")
            throw RpcError (invalidParams, "part is lead, chords or bass");

        listening::Listening::ReferenceNotes heard;

        heard = listening->hearNotes (params.value ("style", std::string()), readParam<std::string> (params, "track"),
                                      params.contains ("from") ? std::optional (params["from"].get<double>()) : std::nullopt,
                                      params.value ("length", 24.0), part == "bass" ? listening::Role::bass : listening::Role::music);

        // A lead is the top line of what plays, a bassline the bottom one; chords keep it all.
        const auto notes = part == "lead" ? music::topLine (heard.notes) : part == "bass" ? music::bottomLine (heard.notes) : heard.notes;

        auto options = fitOptions (params, session.getProject(), false);

        if (heard.tempo > 0)
            options.sourceTempo = heard.tempo;

        if (! params.contains ("register"))
            if (! params.contains ("lowest") && ! params.contains ("highest"))
                options.lowest = options.highest = std::nullopt;

        auto j = melodyJson (music::fitMelody (notes, options), options);
        j["track"] = heard.track;
        j["from"] = std::round (heard.from * 100) / 100;
        j["seconds"] = heard.length;
        j["part"] = part;
        return j;
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

} // namespace gk
