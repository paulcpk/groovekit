#pragma once

#include "Models.h"
#include "music/Notes.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

namespace Ort { struct Session; }

namespace gk::listening
{

/** Notes in recorded music, chords included, with Spotify's Basic Pitch (Apache 2.0) on this
    Mac. Its model (230 KB) is downloaded next to the listening models the first time. Not
    thread-safe: one transcription at a time. */
class NoteTranscriber
{
public:
    explicit NoteTranscriber (const Models&);
    ~NoteTranscriber();

    std::filesystem::path modelFile() const     { return models.getFolder() / "basic-pitch.onnx"; }

    /** The notes in audio (mono or stereo, any rate), in seconds from its start. Blocks for
        about a tenth of the audio's length. Throws std::runtime_error. */
    std::vector<music::HeardNote> transcribe (const juce::AudioBuffer<float>&, double sampleRate, double minNoteSeconds = 0.128);

    /** Model outputs to notes (Basic Pitch's own post-processing): frames and onsets are
        [frame][88 keys from A0] activations, about 86 frames a second. Public for testing. */
    struct Activations
    {
        std::vector<std::array<float, 88>> frames, onsets;
    };

    static std::vector<music::HeardNote> notesFrom (Activations, const std::vector<double>& frameTimes, int minNoteFrames = 11);

private:
    const Models& models;
    std::unique_ptr<Ort::Session> session;

    void ensureModel();
};

} // namespace gk::listening
