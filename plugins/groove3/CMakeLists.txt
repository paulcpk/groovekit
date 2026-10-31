# Groove-3: GrooveKit's homage to the bass line synthesizer of 1981, its circuits modelled, with
# the controls its best-known modifications added and a step section of its own. A VST3 the app
# bundles.
gk_add_plugin(Groove3 SYNTH
    PRODUCT "Groove-3"
    CODE Grv3
    BUNDLE_ID dev.groovekit.groove3
    SOURCES
        Source/Bassline.cpp
        Source/Line.cpp
        Source/LineLink.cpp
        Source/Parameters.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/Presets.cpp)

# The machines' panel look (knobs, lights, logo) and their DSP pieces.
gk_link_rhythm(Groove3)
