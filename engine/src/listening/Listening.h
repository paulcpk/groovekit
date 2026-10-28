#pragma once

#include "NoteTranscriber.h"
#include "SoundIndex.h"
#include "Styles.h"
#include "Taste.h"
#include "TasteModel.h"

namespace gk::listening
{

/** GrooveKit's ears, for the agent: the listening models, what the library's samples sound
    like, the user's styles and their verdicts, and the searches and comparisons built on them.
    One per engine process, shared by every project it opens. Thread-safe. */
class Listening
{
public:
    struct Files
    {
        std::filesystem::path models = Models::getDefaultFolder();
        std::filesystem::path soundIndex = SoundIndex::getDefaultFile();
        std::filesystem::path styles = Styles::getDefaultFolder();
        std::filesystem::path taste = Taste::getDefaultFile();

        /** The song maps; beside the styles folder unless given (the app's are in
            MapStore::getDefaultFolder()). */
        std::filesystem::path maps;
    };

    Listening (library::SampleLibrary&, Files);
    ~Listening();

    Models& getModels()         { return models; }
    SoundIndex& getIndex()      { return index; }
    Styles& getStyles()         { return styles; }
    Taste& getTaste()           { return taste; }
    Clap& getClap()             { return clap; }

    /** Called from any thread whenever a download, indexing or hearing moves on, or a map changes. */
    void setObserver (std::function<void (const model::Json& status)>);

    /** Models, index and hearing progress. */
    model::Json getStatus() const;

    /** Tells the observer that maps or the user's choices changed. */
    void notifyChanged()        { notify(); }

    /** Downloads the models; when they're in, indexes the library. */
    void download();

    /** Embeds new library samples, after any running library scan, if the models are in. */
    void startIndexing();

    /** Hears a folder of reference tracks as a style: queues its new and changed tracks. Returns
        the style's name. */
    std::string addStyle (const std::filesystem::path& folder, const std::string& name);

    //==============================================================================
    struct SoundQuery
    {
        std::string sound;                          // words: what it should sound like
        std::filesystem::path like;                 // a sample it should sound like
        std::string style;                          // a style it should fit
        std::optional<Role> role;                   // the part it plays, if not its category's
    };

    /** Library samples passing the filters, ranked by how they sound: model::Json results as
        library.search gives them, plus "fit" (to the style: 0 an average library sample, 100
        as close as the style's own tracks are to each other), "match" (to the words or the
        sample: the share of the library it beats, 0-100), "rating" and "sounds" (words). */
    model::Json search (const library::SampleQuery& filters, const SoundQuery&);

    /** How well a sound fits a style in a role: 0 like an average library sample in that role,
        100 as close as the style's own tracks are to each other. */
    double fitPercent (const StyleProfile&, Role, const Embedding&);

    /** Descriptive words for a sound, the most distinctive first. */
    std::vector<std::string> describe (const Embedding&, size_t count = 4);

    /** A style for the agent: its measurements and what each part sounds like. */
    model::Json describeStyle (const StyleProfile&);

    /** How a rendered song compares with a style. The render is heard as the style's tracks
        were (split into drums, bass and the rest), then compared part by part: fit and words,
        levels against the mix, tempo, swing and density, loudness and balance, with notes on
        what differs most. Blocks for several seconds. */
    model::Json compare (const StyleProfile&, const std::filesystem::path& render, double songTempo, double songSwing);

    /** The notes in a stretch of one of the styles' reference tracks: track is part of its file
        name (searched in style, or every style); from and length in seconds (from defaults to
        a stretch two fifths in). part is music (everything but the drums and bass: leads,
        chords, vocals) or bass. Downloads the note model the first time, then blocks for a few
        seconds. Throws std::runtime_error naming the tracks if none matches. */
    struct ReferenceNotes
    {
        std::string track;
        double tempo = 0, from = 0, length = 0;
        std::vector<music::HeardNote> notes;
    };

    ReferenceNotes hearNotes (const std::string& style, const std::string& track, std::optional<double> from, double length, Role part);

private:
    library::SampleLibrary& library;
    Models models;
    Clap clap;
    SoundIndex index;
    Styles styles;
    Taste taste;

    mutable std::mutex observerMutex;
    std::function<void (const model::Json&)> observer;
    std::mutex starterMutex;
    std::thread indexStarter;
    std::mutex separatorMutex;
    std::unique_ptr<Separator> separator;
    std::unique_ptr<NoteTranscriber> noteTranscriber;
    std::atomic<bool> starting { false }, stopping { false };

    // Library-wide statistics, rebuilt when the index changes.
    struct Words
    {
        std::vector<std::string> words;
        std::vector<Embedding> embeddings;
        std::vector<float> mean, deviation;     // of each word's similarity over the library
        size_t indexSize = 0;
    };

    std::mutex statsMutex;
    Words words;
    struct Baseline
    {
        size_t indexSize = 0, tracks = 0;
        float median = 0;
    };

    std::map<std::pair<std::string, Role>, Baseline> baselines; // by style and role

    // The taste learned from the verdicts that apply to a style (or to any style: ""), retrained
    // when they change.
    struct LearnedTaste
    {
        std::string verdicts;
        size_t indexSize = 0;
        std::optional<TasteModel> model;
    };

    std::map<std::string, LearnedTaste> tastes;

    void notify();
    Words& getWords();      // with statsMutex held
    float baseline (const StyleProfile&, Role);
    std::optional<TasteModel> learnedTaste (const std::string& style, const std::vector<std::pair<bool, Embedding>>& verdicts,
                                            const std::string& signature);
    std::vector<std::pair<std::string, Embedding>> indexSnapshot() const;
};

} // namespace gk::listening
