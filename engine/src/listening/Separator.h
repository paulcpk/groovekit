#pragma once

#include "Models.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <memory>

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

} // namespace gk::listening
