#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <filesystem>
#include <string>

namespace gk
{

/** How a rendered file is written. */
struct EncodeOptions
{
    std::string format = "wav";     // wav, flac or mp3
    int bitDepth = 24;              // wav: 16, 24 or 32 (float); flac: 16 or 24
    int bitrate = 320;              // mp3, kbit/s: 128 to 320, constant
    bool dither = true;             // triangular dither when going to 16 bits
};

/** The extension a format's files have: "wav", "flac", "mp3". Throws for an unknown format. */
std::string extensionFor (const std::string& format);

/** Writes stereo (or mono) audio to a file in a format. Throws on failure. */
void writeAudio (const juce::AudioBuffer<float>&, double sampleRate, const std::filesystem::path&, const EncodeOptions&);

} // namespace gk
