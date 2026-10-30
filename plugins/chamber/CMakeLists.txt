# Chamber: an algorithmic reverb from small rooms to endless washes, played from an X/Y pad.
gk_add_plugin(Chamber
    CODE Chm8
    BUNDLE_ID dev.groovekit.chamber
    SOURCES
        Source/Parameters.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/Presets.cpp
        Source/Reverb.cpp)
