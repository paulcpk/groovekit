# What GrooveKit's plugins share: the look (knobs, choices, X/Y pads, panels, screens, a keyboard),
# presets as programs, and DSP building blocks. An interface library: each plugin compiles these
# sources itself, so nothing of JUCE is shared between the plugins' binaries.
add_library(gk_plugin_common INTERFACE)

target_sources(gk_plugin_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/EffectEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Look.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Presets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Widgets.cpp)

target_include_directories(gk_plugin_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/EffectEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Look.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Presets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Widgets.cpp
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wfloat-equal")

# One plugin target, the GrooveKit way: a VST3 the app bundles, with JUCE kept inside it.
function(gk_add_plugin name)
    cmake_parse_arguments(GK "SYNTH" "CODE;BUNDLE_ID;DESCRIPTION" "SOURCES" ${ARGN})

    if(GK_SYNTH)
        set(kind IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE VST3_CATEGORIES Instrument Synth)
    else()
        set(kind IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE VST3_CATEGORIES Fx)
    endif()

    juce_add_plugin(${name}
        PRODUCT_NAME "${name}"
        COMPANY_NAME "GrooveKit"
        COMPANY_WEBSITE "https://groovekit.org"
        BUNDLE_ID ${GK_BUNDLE_ID}
        PLUGIN_MANUFACTURER_CODE Grvk
        PLUGIN_CODE ${GK_CODE}
        FORMATS VST3
        ${kind}
        COPY_PLUGIN_AFTER_BUILD FALSE
        VERSION ${PROJECT_VERSION})

    target_sources(${name} PRIVATE ${GK_SOURCES})
    target_compile_definitions(${name} PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(${name} PRIVATE gk_plugin_common juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
    set_source_files_properties(${GK_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wfloat-equal")
    set_target_properties(${name} ${name}_VST3 PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        OBJC_VISIBILITY_PRESET hidden
        OBJCXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN TRUE)

    # Every plugin lands in one folder, where the engine and the app's packaging find them.
    add_custom_command(TARGET ${name}_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GK_PLUGINS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${GK_PLUGINS_DIR}/${name}.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${name}_VST3>" "${GK_PLUGINS_DIR}/${name}.vst3"
        COMMENT "Collecting ${name}.vst3")
endfunction()
