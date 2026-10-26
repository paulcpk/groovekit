#include "Render.h"

namespace gk
{

namespace te = tracktion;

void renderToWav (te::Edit& edit, const model::Project& project, const std::filesystem::path& output, const RenderOptions& options)
{
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
    params.destFile = file;
    params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
    params.bitDepth = options.bitDepth;
    params.sampleRateForAudio = options.sampleRate;
    params.blockSizeForAudio = options.blockSize;
    params.time = { toTime (startTicks), toTime (endTicks) + te::TimeDuration::fromSeconds (options.tailSeconds) };
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

    if (! file.existsAsFile())
        throw std::runtime_error ("Render failed: no output was written");
}

} // namespace gk
