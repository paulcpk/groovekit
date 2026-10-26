#include "Render.h"

namespace gk
{

namespace te = tracktion;

namespace
{
    /** Copies a WAV file without its first seconds. */
    void trimStart (const juce::File& source, const juce::File& dest, double seconds, int bitDepth)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (source.createInputStream().release(), true));

        if (reader == nullptr)
            throw std::runtime_error ("Render failed: can't read " + source.getFullPathName().toStdString());

        const auto skip = static_cast<juce::int64> (std::llround (seconds * reader->sampleRate));
        const auto length = std::max<juce::int64> (0, reader->lengthInSamples - skip);

        dest.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (dest.createOutputStream().release(), reader->sampleRate,
                                                                              reader->numChannels, bitDepth, {}, 0));
        if (writer == nullptr)
            throw std::runtime_error ("Render failed: can't write " + dest.getFullPathName().toStdString());

        writer->writeFromAudioReader (*reader, skip, length);
    }
}

void renderToWav (const CompiledEdit& compiled, const model::Project& project, const std::filesystem::path& output, const RenderOptions& options)
{
    auto& edit = *compiled.edit;

    const auto startTicks = options.start.value_or (0);
    const auto endTicks = options.end.value_or (model::getSongEnd (project));

    if (endTicks <= startTicks && options.tailSeconds <= 0)
        throw std::runtime_error ("Nothing to render: the range is empty");

    auto toTime = [&] (model::Ticks t)
    {
        return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (static_cast<double> (t) / model::ticksPerQuarter));
    };

    auto file = juce::File (std::filesystem::absolute (output).string());
    file.deleteFile();

    te::Renderer::Parameters params (edit);
    params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
    params.bitDepth = options.bitDepth;
    params.sampleRateForAudio = options.sampleRate;
    params.blockSizeForAudio = options.blockSize;
    // Tracktion compensates plugin latency by reading sources ahead, so a render that starts where the
    // music starts would lose the first few milliseconds. Start a little earlier and trim it off.
    const auto padSeconds = compiled.outputLatencySeconds > 0 ? compiled.outputLatencySeconds + 0.05 : 0.0;
    const auto start = toTime (startTicks) - te::TimeDuration::fromSeconds (padSeconds);

    auto temp = file.getSiblingFile (file.getFileNameWithoutExtension() + ".untrimmed.wav");
    params.destFile = padSeconds > 0 ? temp : file;
    params.time = { start, toTime (endTicks) + te::TimeDuration::fromSeconds (options.tailSeconds) };
    params.tracksToDo = te::toBitSet (te::getAllTracks (edit));
    params.usePlugins = true;
    params.useMasterPlugins = false;
    params.canRenderInMono = false;
    params.checkNodesForAudio = false;
    params.ditheringEnabled = false;

    te::Renderer::turnOffAllPlugins (edit);

    std::atomic<float> progress { 0.0f };
    te::Renderer::RenderTask task ("Render", params, &progress, nullptr);

    if (task.errorMessage.isNotEmpty())
        throw std::runtime_error ("Render failed: " + task.errorMessage.toStdString());

    while (task.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
    {}

    te::Renderer::turnOffAllPlugins (edit);

    if (task.errorMessage.isNotEmpty())
        throw std::runtime_error ("Render failed: " + task.errorMessage.toStdString());

    if (padSeconds > 0)
    {
        trimStart (temp, file, padSeconds, options.bitDepth);
        temp.deleteFile();
    }

    if (! file.existsAsFile())
        throw std::runtime_error ("Render failed: no output was written");
}

} // namespace gk
