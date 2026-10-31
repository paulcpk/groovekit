# Kessel: a kick machine for hard kicks (hardstyle, hardcore, hard techno), with a rumble. A VST3
# the app bundles, like Triad.
gk_add_plugin(Kessel SYNTH
    CODE Ksl1
    BUNDLE_ID dev.groovekit.kessel
    SOURCES
        Source/Kick.cpp
        Source/Parameters.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/Presets.cpp)
