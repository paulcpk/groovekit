#include "PluginEditor.h"

namespace groove8
{

namespace
{
    rhythm::Style style()
    {
        rhythm::Style s;
        s.name = "Groove-8";
        s.tagline = juce::String (juce::CharPointer_UTF8 ("Analogue rhythm machine  \xc2\xb7  GrooveKit"));
        s.panelTop = juce::Colour (0xff242d3d);
        s.panelBottom = juce::Colour (0xff171d28);
        s.strip = juce::Colour (0xff0e121a);
        s.well = juce::Colour (0xff141a24);
        s.wellEdge = juce::Colour (0xff2e3849);
        s.ink = juce::Colour (0xffece5d3);
        s.inkDim = juce::Colour (0xff8a95a8);
        s.accent = juce::Colour (0xff5fd6c5);
        s.led = juce::Colour (0xff6dfbe4);
        s.keyTop = juce::Colour (0xfff0eadb);
        s.keyBottom = juce::Colour (0xffcbc3ae);
        s.keyEdge = juce::Colour (0xff6f6858);
        s.keyInk = juce::Colour (0xff27303f);
        s.keyMark = juce::Colour (0xff3f6f86);
        s.knobSkirt = juce::Colour (0xff1c212a);
        s.knobCap = juce::Colour (0xff3b4556);
        s.knobCapLevel = juce::Colour (0xff2e7d76);
        s.knobPointer = juce::Colour (0xfff2ebd9);
        return s;
    }

    rhythm::KnobSpec knob (const juce::String& paramId, const juce::String& label)  { return { paramId, label }; }
    rhythm::KnobSpec small (const juce::String& paramId, const juce::String& label) { return { paramId, label, true }; }

    rhythm::PanelSpec panel()
    {
        rhythm::PanelSpec p;
        p.style = style();
        p.columns = {
            { bd, { "Bass Drum" }, { { knob ("bdLevel", "Level") }, { knob (id::bdTone, "Tone") }, { knob (id::bdDecay, "Decay") } }, { small ("bdPan", "Pan"), small (id::bdPitch, "Pitch") }, {} },
            { sd, { "Snare Drum" }, { { knob ("sdLevel", "Level") }, { knob (id::sdTone, "Tone") }, { knob (id::sdSnappy, "Snappy") } }, { small ("sdPan", "Pan") }, {} },
            { lt, { "Low Tom", "Low Conga" }, { { knob ("ltLevel", "Level") }, { knob ("ltTuning", "Tuning") } }, { small ("ltPan", "Pan") }, "ltSwitch" },
            { mt, { "Mid Tom", "Mid Conga" }, { { knob ("mtLevel", "Level") }, { knob ("mtTuning", "Tuning") } }, { small ("mtPan", "Pan") }, "mtSwitch" },
            { ht, { "High Tom", "High Conga" }, { { knob ("htLevel", "Level") }, { knob ("htTuning", "Tuning") } }, { small ("htPan", "Pan") }, "htSwitch" },
            { rs, { "Rim Shot", "Claves" }, { { knob ("rsLevel", "Level") } }, { small ("rsPan", "Pan") }, "rsSwitch" },
            { cp, { "Hand Clap", "Maracas" }, { { knob ("cpLevel", "Level") } }, { small ("cpPan", "Pan") }, "cpSwitch" },
            { cb, { "Cowbell" }, { { knob ("cbLevel", "Level") } }, { small ("cbPan", "Pan") }, {} },
            { cy, { "Cymbal" }, { { knob ("cyLevel", "Level") }, { knob (id::cyTone, "Tone") }, { knob (id::cyDecay, "Decay") } }, { small ("cyPan", "Pan") }, {} },
            { oh, { "Open Hat" }, { { knob ("ohLevel", "Level") }, {}, { knob (id::ohDecay, "Decay") } }, { small ("ohPan", "Pan") }, {} },
            { ch, { "Closed Hat" }, { { knob ("chLevel", "Level") } }, { small ("chPan", "Pan") }, {} },
        };
        p.master = { knob (id::accent, "Accent"), knob (id::gain, "Volume"), small (id::drive, "Drive") };
        return p;
    }
}

Editor::Editor (Processor& p) : MachineEditor (p, panel())
{
}

} // namespace groove8
