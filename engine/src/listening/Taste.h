#pragma once

#include "model/JsonIO.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gk::listening
{

/** The user's verdicts on samples: this fits (a style), this doesn't. Kept in a JSON file.
    Thread-safe. */
class Taste
{
public:
    explicit Taste (std::filesystem::path file);

    static std::filesystem::path getDefaultFile();

    struct Rating
    {
        std::string path;
        std::string style;      // empty: any style
        bool fits = true;
        int64_t time = 0;       // ms since 1970
    };

    /** Records a verdict, replacing an earlier one on the same sample and style; without one,
        forgets it. */
    void rate (const std::string& path, const std::string& style, std::optional<bool> fits);

    /** The verdicts that apply to a style: its own and the ones for any style. */
    std::vector<Rating> forStyle (const std::string& style) const;
    std::vector<Rating> all() const;

private:
    const std::filesystem::path file;
    mutable std::mutex mutex;
    std::vector<Rating> ratings;

    void save() const;
};

} // namespace gk::listening
