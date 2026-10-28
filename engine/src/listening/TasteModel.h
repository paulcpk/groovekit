#pragma once

#include "Clap.h"

#include <optional>
#include <vector>

namespace gk::listening
{

/** What the samples the user said fit have in common, learned from their verdicts: a logistic
    regression over how samples sound (their embeddings), trained on the samples they rated
    against the ones they turned down and, weakly, the library's sounds at large (most of which
    they'd pass over). Gives the chance that the user would say a sample fits. */
class TasteModel
{
public:
    /** Needs two samples that fit; without background (the library), turned-down ones as well.
        Nothing otherwise. */
    static std::optional<TasteModel> train (const std::vector<Embedding>& fits, const std::vector<Embedding>& misfits,
                                            const std::vector<Embedding>& background);

    /** 0-1: how likely the user is to say a sample sounding like this fits. */
    float probability (const Embedding&) const;

    /** How far the model can be trusted: 0 with a couple of verdicts, 1 from about a dozen. */
    float confidence() const    { return confidenceValue; }

private:
    Embedding mean {}, scale {};    // features are standardised against the background
    Embedding weights {};
    float bias = 0, confidenceValue = 0;
};

} // namespace gk::listening
