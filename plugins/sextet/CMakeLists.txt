# Sextet: six morphing oscillators, two filters, three envelopes, three LFOs, a modulation matrix
# and effects, for pads, long chords, trance sounds and raw basslines.
gk_add_plugin(Sextet SYNTH
    CODE Sxt6
    BUNDLE_ID dev.groovekit.sextet
    SOURCES
        Source/Parameters.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/Presets.cpp
        Source/Synth.cpp)
