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

Json ApiHandler::callRender (const std::string& method, const Json& params, Client& client)
{
    if (method == "render")
        return render (params);

    if (method == "audio.peaks")
        return peaks (params);

    if (method == "master")
    {
        MasterOptions options;
        options.execute.origin = client.origin;

        if (auto v = optionalNumber (params, "targetLufs"))     options.targetLufs = *v;
        if (auto v = optionalNumber (params, "ceilingDb"))      options.ceilingDb = *v;
        if (auto v = optionalNumber (params, "releaseMs"))      options.releaseMs = *v;
        if (auto v = optionalNumber (params, "start"))          options.render.start = static_cast<model::Ticks> (*v);
        if (auto v = optionalNumber (params, "end"))            options.render.end = static_cast<model::Ticks> (*v);
        if (params.contains ("group"))                          options.execute.group = readParam<std::string> (params, "group");

        deferred = masterToLoudnessSteps (context.getEngine(), session, options);
        return {};
    }

    if (method == "analyzeMix")
    {
        RenderOptions options;

        if (auto v = optionalNumber (params, "start"))          options.start = static_cast<model::Ticks> (*v);
        if (auto v = optionalNumber (params, "end"))            options.end = static_cast<model::Ticks> (*v);
        if (auto v = optionalNumber (params, "tailSeconds"))    options.tailSeconds = *v;

        const auto projectDir = projectDirOf (session);
        deferred = analysis::analyseMixSteps (context.getEngine(), session.getProject(), { projectDir, tracktion::Edit::forRendering },
                                              options, projectDir / "renders" / "analysis");
        return {};
    }

    if (method == "analyze")
    {
        const auto path = readParam<std::string> (params, "path");
        const auto start = optionalNumber (params, "start").value_or (0);
        const auto end = optionalNumber (params, "end");
        auto steps = std::make_shared<Steps>();
        auto* result = &steps->result;

        steps->pooled ([path, start, end, result]
        {
            auto audio = analysis::loadAudio (path);
            *result = analysis::analyse (analysis::slice (audio, { start, end, {} }), true);
        });

        deferred = steps;
        return {};
    }

    throw RpcError (methodNotFound, "unknown method \"" + method + "\"");
}

Json ApiHandler::peaks (const Json& params)
{
    auto path = std::filesystem::path (readParam<std::string> (params, "path"));

    if (path.is_relative())
        if (auto dir = session.getProjectDir())
            path = *dir / path;

    // Sampler windows ask for the wave itself: its lowest and highest value per slice, finer.
    const bool withSigns = params.value ("signed", false);
    const auto resolution = std::clamp (optionalNumber (params, "resolution").value_or (100.0), 1.0, withSigns ? 48000.0 : 1000.0);

    // A sampler layer's processing (process, punch, tail): the wave as it plays.
    model::SamplerLayer layer;
    bool processed = false;

    if (params.contains ("process") || params.contains ("punch") || params.contains ("tail"))
    {
        if (params.contains ("process"))
        {
            try
            {
                model::Reader reader;
                readValue (reader, params["process"], layer.process.emplace());
            }
            catch (const model::ReadError& e)
            {
                throw RpcError (invalidParams, std::string ("params.process") + e.what());
            }
        }

        layer.punch = optionalNumber (params, "punch").value_or (0.0);
        layer.tail = optionalNumber (params, "tail").value_or (0.0);

        processed = ! sampler::toProcessing (layer).isNeutral();
    }

    const auto key = path.string() + "@" + std::to_string (resolution) + (withSigns ? "+" : "")
                   + (processed ? sampler::toProcessing (layer).key() : "");
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time (path, ec);

    if (ec)
        throw RpcError (operationFailed, "no file \"" + path.string() + "\"");

    if (auto it = peaksCache.find (key); it != peaksCache.end() && it->second.first == modified)
        return it->second.second;

    analysis::Audio audio;
    double timeScale = 1;

    if (processed)
    {
        auto& cache = sampler::SampleCache::getInstance();
        const auto sample = cache.processed (cache.load (path), sampler::toProcessing (layer));
        audio.buffer = sample->audio;
        audio.sampleRate = sample->sampleRate;
        timeScale = sample->timeScale;
    }
    else
    {
        audio = analysis::loadAudio (path);
    }

    const auto& buffer = audio.buffer;
    const auto perPeak = std::max (1, static_cast<int> (std::round (audio.sampleRate / resolution)));
    auto values = Json::array(), lows = Json::array(), highs = Json::array();
    const auto rounded = [] (float v) { return std::round (std::clamp (v, -1.0f, 1.0f) * 1000.0f) / 1000.0f; };

    for (int start = 0; start < buffer.getNumSamples(); start += perPeak)
    {
        const auto n = std::min (perPeak, buffer.getNumSamples() - start);
        float peak = 0, low = 0, high = 0;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto range = buffer.findMinMax (ch, start, n);
            low = std::min (low, range.getStart());
            high = std::max (high, range.getEnd());
            peak = std::max (peak, std::max (-range.getStart(), range.getEnd()));
        }

        values.push_back (rounded (peak));

        if (withSigns)
        {
            lows.push_back (rounded (low));
            highs.push_back (rounded (high));
        }
    }

    Json result { { "durationSeconds", buffer.getNumSamples() / audio.sampleRate },
                  { "sampleRate", audio.sampleRate },
                  { "channels", buffer.getNumChannels() },
                  { "resolution", audio.sampleRate / perPeak },
                  { "timeScale", timeScale },
                  { "peaks", std::move (values) } };

    if (withSigns)
    {
        result["min"] = std::move (lows);
        result["max"] = std::move (highs);
    }

    // Waveforms are asked for again on every redraw of a song; keep a few dozen.
    if (peaksCache.size() > 64)
        peaksCache.clear();

    peaksCache[key] = { modified, result };
    return result;
}

Json ApiHandler::render (const Json& params)
{
    RenderOptions options;

    if (auto v = optionalNumber (params, "start"))          options.start = static_cast<model::Ticks> (*v);
    if (auto v = optionalNumber (params, "end"))            options.end = static_cast<model::Ticks> (*v);
    if (auto v = optionalNumber (params, "tailSeconds"))    options.tailSeconds = *v;
    if (auto v = optionalNumber (params, "sampleRate"))     options.sampleRate = *v;

    EncodeOptions encode;
    encode.format = params.value ("format", std::string ("wav"));
    encode.bitDepth = static_cast<int> (optionalNumber (params, "bitDepth").value_or (24));
    encode.bitrate = static_cast<int> (optionalNumber (params, "bitrate").value_or (320));
    encode.dither = params.value ("dither", true);

    const auto silenceBefore = std::max (0.0, optionalNumber (params, "silenceBefore").value_or (0.0));
    const auto silenceAfter = std::max (0.0, optionalNumber (params, "silenceAfter").value_or (0.0));
    const auto normalize = optionalNumber (params, "normalize");

    const auto extension = extensionFor (encode.format);

    if (encode.format != "mp3" && encode.bitDepth != 16 && encode.bitDepth != 24 && encode.bitDepth != 32)
        throw RpcError (invalidParams, "bitDepth must be 16, 24 or 32");

    if (encode.format == "flac" && encode.bitDepth == 32)
        throw RpcError (invalidParams, "FLAC is 16 or 24 bits");

    if (encode.format == "mp3" && options.sampleRate != 44100 && options.sampleRate != 48000)
        throw RpcError (invalidParams, "MP3 is 44.1 or 48 kHz");

    if (encode.format == "mp3" && (encode.bitrate < 96 || encode.bitrate > 320))
        throw RpcError (invalidParams, "bitrate must be 96 to 320 kbit/s");

    if (options.sampleRate < 8000 || options.sampleRate > 192000)
        throw RpcError (invalidParams, "sampleRate must be between 8000 and 192000");

    const auto projectDir = projectDirOf (session);

    std::filesystem::path output;

    if (params.contains ("path"))
    {
        output = readParam<std::string> (params, "path");
    }
    else
    {
        std::filesystem::create_directories (projectDir / "renders");

        // Numbered after the renders already there, so reopening the project doesn't write over them.
        if (renderCount == 0)
        {
            std::error_code ec;

            for (auto& entry : std::filesystem::directory_iterator (projectDir / "renders", ec))
                if (const auto stem = entry.path().stem().string(); stem.starts_with ("render-"))
                    renderCount = std::max (renderCount, static_cast<int> (std::strtol (stem.c_str() + 7, nullptr, 10)));
        }

        output = projectDir / "renders" / ("render-" + std::to_string (++renderCount) + "." + extension);
    }

    // Rendered at full resolution, then given its silence, level and format. The mix and its
    // stems get the same gain, so the stems still add up to the mix. Each Edit is compiled on the
    // message thread and rendered on the pool, one after the other.
    options.bitDepth = 32;

    struct Run
    {
        std::optional<float> gain;
        std::optional<CompiledEdit> compiled;
        model::Project project;
        Json analysis;
        Json result, stems = Json::array();
    };

    auto run = std::make_shared<Run>();
    auto steps = std::make_shared<Steps>();
    auto& engine = context.getEngine();

    // With a progress id, how far the export is goes out as renderProgress notifications: the
    // mix and each stem are a part, rendering most of a part's share and writing the rest.
    const auto progressId = params.value ("progress", std::string());
    const auto withStems = params.value ("stems", false);
    const auto parts = 1 + (withStems ? static_cast<int> (session.getProject().mixer.inserts.size()) - 1 : 0);
    const auto formatName = encode.format == "mp3" ? std::string ("MP3") : encode.format == "flac" ? std::string ("FLAC") : std::string ("WAV");

    auto report = [this, alive = alive, progressId, parts] (int part, float within, const std::string& stage)
    {
        if (progressId.empty())
            return;

        Json params { { "id", progressId }, { "progress", std::round ((part + within) / parts * 1000) / 1000 }, { "stage", stage } };

        // Called on the pool: broadcast from the message thread.
        juce::MessageManager::callAsync ([this, alive, params]
        {
            if (*alive && broadcast)
                broadcast (notification ("renderProgress", params).dump());
        });
    };

    auto addRender = [&, run, steps, projectDir, options, encode, silenceBefore, silenceAfter, normalize, report, formatName]
                     (const std::filesystem::path& file, bool isMix, std::optional<model::Id> soloInsert, int part, const std::string& name)
    {
        steps->here ([run, &engine, projectDir, soloInsert, file, this]
        {
            // The song as it is now; a stem's insert soloed (heard through what it feeds).
            run->project = session.getProject();

            if (soloInsert)
            {
                for (auto& i : run->project.mixer.inserts)
                    i.solo = i.id == *soloInsert;

                for (auto& c : run->project.channels)
                    c.solo = false;

                for (auto& t : run->project.playlist.rows)
                    t.solo = false;
            }

            std::filesystem::create_directories (file.parent_path());
            run->compiled.emplace (compileProject (engine, run->project, { projectDir, tracktion::Edit::forRendering }));
        });

        steps->pooled ([run, file, isMix, options, encode, silenceBefore, silenceAfter, normalize, report, formatName, part, name]
        {
            const auto temp = file.parent_path() / ("." + file.stem().string() + ".render.wav");
            const auto rendering = "Rendering " + name;
            auto withProgress = options;
            withProgress.progress = [&] (float p) { report (part, p * 0.8f, rendering); };
            report (part, 0, rendering);
            renderToWav (*run->compiled, run->project, temp, withProgress);

            auto audio = analysis::loadAudio (temp);
            std::error_code ec;
            std::filesystem::remove (temp, ec);

            const auto before = static_cast<int> (std::round (silenceBefore * audio.sampleRate));
            const auto after = static_cast<int> (std::round (silenceAfter * audio.sampleRate));

            if (before > 0 || after > 0)
            {
                juce::AudioBuffer<float> padded (audio.buffer.getNumChannels(), audio.buffer.getNumSamples() + before + after);
                padded.clear();

                for (int c = 0; c < padded.getNumChannels(); ++c)
                    padded.copyFrom (c, before, audio.buffer, c, 0, audio.buffer.getNumSamples());

                audio.buffer = std::move (padded);
            }

            if (normalize && ! run->gain)
            {
                const auto peak = audio.buffer.getMagnitude (0, audio.buffer.getNumSamples());
                run->gain = peak > 0 ? juce::Decibels::decibelsToGain (static_cast<float> (*normalize)) / peak : 1.0f;
            }

            if (run->gain)
                audio.buffer.applyGain (*run->gain);

            // Measured before it's written, so the file appears when the export is all but done.
            if (isMix)
            {
                report (part, 0.8f, "Measuring the mix");
                run->analysis = analysis::analyse (audio, true);
            }

            report (part, isMix ? 0.9f : 0.8f, "Writing the " + formatName + (isMix ? "" : " of " + name));
            writeAudio (audio.buffer, audio.sampleRate, file, encode);
        });

        // The Edit goes on the message thread, as Edits must.
        steps->here ([run] { run->compiled.reset(); });
    };

    addRender (output, true, std::nullopt, 0, "the mix");

    steps->here ([run, output, encode]
    {
        run->result = { { "path", std::filesystem::absolute (output).string() }, { "format", encode.format },
                        { "analysis", std::move (run->analysis) } };

        if (run->gain)
            run->result["gainDb"] = std::round (juce::Decibels::gainToDecibels (*run->gain) * 10) / 10;
    });

    // Stems: each insert but master on its own (soloed, so it's heard through what it feeds),
    // over the same range, next to the mix as <name>-<insert>.<format>.
    if (withStems)
    {
        int part = 0;

        for (auto& insert : session.getProject().mixer.inserts)
        {
            if (insert.id == model::masterInsertId)
                continue;

            ++part;
            auto stem = output.parent_path() / (output.stem().string() + "-" + insert.id + "." + extension);
            addRender (stem, false, insert.id, part,
                       "stem " + std::to_string (part) + " of " + std::to_string (parts - 1) + " (" + (insert.name.empty() ? insert.id : insert.name) + ")");
            steps->here ([run, id = insert.id, stem] { run->stems.push_back ({ { "insert", id }, { "path", std::filesystem::absolute (stem).string() } }); });
        }

        steps->here ([run] { run->result["stems"] = run->stems; });
    }

    auto* result = &steps->result;
    steps->here ([run, result] { *result = std::move (run->result); });
    deferred = steps;
    return {};
}

} // namespace gk
