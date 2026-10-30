# Quartet: four-operator FM with a chord memory and an analogue layer, for house chords and
# techno stabs. A VST3 the app bundles, like Triad.
gk_add_plugin(Quartet SYNTH
    CODE Qrt4
    BUNDLE_ID dev.groovekit.quartet
    SOURCES
        Source/Parameters.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/Presets.cpp
        Source/Synth.cpp)
