#pragma once

#include "SampleFeatures.h"

#include <filesystem>

namespace gk::library
{

/** What a sample is, from its name, its folders and, when those don't say, its sound. */
struct Classification
{
    std::string category;           // kick, snare, clap, hat, cymbal, tom, perc, bass, synth, vocal, fx, drums, other
    std::string categorySource;     // "name" or "sound"
    bool loop = false;
    std::optional<double> tempo;    // from the name, else estimated for loops
    std::optional<std::string> key; // from the name, e.g. "A minor"
    std::vector<std::string> tags;  // lowercase words from the name and folders
};

/** Lowercase words of a path, split on separators, digits and camelCase. */
std::vector<std::string> tokenize (const std::string&);

Classification classify (const std::filesystem::path&, const SampleFeatures&);

/** Parses key names like "Am", "C#min", "Fmaj", "Bb" into "A minor", "C# minor", "F major", "Bb major". */
std::optional<std::string> parseKey (const std::string& token);

} // namespace gk::library
