# gk_engine compiles JUCE, Tracktion and the GrooveKit engine code once; the CLI and tests link it.
add_library(gk_engine STATIC)

target_sources(gk_engine PRIVATE
    src/EngineContext.cpp
    src/analysis/Analysis.cpp
    src/analysis/MixAnalysis.cpp
    src/api/ApiHandler.cpp
    src/api/ApiServer.cpp
    src/compiler/EditCompiler.cpp
    src/compiler/EditGraveyard.cpp
    src/effects/BuiltinEffects.cpp
    src/library/Classifier.cpp
    src/library/SampleFeatures.cpp
    src/library/SampleLibrary.cpp
    src/live/LiveEngine.cpp
    src/live/MidiClock.cpp
    src/live/MidiClockPlugin.cpp
    src/live/SamplePreview.cpp
    src/mixer/FaderPlugin.cpp
    src/mixer/LimiterPlugin.cpp
    src/model/Project.cpp
    src/plugins/PluginCatalog.cpp
    src/render/Render.cpp
    src/render/Stretch.cpp
    src/sampler/SampleCache.cpp
    src/sampler/SamplerEngine.cpp
    src/sampler/SamplerPlugin.cpp
    src/session/Commands.cpp
    src/session/Mastering.cpp
    src/session/Session.cpp
    src/session/Summary.cpp
    src/session/Transaction.cpp)

target_include_directories(gk_engine PUBLIC src)

target_sources(gk_engine PRIVATE "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
target_include_directories(gk_engine PRIVATE "${rubberband_SOURCE_DIR}")
set_source_files_properties("${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp" PROPERTIES COMPILE_OPTIONS "-w")

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
        nlohmann_json::nlohmann_json
        ixwebsocket::ixwebsocket)

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
