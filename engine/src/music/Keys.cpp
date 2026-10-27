#include "Keys.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <regex>

namespace gk::music
{

namespace
{
    constexpr int majorSteps[] { 0, 2, 4, 5, 7, 9, 11 };
    constexpr int minorSteps[] { 0, 2, 3, 5, 7, 8, 10 };

    int wrap (int pc)   { return ((pc % 12) + 12) % 12; }

    // Krumhansl-Kessler probe-tone profiles, from the tonic up.
    constexpr float majorProfile[] { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr float minorProfile[] { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float correlation (const std::array<float, 12>& x, const float* profile, int tonic)
    {
        float mx = 0, my = 0;

        for (int i = 0; i < 12; ++i)
        {
            mx += x[static_cast<size_t> (i)];
            my += profile[i];
        }

        mx /= 12;
        my /= 12;
        float sxy = 0, sxx = 0, syy = 0;

        for (int i = 0; i < 12; ++i)
        {
            const auto dx = x[static_cast<size_t> (wrap (tonic + i))] - mx, dy = profile[i] - my;
            sxy += dx * dy;
            sxx += dx * dx;
            syy += dy * dy;
        }

        return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0.0f;
    }
}

bool Key::contains (int pitchClass) const
{
    const auto step = wrap (pitchClass - tonic);
    const auto& steps = minor ? minorSteps : majorSteps;
    return std::find (std::begin (steps), std::end (steps), step) != std::end (steps);
}

int Key::snap (int note) const
{
    for (int d = 0; d <= 6; ++d)
    {
        if (contains (note - d)) return note - d;
        if (contains (note + d)) return note + d;
    }

    return note;
}

std::optional<Key> parseKey (const std::string& text)
{
    static const std::regex pattern (R"(^\s*([a-g])\s*(#|b|sharp|flat)?\s*(m|min|minor|maj|major)?\s*$)", std::regex::icase);
    std::smatch m;

    if (! std::regex_match (text, m, pattern))
        return {};

    static const int naturals[] { 9, 11, 0, 2, 4, 5, 7 }; // a..g
    const auto letter = std::tolower (static_cast<unsigned char> (m[1].str()[0]));
    auto pc = naturals[letter - 'a'];
    const auto accidental = juce::String (m[2].str()).toLowerCase();

    if (accidental == "#" || accidental == "sharp")    ++pc;
    if (accidental == "b" || accidental == "flat")     --pc;

    // "m" alone is minor (as in "Am"); "M" is too, since names rarely mean major by it.
    const auto quality = juce::String (m[3].str()).toLowerCase();
    return Key { wrap (pc), quality == "m" || quality == "min" || quality == "minor" };
}

std::string pitchClassName (int pc)
{
    static const char* names[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[wrap (pc)];
}

std::string keyName (Key k)
{
    return pitchClassName (k.tonic) + (k.minor ? " minor" : " major");
}

int semitonesBetween (Key from, Key to)
{
    auto shift = wrap (to.relativeMajor() - from.relativeMajor());
    return shift > 6 ? shift - 12 : shift;
}

bool compatible (Key a, Key b)
{
    const auto d = wrap (a.relativeMajor() - b.relativeMajor());
    return d == 0 || d == 5 || d == 7;
}

std::optional<KeyEstimate> estimateKey (const std::array<float, 12>& chroma)
{
    if (*std::max_element (chroma.begin(), chroma.end()) <= 0)
        return {};

    struct Candidate { Key key; float r; };
    std::vector<Candidate> all;

    for (int t = 0; t < 12; ++t)
    {
        all.push_back ({ { t, false }, correlation (chroma, majorProfile, t) });
        all.push_back ({ { t, true }, correlation (chroma, minorProfile, t) });
    }

    std::sort (all.begin(), all.end(), [] (auto& a, auto& b) { return a.r > b.r; });

    // The relative key shares every note, so it's no real rival: the margin is over the best
    // key that isn't it.
    const auto& best = all.front();
    auto rival = std::find_if (all.begin() + 1, all.end(), [&] (auto& c) { return c.key.relativeMajor() != best.key.relativeMajor(); });
    const auto margin = rival != all.end() ? best.r - rival->r : best.r;

    // Of keys that fit about as well, the one whose tonic sounds most: profiles alone easily
    // take a loop on its fifth for one in the fifth's key.
    auto key = best.key;

    for (auto& c : all)
        if (c.r >= best.r - 0.08f && chroma[static_cast<size_t> (c.key.tonic)] > chroma[static_cast<size_t> (key.tonic)])
            key = c.key;

    // Major only when the major third clearly sounds over the tonic, more than the minor one;
    // otherwise minor, as most loops with no third to speak of are meant.
    const auto tonicLevel = chroma[static_cast<size_t> (key.tonic)];
    const auto minorThird = chroma[static_cast<size_t> (wrap (key.tonic + 3))], majorThird = chroma[static_cast<size_t> (wrap (key.tonic + 4))];
    key.minor = ! (majorThird > 0.3f * tonicLevel && majorThird > 1.5f * minorThird);

    const auto confidence = std::clamp (best.r, 0.0f, 1.0f) * std::clamp (margin * 6.0f, 0.0f, 1.0f);
    return KeyEstimate { key, confidence };
}

std::array<float, 12> chromaOf (const float* mono, int numSamples, double sampleRate)
{
    std::array<float, 12> chroma {};
    constexpr int order = 13, size = 1 << order;   // 8192: about 5 Hz a bin at 44.1 kHz, enough to tell low notes apart
    juce::dsp::FFT fft (order);
    std::vector<float> window (size), frame (2 * size);

    for (int i = 0; i < size; ++i)
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / size);

    // Every spectral peak of every frame, as a MIDI pitch (fraction included) and a weight.
    struct Peak { double note; float weight; };
    std::vector<Peak> notes;
    const auto hop = size / 2;

    for (int start = 0; start + size <= std::max (numSamples, size); start += hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);

        for (int i = 0; i < size && start + i < numSamples; ++i)
            frame[static_cast<size_t> (i)] = mono[start + i] * window[static_cast<size_t> (i)];

        fft.performFrequencyOnlyForwardTransform (frame.data(), true);

        // Peaks only: a bin louder than its neighbours, so noise spread over every bin counts
        // less. Where between the bins each lies is interpolated.
        struct Bin { double bin; float magnitude; };
        std::vector<Bin> peaks;
        float loudest = 0;
        const auto lowest = static_cast<int> (55.0 * size / sampleRate), highest = static_cast<int> (4000.0 * size / sampleRate);

        for (int b = std::max (2, lowest); b < std::min (size / 2 - 1, highest); ++b)
        {
            const auto a = frame[static_cast<size_t> (b - 1)], m = frame[static_cast<size_t> (b)], c = frame[static_cast<size_t> (b + 1)];

            if (m > a && m >= c)
            {
                const auto denom = a - 2 * m + c;
                peaks.push_back ({ b + (std::abs (denom) > 1.0e-9f ? 0.5 * (a - c) / denom : 0.0), m });
                loudest = std::max (loudest, m);
            }
        }

        for (auto& p : peaks)
        {
            if (p.magnitude < loudest * 0.01f)
                continue;

            // An overtone of a lower note sounding with it (a fifth from the third harmonic, a
            // third from the fifth) is mostly that note's, not a note of its own.
            auto weight = 1.0f;

            for (int h = 2; h <= 5; ++h)
            {
                const auto below = p.bin / h;
                const auto near = std::find_if (peaks.begin(), peaks.end(), [&] (auto& q) { return std::abs (q.bin - below) <= std::max (0.8, below * 0.01); });

                if (near != peaks.end() && near->magnitude > p.magnitude * 0.25f)
                {
                    weight = 0.25f;
                    break;
                }
            }

            // Low notes count more: the root is usually in the bass.
            const auto hz = p.bin * sampleRate / size;
            weight *= std::sqrt (p.magnitude) * static_cast<float> (std::sqrt (440.0 / std::max (55.0, hz)));
            notes.push_back ({ 69.0 + 12.0 * std::log2 (hz / 440.0), weight });
        }

        if (start + size >= numSamples)
            break;
    }

    // How far the sample is tuned from A440: the weighted (circular) mean of how far its peaks
    // sit from the nearest semitone. Notes are counted from there.
    double sx = 0, sy = 0;

    for (auto& n : notes)
    {
        const auto angle = juce::MathConstants<double>::twoPi * (n.note - std::round (n.note));
        sx += n.weight * std::cos (angle);
        sy += n.weight * std::sin (angle);
    }

    const auto tuning = std::atan2 (sy, sx) / juce::MathConstants<double>::twoPi;

    for (auto& n : notes)
    {
        const auto note = n.note - tuning;
        const auto off = std::abs (note - std::round (note));

        // Between two notes: neither.
        if (off < 0.35)
            chroma[static_cast<size_t> (wrap (static_cast<int> (std::lround (note))))] += n.weight * static_cast<float> (1.0 - off / 0.35);
    }

    if (const auto top = *std::max_element (chroma.begin(), chroma.end()); top > 0)
        for (auto& c : chroma)
            c /= top;

    return chroma;
}

} // namespace gk::music
