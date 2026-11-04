#pragma once

#include "Models.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace Ort { struct Session; }

namespace gk::listening
{

/** Splits music into drums, bass and the rest with htdemucs, on this Mac. Loads the model on
    first use and holds about a gigabyte while it lives. */
class Separator
{
public:
    explicit Separator (const Models&);
    ~Separator();

    static constexpr double sampleRate = 44100;

    struct Stems
    {
        juce::AudioBuffer<float> drums, bass, other;    // other includes any vocals
        juce::AudioBuffer<float> vocals;                // also on their own
    };

    /** Stereo (or mono) audio at 44.1 kHz. Blocks for about a fifth of the audio's length.
        progress hears how far it is (0 to 1); returning false stops it with an error. */
    Stems separate (const juce::AudioBuffer<float>&, const std::function<bool (double)>& progress = {});

private:
    const Models& models;
    std::unique_ptr<Ort::Session> session;
};

/** Stems kept on disk, in a folder of their own: drums, bass, music (the rest, without the vocals)
    and vocals, as 24-bit stereo FLAC at the separator's rate. */
inline constexpr const char* stemNames[] = { "drums", "bass", "music", "vocals" };

/** Writes them (into a folder beside, then moved in place, so a folder is whole or not there). */
void saveStems (const Separator::Stems&, const std::filesystem::path& folder);

/** Whether all four are in a folder. */
bool hasStems (const std::filesystem::path& folder);

/** A stretch of kept stems (seconds; to 0 for the end), as the separator gives them: other with
    the vocals in. None unless all four are there and readable. */
std::optional<Separator::Stems> loadStems (const std::filesystem::path& folder, double from = 0, double to = 0);

} // namespace gk::listening
