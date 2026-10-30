# Triad: GrooveKit's three-oscillator synth, as a VST3 any host can load. The app bundles it and
# registers it with its engine; it is not installed into the system's plug-in folders.
juce_add_plugin(Triad
    PRODUCT_NAME "Triad"
    COMPANY_NAME "GrooveKit"
    COMPANY_WEBSITE "https://groovekit.org"
    BUNDLE_ID dev.groovekit.triad
    PLUGIN_MANUFACTURER_CODE Grvk
    PLUGIN_CODE Trd3
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE
    VERSION ${PROJECT_VERSION})

target_sources(Triad PRIVATE
    Source/Parameters.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/Synth.cpp)

target_compile_definitions(Triad PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(Triad
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
gk_optimise_third_party()

# Warnings for our own code, not JUCE's.
set_source_files_properties(Source/Parameters.cpp Source/PluginEditor.cpp Source/PluginProcessor.cpp Source/Synth.cpp
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wfloat-equal")

# Nothing of JUCE visible outside the plug-in, so it never binds to a host's own JUCE (GrooveKit's
# engine is built with JUCE too) and the two don't mix up their objects.
set_target_properties(Triad Triad_VST3 PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    OBJC_VISIBILITY_PRESET hidden
    OBJCXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN TRUE)

# Beside the other plugins, where the engine and the app's packaging find them.
add_custom_command(TARGET Triad_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GK_PLUGINS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${GK_PLUGINS_DIR}/Triad.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:Triad_VST3>" "${GK_PLUGINS_DIR}/Triad.vst3"
    COMMENT "Collecting Triad.vst3")
