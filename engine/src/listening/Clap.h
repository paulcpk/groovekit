#pragma once

#include "Models.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ort { struct Session; }

namespace gk::listening
{

/** Where CLAP puts a sound or a description: sounds that feel alike, and words that describe
    them, land close together. Unit length, so the dot product is the cosine similarity. */
using Embedding = std::array<float, 512>;

float similarity (const Embedding&, const Embedding&);

/** The mean of some embeddings, back at unit length. */
Embedding average (const std::vector<Embedding>&);

/** CLAP's input, as its feature extractor makes it: the log-power mel spectrogram (64 slaney
    bands, 50 Hz to 14 kHz, dB) of 10 s of 48 kHz mono in 1001 frames. Shorter sounds repeat
    to fill the 10 s (the rest is silence), longer ones are cut. Frames are rows. */
std::vector<float> clapFeatures (const float* samples, size_t numSamples);

/** RoBERTa's byte-level BPE, which CLAP's text side reads. */
class Tokenizer
{
public:
    Tokenizer (const std::filesystem::path& vocabulary, const std::filesystem::path& merges);

    /** Token ids with the start and end tokens, at most maxTokens in all. */
    std::vector<int64_t> encode (const std::string& text, size_t maxTokens = 77) const;

private:
    std::unordered_map<std::string, int64_t> vocabulary;
    std::map<std::pair<std::string, std::string>, int> mergeRanks;
    std::array<std::string, 256> byteSymbols;

    std::vector<std::string> bytePairs (const std::string& word) const;
};

/** CLAP on this Mac. Loads its models on first use; safe to use from several threads. */
class Clap
{
public:
    explicit Clap (const Models&);
    ~Clap();

    static constexpr double sampleRate = 48000;
    static constexpr double windowSeconds = 10;

    /** 48 kHz mono; only the first 10 s count. */
    Embedding embedAudio (const float* samples, size_t numSamples);
    Embedding embedText (const std::string&);

private:
    const Models& models;
    std::mutex loadMutex;
    std::unique_ptr<Ort::Session> audio, text;
    std::unique_ptr<Tokenizer> tokenizer;

    void load();
};

/** One channel (or the average of all of them) at another rate. */
std::vector<float> resampleMono (const juce::AudioBuffer<float>&, double fromRate, double toRate, int startSample = 0, int numSamples = -1);

} // namespace gk::listening
