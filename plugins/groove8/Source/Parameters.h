#pragma once

#include "Sequencer.h"

/** Groove-8: GrooveKit's homage to the analogue rhythm machine of 1980, every voice a model of
    its circuit (resonators struck by a pulse, filtered noise, six square-wave oscillators for
    the metal), with its step sequencer.

    The voices play from these MIDI notes (General MIDI's drum map, fixed):
        36 bass drum          37 rim shot           38 snare (40 too)     39 hand clap
        41 low tom (43)       45 mid tom (47)       48 high tom (50)
        64 low conga          63 mid conga          62 high conga
        75 claves             70 maracas            56 cowbell
        42 closed hat (44)    46 open hat           49 cymbal (51, 57 and 59 too)
    Velocity sets the level; 110 and up is an accent. The sequencer plays a tom or a conga, a
    rim shot or claves, a clap or maracas, as their switches are set. */
namespace groove8
{

/** The sequencer's rows: the voices in panel order, then the accent. */
enum RowIndex { bd, sd, lt, mt, ht, rs, cp, cb, cy, oh, ch, accent, numVoices = accent };

const std::vector<rhythm::Row>& getRows();

namespace id
{
    inline juce::String voice (const char* code, const char* what)  { return juce::String (code) + what; }

    inline const juce::String bdTone = "bdTone", bdDecay = "bdDecay", bdPitch = "bdPitch";
    inline const juce::String sdTone = "sdTone", sdSnappy = "sdSnappy";
    inline const juce::String cyTone = "cyTone", cyDecay = "cyDecay", ohDecay = "ohDecay";
    inline const juce::String accent = "accent", drive = "drive", gain = "gain";
}

/** Each voice's level, pan and (for the toms, rim shot and clap) its switch: "Level", "Pan",
    "Tuning", "Switch" after the row's code. */
inline const juce::StringArray tomSwitchNames { "Tom", "Conga" };
inline const juce::StringArray rimSwitchNames { "Rim Shot", "Claves" };
inline const juce::StringArray clapSwitchNames { "Hand Clap", "Maracas" };

const std::vector<rhythm::FactoryPattern>& getPatterns();

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace groove8
