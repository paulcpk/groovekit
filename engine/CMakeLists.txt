# gk_engine compiles JUCE, Tracktion and the GrooveKit engine code once; the CLI and tests link it.
add_library(gk_engine STATIC)

target_sources(gk_engine PRIVATE
    src/EngineContext.cpp
    src/model/Project.cpp)

target_include_directories(gk_engine PUBLIC src)

target_compile_definitions(gk_engine PUBLIC
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_SILENCE_XCODE_15_LINKER_WARNING=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    TRACKTION_LOG_DEVICES=0)

target_link_libraries(gk_engine
    PRIVATE
        tracktion::tracktion_core
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        nlohmann_json::nlohmann_json)

# Expose the module include paths and definitions without compiling the modules again in dependents.
target_include_directories(gk_engine INTERFACE $<TARGET_PROPERTY:gk_engine,INCLUDE_DIRECTORIES>)
target_compile_definitions(gk_engine INTERFACE $<TARGET_PROPERTY:gk_engine,COMPILE_DEFINITIONS>)

set_target_properties(gk_engine PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE
    VISIBILITY_INLINES_HIDDEN TRUE
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

if(APPLE)
    target_compile_options(gk_engine PRIVATE -fno-aligned-allocation)
endif()

add_executable(groovekit-engine src/main.cpp)
target_link_libraries(groovekit-engine PRIVATE gk_engine juce::juce_recommended_warning_flags)

if(GK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
