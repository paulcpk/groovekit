# Groove-9: GrooveKit's homage to the rhythm machine of 1983, its drums modelled circuits and its
# hats and cymbals recordings (CC0, see Samples/LICENSE.txt), with its step sequencer. A VST3 the
# app bundles.
juce_add_binary_data(Groove9Samples
    NAMESPACE BinaryData
    SOURCES
        Samples/crash.flac
        Samples/hat-closed.flac
        Samples/hat-open.flac
        Samples/ride.flac)

gk_add_plugin(Groove9 SYNTH
    PRODUCT "Groove-9"
    CODE Grv9
    BUNDLE_ID dev.groovekit.groove9
    SOURCES
        Source/Parameters.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/Presets.cpp
        Source/Synth.cpp)

target_link_libraries(Groove9 PRIVATE Groove9Samples)
gk_link_rhythm(Groove9)
