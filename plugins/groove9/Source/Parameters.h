#pragma once

#include "Sequencer.h"

/** Groove-9: GrooveKit's homage to the rhythm machine of 1983, its drums modelled circuits (an
    oscillator swept down for the bass drum, resonators and filtered noise for the snare, toms,
    rim shot and clap) and its hats and cymbals recordings, as the original's were, with its step
    sequencer, flams and shuffle.

    The voices play from these MIDI notes (General MIDI's drum map, fixed):
        36 bass drum (35)     37 rim shot           38 snare (40)         39 hand clap
        41 low tom (43)       45 mid tom (47)       48 high tom (50)
        42 closed hat (44)    46 open hat           49 crash (57)         51 ride (59)
    Velocity sets the level; 110 and up is an accent. */
namespace groove9
{

/** The sequencer's rows: the voices in panel order, then the accent and the flam. */
enum RowIndex { bd, sd, lt, mt, ht, rs, cp, ch, oh, cr, rd, accent, flam, numVoices = accent };

const std::vector<rhythm::Row>& getRows();

namespace id
{
    inline juce::String voice (const char* code, const char* what)  { return juce::String (code) + what; }

    inline const juce::String bdTune = "bdTune", bdAttack = "bdAttack", bdDecay = "bdDecay";
    inline const juce::String sdTune = "sdTune", sdTone = "sdTone", sdSnappy = "sdSnappy";
    inline const juce::String chDecay = "chDecay", ohDecay = "ohDecay", crTune = "crTune", rdTune = "rdTune";
    inline const juce::String accent = "accent", drive = "drive", gain = "gain";
}

/** Each voice has "Level" and "Pan" after its row's code; the toms "Tune" and "Decay" too. */
const std::vector<rhythm::FactoryPattern>& getPatterns();

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace groove9
