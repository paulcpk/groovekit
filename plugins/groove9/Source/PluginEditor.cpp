#include "PluginEditor.h"

namespace groove9
{

namespace
{
    rhythm::Style style()
    {
        rhythm::Style s;
        s.name = "Groove-9";
        s.tagline = juce::String (juce::CharPointer_UTF8 ("Rhythm machine  \xc2\xb7  GrooveKit"));
        s.panelTop = juce::Colour (0xff2c2d31);
        s.panelBottom = juce::Colour (0xff1c1d20);
        s.strip = juce::Colour (0xff111214);
        s.well = juce::Colour (0xff17181b);
        s.wellEdge = juce::Colour (0xff36383d);
        s.ink = juce::Colour (0xffe8e8e3);
        s.inkDim = juce::Colour (0xff8d8f95);
        s.accent = juce::Colour (0xffc4ef3f);
        s.led = juce::Colour (0xffd2ff4a);
        s.keyTop = juce::Colour (0xff4b4d53);
        s.keyBottom = juce::Colour (0xff303236);
        s.keyEdge = juce::Colour (0xff0d0e10);
        s.keyInk = juce::Colour (0xffe8e8e3);
        s.keyMark = juce::Colour (0xffc4ef3f);
        s.knobSkirt = juce::Colour (0xff141517);
        s.knobCap = juce::Colour (0xff2b2d31);
        s.knobCapLevel = juce::Colour (0xff35373c);
        s.knobPointer = juce::Colour (0xffc4ef3f);
        s.brushed = true;
        return s;
    }

    rhythm::KnobSpec knob (const juce::String& paramId, const juce::String& label)  { return { paramId, label }; }
    rhythm::KnobSpec small (const juce::String& paramId, const juce::String& label) { return { paramId, label, true }; }

    rhythm::PanelSpec panel()
    {
        rhythm::PanelSpec p;
        p.style = style();
        p.columns = {
            { bd, { "Bass Drum" }, { { knob ("bdLevel", "Level") }, { knob (id::bdTune, "Tune"), knob (id::bdAttack, "Attack") }, { knob (id::bdDecay, "Decay") } }, { small ("bdPan", "Pan") }, {} },
            { sd, { "Snare Drum" }, { { knob ("sdLevel", "Level") }, { knob (id::sdTune, "Tune"), knob (id::sdTone, "Tone") }, { knob (id::sdSnappy, "Snappy") } }, { small ("sdPan", "Pan") }, {} },
            { lt, { "Low Tom" }, { { knob ("ltLevel", "Level") }, { knob ("ltTune", "Tune") }, { knob ("ltDecay", "Decay") } }, { small ("ltPan", "Pan") }, {} },
            { mt, { "Mid Tom" }, { { knob ("mtLevel", "Level") }, { knob ("mtTune", "Tune") }, { knob ("mtDecay", "Decay") } }, { small ("mtPan", "Pan") }, {} },
            { ht, { "High Tom" }, { { knob ("htLevel", "Level") }, { knob ("htTune", "Tune") }, { knob ("htDecay", "Decay") } }, { small ("htPan", "Pan") }, {} },
            { rs, { "Rim Shot" }, { { knob ("rsLevel", "Level") } }, { small ("rsPan", "Pan") }, {} },
            { cp, { "Hand Clap" }, { { knob ("cpLevel", "Level") } }, { small ("cpPan", "Pan") }, {} },
            { ch, { "Closed Hat" }, { { knob ("chLevel", "Level") }, {}, { knob (id::chDecay, "Decay") } }, { small ("chPan", "Pan") }, {} },
            { oh, { "Open Hat" }, { { knob ("ohLevel", "Level") }, {}, { knob (id::ohDecay, "Decay") } }, { small ("ohPan", "Pan") }, {} },
            { cr, { "Crash" }, { { knob ("crLevel", "Level") }, { knob (id::crTune, "Tune") } }, { small ("crPan", "Pan") }, {} },
            { rd, { "Ride" }, { { knob ("rdLevel", "Level") }, { knob (id::rdTune, "Tune") } }, { small ("rdPan", "Pan") }, {} },
        };
        p.master = { knob (id::accent, "Total Accent"), knob (id::gain, "Volume"), small (id::drive, "Drive") };
        return p;
    }
}

Editor::Editor (Processor& p) : MachineEditor (p, panel())
{
}

} // namespace groove9
