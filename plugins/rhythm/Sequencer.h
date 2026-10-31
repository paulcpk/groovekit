#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

/** What GrooveKit's rhythm machines share: the step sequencer, its patterns, the panel and the
    voices' building blocks. */
namespace rhythm
{

constexpr int numPatterns = 16, numSteps = 16, maxRows = 16;

/** A row of the sequencer: a voice, or the accent or flam row. The code names it in the saved
    state ("bd"), the short name labels its select button ("BD"). */
struct Row
{
    juce::String code, shortName, name;
};

/** A factory pattern: its name, and rows as 16 characters by code, "x" for a step. */
struct FactoryPattern
{
    const char* name;
    std::vector<std::pair<const char*, const char*>> rows;
};

/** The sequencer's parameters, the same on every machine. */
namespace id
{
    inline const juce::String seq = "seq", pattern = "pattern", lastStep = "lastStep", scale = "scale", shuffle = "shuffle", flam = "flam", editRow = "editRow";
}

inline const juce::StringArray scaleNames { "16ths", "8th triplets", "16th triplets", "32nds" };

/** Adds Seq, Pattern, Last Step, Scale, Shuffle, Instrument (the row being edited) and, when
    the machine has it, the Flam time. */
void addSequencerParameters (juce::AudioProcessorValueTreeState::ParameterLayout&, const std::vector<Row>& rows, bool flam);

//==============================================================================
/** Sixteen patterns of sixteen steps for every row, kept as bits so the audio thread reads them
    while the editor writes them. Each pattern has a name. */
class PatternStore
{
public:
    explicit PatternStore (const std::vector<Row>& rows);

    bool get (int pattern, int row, int step) const;
    void set (int pattern, int row, int step, bool on);
    void toggle (int pattern, int row, int step)      { set (pattern, row, step, ! get (pattern, row, step)); }
    uint32_t getRow (int pattern, int row) const;
    void setRow (int pattern, int row, uint32_t bits);

    void clear (int pattern);
    void copy (int from, int to);

    /** The name is kept on the message thread only. */
    juce::String getName (int pattern) const;
    void setName (int pattern, const juce::String&);

    int indexOf (const juce::String& code) const        { return codes.indexOf (code); }

    void load (const std::vector<FactoryPattern>&);
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);

    /** Goes up on every change, so the editor knows to repaint. */
    std::atomic<uint32_t> version { 0 };

    static inline const juce::Identifier treeType { "Patterns" };

private:
    juce::StringArray codes;
    std::array<std::array<std::atomic<uint32_t>, maxRows>, numPatterns> bits {};
    std::array<juce::String, numPatterns> names;

    static bool valid (int pattern, int row)     { return juce::isPositiveAndBelow (pattern, numPatterns) && juce::isPositiveAndBelow (row, maxRows); }
};

//==============================================================================
/** Plays steps in time with the host: step n starts n step lengths after the song's start, so
    the steps stay where they belong when the host loops or jumps. Odd steps come later by the
    shuffle. */
class Sequencer
{
public:
    struct Settings
    {
        int lastStep = 16, scale = 0;
        float shuffle = 0;      // 0-1
    };

    struct Step
    {
        int sample, step;
    };

    /** Step length in quarter notes for a scale. */
    static double stepBeats (int scale);

    /** The steps starting within this block: the block starts at ppq quarter notes into the song. */
    void run (bool playing, double ppq, double bpm, int numSamples, double sampleRate, const Settings&, std::vector<Step>& steps);

    void reset();

    /** The step playing, or -1 when stopped. */
    std::atomic<int> currentStep { -1 };

private:
    int64_t lastStep = std::numeric_limits<int64_t>::min();
    double lastPpq = -1;
};

} // namespace rhythm
