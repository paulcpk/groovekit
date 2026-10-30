# Echo: a stereo delay with tape and dub in it, played from an X/Y pad.
gk_add_plugin(Echo
    CODE Ech0
    BUNDLE_ID dev.groovekit.echo
    SOURCES
        Source/Delay.cpp
        Source/Parameters.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/Presets.cpp)
