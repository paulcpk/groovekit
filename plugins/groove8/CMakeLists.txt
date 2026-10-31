# Groove-8: GrooveKit's homage to the analogue rhythm machine of 1980, its voices modelled
# circuits, with its step sequencer. A VST3 the app bundles.
gk_add_plugin(Groove8 SYNTH
    PRODUCT "Groove-8"
    CODE Grv8
    BUNDLE_ID dev.groovekit.groove8
    SOURCES
        Source/Parameters.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/Presets.cpp
        Source/Synth.cpp)

gk_link_rhythm(Groove8)
