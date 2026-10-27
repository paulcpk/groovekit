#include "Clap.h"

#include "Ort.h"

#include "model/JsonIO.h"

#include <juce_dsp/juce_dsp.h>

#include <CoreFoundation/CoreFoundation.h>
#include <fstream>

namespace gk::listening
{

float similarity (const Embedding& a, const Embedding& b)
{
    float sum = 0;

    for (size_t i = 0; i < a.size(); ++i)
        sum += a[i] * b[i];

    return sum;
}

Embedding average (const std::vector<Embedding>& embeddings)
{
    Embedding mean {};

    for (auto& e : embeddings)
        for (size_t i = 0; i < mean.size(); ++i)
            mean[i] += e[i];

    float length = 0;

    for (auto v : mean)
        length += v * v;

    if (length > 0)
        for (auto& v : mean)
            v /= std::sqrt (length);

    return mean;
}

//==============================================================================
namespace
{
    constexpr int fftSize = 1024, hop = 480, numBands = 64, numFrames = 1001;
    constexpr size_t windowSamples = 480000;

    double hzToMel (double hz)  { return hz < 1000 ? 3.0 * hz / 200.0 : 15.0 + 27.0 * std::log (hz / 1000.0) / std::log (6.4); }
    double melToHz (double mel) { return mel < 15 ? 200.0 * mel / 3.0 : 1000.0 * std::exp (std::log (6.4) * (mel - 15.0) / 27.0); }

    /** Triangular slaney filters with slaney (area) normalisation, as transformers makes them:
        each band's first FFT bin and its weights from there. */
    struct MelBand
    {
        size_t firstBin = 0;
        std::vector<float> weights;
    };

    const std::vector<MelBand>& melFilters()
    {
        static const auto filters = []
        {
            constexpr int numBins = fftSize / 2 + 1;
            std::array<double, numBands + 2> edges;
            const auto low = hzToMel (50), high = hzToMel (14000);

            for (int i = 0; i < numBands + 2; ++i)
                edges[static_cast<size_t> (i)] = melToHz (low + (high - low) * i / (numBands + 1));

            std::vector<MelBand> bands (numBands);

            for (size_t band = 0; band < numBands; ++band)
            {
                const auto norm = 2.0 / (edges[band + 2] - edges[band]);

                for (int bin = 0; bin < numBins; ++bin)
                {
                    const auto hz = 24000.0 * bin / (numBins - 1);
                    const auto down = (hz - edges[band]) / (edges[band + 1] - edges[band]);
                    const auto up = (edges[band + 2] - hz) / (edges[band + 2] - edges[band + 1]);
                    const auto weight = std::max (0.0, std::min (down, up)) * norm;

                    if (weight > 0)
                    {
                        if (bands[band].weights.empty())
                            bands[band].firstBin = static_cast<size_t> (bin);

                        bands[band].weights.resize (static_cast<size_t> (bin) - bands[band].firstBin + 1, 0.0f);
                        bands[band].weights.back() = static_cast<float> (weight);
                    }
                }
            }

            return bands;
        }();

        return filters;
    }
}

std::vector<float> clapFeatures (const float* samples, size_t numSamples)
{
    // Repeat a short sound as many whole times as fit, then pad with silence.
    std::vector<float> wave (windowSamples, 0.0f);

    if (numSamples >= windowSamples)
        std::copy (samples, samples + windowSamples, wave.begin());
    else if (numSamples > 0)
        for (size_t i = 0, repeats = windowSamples / numSamples; i < repeats * numSamples; ++i)
            wave[i] = samples[i % numSamples];

    // Centred frames: the signal is mirrored at both ends.
    auto at = [&] (int i)
    {
        constexpr int n = static_cast<int> (windowSamples);
        return wave[static_cast<size_t> (i < 0 ? -i : i >= n ? 2 * (n - 1) - i : i)];
    };

    std::array<float, fftSize> window;

    for (int i = 0; i < fftSize; ++i)
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / fftSize);

    juce::dsp::FFT fft (10);
    std::vector<float> data (2 * fftSize);
    std::vector<float> features (static_cast<size_t> (numFrames * numBands));
    auto& filters = melFilters();

    for (int frame = 0; frame < numFrames; ++frame)
    {
        std::fill (data.begin(), data.end(), 0.0f);

        for (int i = 0; i < fftSize; ++i)
            data[static_cast<size_t> (i)] = at (frame * hop - fftSize / 2 + i) * window[static_cast<size_t> (i)];

        fft.performFrequencyOnlyForwardTransform (data.data(), true);

        for (size_t band = 0; band < numBands; ++band)
        {
            double mel = 0;
            auto& f = filters[band];

            for (size_t i = 0; i < f.weights.size(); ++i)
            {
                const double magnitude = data[f.firstBin + i];
                mel += magnitude * magnitude * f.weights[i];
            }

            features[static_cast<size_t> (frame) * numBands + band] = static_cast<float> (10.0 * std::log10 (std::max (mel, 1e-10)));
        }
    }

    return features;
}

//==============================================================================
namespace
{
    void appendUtf8 (std::string& s, juce::juce_wchar c)
    {
        s += juce::String::charToString (c).toStdString();
    }

    // Unicode's letters and numbers, as the tokenizer's \p{L} and \p{N} mean them.
    bool isLetter (juce::juce_wchar c)
    {
        static const auto letters = CFCharacterSetGetPredefined (kCFCharacterSetLetter);
        return CFCharacterSetIsLongCharacterMember (letters, static_cast<UTF32Char> (c));
    }

    bool isNumber (juce::juce_wchar c)
    {
        static const auto numbers = CFCharacterSetGetPredefined (kCFCharacterSetDecimalDigit);
        return CFCharacterSetIsLongCharacterMember (numbers, static_cast<UTF32Char> (c));
    }

    bool isSpace (juce::juce_wchar c)   { return juce::CharacterFunctions::isWhitespace (c); }

    /** GPT-2's pre-tokenisation: contractions, then runs of letters, numbers or other symbols,
        each with at most one space before it, and whitespace. */
    std::vector<std::string> splitWords (const std::string& text)
    {
        std::vector<juce::juce_wchar> chars;
        const auto utf8 = juce::String::fromUTF8 (text.data(), static_cast<int> (text.size()));

        for (auto p = utf8.getCharPointer(); ! p.isEmpty();)
            chars.push_back (p.getAndAdvance());

        std::vector<std::string> words;
        const auto n = chars.size();
        size_t i = 0;

        auto run = [&] (size_t from, auto&& belongs)
        {
            auto end = from;

            while (end < n && belongs (chars[end]))
                ++end;

            return end;
        };

        auto emit = [&] (size_t from, size_t to)
        {
            std::string s;

            for (auto k = from; k < to; ++k)
                appendUtf8 (s, chars[k]);

            words.push_back (s);
            i = to;
        };

        while (i < n)
        {
            const auto c = chars[i];

            if (c == '\'')
            {
                static const char* contractions[] = { "s", "t", "re", "ve", "m", "ll", "d" };
                bool matched = false;

                for (auto* suffix : contractions)
                {
                    const auto length = std::strlen (suffix);
                    bool same = i + length < n;

                    for (size_t k = 0; same && k < length; ++k)
                        same = chars[i + 1 + k] == static_cast<juce::juce_wchar> (suffix[k]);

                    if (same)
                    {
                        emit (i, i + 1 + length);
                        matched = true;
                        break;
                    }
                }

                if (matched)
                    continue;
            }

            const auto start = i;
            const auto body = (c == ' ' && i + 1 < n && ! isSpace (chars[i + 1])) ? i + 1 : i;
            const auto first = chars[body];

            if (isLetter (first))
                emit (start, run (body, isLetter));
            else if (isNumber (first))
                emit (start, run (body, isNumber));
            else if (! isSpace (first))
                emit (start, run (body, [] (juce::juce_wchar ch) { return ! isSpace (ch) && ! isLetter (ch) && ! isNumber (ch); }));
            else
            {
                // Whitespace: all but its last character before a word (a last space then
                // starts the word), otherwise all of it.
                const auto end = run (i, isSpace);
                emit (i, end < n && end - i > 1 ? end - 1 : end);
            }
        }

        return words;
    }
}

Tokenizer::Tokenizer (const std::filesystem::path& vocabularyFile, const std::filesystem::path& mergesFile)
{
    std::ifstream vocabIn (vocabularyFile);

    if (! vocabIn)
        throw std::runtime_error ("Can't read " + vocabularyFile.string());

    // Not the engine's ordered JSON: its lookups are linear, and the vocabulary has 50,000 words.
    const auto vocabularyJson = nlohmann::json::parse (vocabIn);

    for (auto& [token, id] : vocabularyJson.items())
        vocabulary.emplace (token, id.get<int64_t>());

    std::ifstream mergesIn (mergesFile);

    if (! mergesIn)
        throw std::runtime_error ("Can't read " + mergesFile.string());

    std::string line;
    int rank = 0;

    while (std::getline (mergesIn, line))
    {
        if (line.starts_with ("#version") || line.empty())
            continue;

        if (auto space = line.find (' '); space != std::string::npos)
            mergeRanks.emplace (std::make_pair (line.substr (0, space), line.substr (space + 1)), rank++);
    }

    // Bytes as printable characters: the printable ones as themselves, the rest from U+0100 on.
    std::array<bool, 256> printable {};

    for (int b = '!'; b <= '~'; ++b)   printable[static_cast<size_t> (b)] = true;
    for (int b = 0xA1; b <= 0xAC; ++b) printable[static_cast<size_t> (b)] = true;
    for (int b = 0xAE; b <= 0xFF; ++b) printable[static_cast<size_t> (b)] = true;

    for (int b = 0, extra = 0; b < 256; ++b)
    {
        std::string s;
        appendUtf8 (s, static_cast<juce::juce_wchar> (printable[static_cast<size_t> (b)] ? b : 256 + extra++));
        byteSymbols[static_cast<size_t> (b)] = s;
    }
}

std::vector<std::string> Tokenizer::bytePairs (const std::string& word) const
{
    std::vector<std::string> parts;

    for (unsigned char b : word)
        parts.push_back (byteSymbols[b]);

    // Merge the best-ranked adjacent pair until none is left to merge.
    while (parts.size() > 1)
    {
        int best = std::numeric_limits<int>::max();
        size_t at = 0;

        for (size_t i = 0; i + 1 < parts.size(); ++i)
            if (auto it = mergeRanks.find ({ parts[i], parts[i + 1] }); it != mergeRanks.end() && it->second < best)
                best = it->second, at = i;

        if (best == std::numeric_limits<int>::max())
            break;

        const auto first = parts[at], second = parts[at + 1];
        std::vector<std::string> merged;

        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i + 1 < parts.size() && parts[i] == first && parts[i + 1] == second)
            {
                merged.push_back (first + second);
                ++i;
            }
            else
                merged.push_back (parts[i]);
        }

        parts = std::move (merged);
    }

    return parts;
}

std::vector<int64_t> Tokenizer::encode (const std::string& text, size_t maxTokens) const
{
    constexpr int64_t start = 0, end = 2, unknown = 3;
    std::vector<int64_t> ids { start };

    for (auto& word : splitWords (text))
    {
        for (auto& part : bytePairs (word))
        {
            auto it = vocabulary.find (part);
            ids.push_back (it != vocabulary.end() ? it->second : unknown);
        }
    }

    if (ids.size() > maxTokens - 1)
        ids.resize (maxTokens - 1);

    ids.push_back (end);
    return ids;
}

//==============================================================================
Clap::Clap (const Models& m) : models (m) {}
Clap::~Clap() = default;

void Clap::load()
{
    std::lock_guard lock (loadMutex);

    if (audio != nullptr)
        return;

    models.loadRuntime();
    const auto threads = std::clamp (static_cast<int> (std::thread::hardware_concurrency()) / 2, 1, 4);
    tokenizer = std::make_unique<Tokenizer> (models.clapVocabulary(), models.clapMerges());
    text = std::make_unique<Ort::Session> (openSession (models.clapText(), threads));
    audio = std::make_unique<Ort::Session> (openSession (models.clapAudio(), threads));
}

namespace
{
    Embedding normalised (const float* values)
    {
        Embedding e;
        std::copy (values, values + e.size(), e.begin());
        return average ({ e });
    }
}

Embedding Clap::embedAudio (const float* samples, size_t numSamples)
{
    load();

    auto features = clapFeatures (samples, numSamples);
    const std::array<int64_t, 4> shape { 1, 1, numFrames, numBands };
    auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    auto input = Ort::Value::CreateTensor<float> (memory, features.data(), features.size(), shape.data(), shape.size());

    const char* inputNames[] = { "input_features" };
    const char* outputNames[] = { "audio_embeds" };
    auto outputs = audio->Run (Ort::RunOptions { nullptr }, inputNames, &input, 1, outputNames, 1);
    return normalised (outputs.front().GetTensorData<float>());
}

Embedding Clap::embedText (const std::string& s)
{
    load();

    auto ids = tokenizer->encode (s);
    const std::array<int64_t, 2> shape { 1, static_cast<int64_t> (ids.size()) };
    auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    auto input = Ort::Value::CreateTensor<int64_t> (memory, ids.data(), ids.size(), shape.data(), shape.size());

    const char* inputNames[] = { "input_ids" };
    const char* outputNames[] = { "text_embeds" };
    auto outputs = text->Run (Ort::RunOptions { nullptr }, inputNames, &input, 1, outputNames, 1);
    return normalised (outputs.front().GetTensorData<float>());
}

//==============================================================================
std::vector<float> resampleMono (const juce::AudioBuffer<float>& buffer, double fromRate, double toRate, int startSample, int numSamples)
{
    if (numSamples < 0)
        numSamples = buffer.getNumSamples() - startSample;

    std::vector<float> mono (static_cast<size_t> (std::max (0, numSamples)), 0.0f);
    const auto channels = buffer.getNumChannels();

    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t> (i)] += buffer.getSample (c, startSample + i) / static_cast<float> (channels);

    if (std::abs (fromRate - toRate) < 0.5 || mono.empty())
        return mono;

    const auto ratio = fromRate / toRate;
    std::vector<float> out (static_cast<size_t> (std::floor (static_cast<double> (mono.size()) / ratio)));

    // The interpolator reads ahead: give it some silence past the end. Lagrange is plenty for
    // listening (CLAP hears up to 14 kHz) and far cheaper than a windowed sinc.
    mono.resize (mono.size() + 8, 0.0f);
    juce::LagrangeInterpolator interpolator;
    interpolator.process (ratio, mono.data(), out.data(), static_cast<int> (out.size()));
    return out;
}

} // namespace gk::listening
