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
    src/effects/Dynamics.cpp
    src/effects/EffectPlugin.cpp
    src/effects/Effects.cpp
    src/effects/EqPlugin.cpp
    src/effects/Saturation.cpp
    src/effects/Shapers.cpp
    src/effects/StereoPlugin.cpp
    src/guides/Guides.cpp
    src/library/Classifier.cpp
    src/library/SampleFeatures.cpp
    src/library/SampleLibrary.cpp
    src/listening/Arrangement.cpp
    src/listening/Clap.cpp
    src/listening/Download.cpp
    src/listening/Groove.cpp
    src/listening/KickIndex.cpp
    src/listening/KickSound.cpp
    src/listening/Listening.cpp
    src/listening/Maps.cpp
    src/listening/Models.cpp
    src/listening/NoteTranscriber.cpp
    src/listening/Ort.cpp
    src/listening/Separator.cpp
    src/listening/SoundIndex.cpp
    src/listening/Styles.cpp
    src/listening/Taste.cpp
    src/listening/TasteModel.cpp
    src/live/AudioDevices.cpp
    src/live/LiveEngine.cpp
    src/live/MidiClock.cpp
    src/live/MidiClockPlugin.cpp
    src/live/SamplePreview.cpp
    src/mixer/EffectMix.cpp
    src/mixer/FaderPlugin.cpp
    src/mixer/LimiterPlugin.cpp
    src/mixer/Slots.cpp
    src/model/Project.cpp
    src/plugins/PluginCatalog.cpp
    src/plugins/PluginWindows.cpp
    src/render/Encode.cpp
    src/render/Render.cpp
    src/render/Stretch.cpp
    src/sampler/AudioChannelPlugin.cpp
    src/sampler/Processing.cpp
    src/sampler/SampleCache.cpp
    src/music/Keys.cpp
    src/music/Notes.cpp
    src/sampler/Modulation.cpp
    src/sampler/SamplerEngine.cpp
    src/sampler/SamplerPlugin.cpp
    src/session/AgentNotes.cpp
    src/session/Chats.cpp
    src/session/Commands.cpp
    src/session/Mastering.cpp
    src/session/Presets.cpp
    src/session/RecentProjects.cpp
    src/session/Session.cpp
    src/session/Summary.cpp
    src/session/Transaction.cpp
    src/voice/Transcriber.cpp)

target_include_directories(gk_engine PUBLIC src)

# The style guides (guides/*.md), built in.
file(GLOB guide_files CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/guides/*.md")
set(guides_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/GuidesData.cpp")
add_custom_command(
    OUTPUT "${guides_cpp}"
    COMMAND ${CMAKE_COMMAND} -DGUIDES_DIR=${CMAKE_SOURCE_DIR}/guides -DOUTPUT=${guides_cpp} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedGuides.cmake"
    DEPENDS ${guide_files} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedGuides.cmake"
    COMMENT "Embedding the style guides")
target_sources(gk_engine PRIVATE "${guides_cpp}")
target_include_directories(gk_engine SYSTEM PUBLIC "${onnxruntime_SOURCE_DIR}/include")

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
        whisper
        mp3lame
    PUBLIC
        juce::juce_recommended_config_flags
        nlohmann_json::nlohmann_json
        ixwebsocket::ixwebsocket)

if(GK_SANITIZERS)
    # libFLAC, compiled inside JUCE's audio formats, offsets null pointers when it encodes (harmless,
    # and not ours to change): the rest of the checks stay on.
    set_source_files_properties(
        "${TRACKTION_DIR}/modules/juce/modules/juce_audio_formats/juce_audio_formats.mm"
        "${TRACKTION_DIR}/modules/juce/modules/juce_audio_formats/juce_audio_formats.cpp"
        PROPERTIES COMPILE_OPTIONS "-fno-sanitize=pointer-overflow")
endif()

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

# GrooveKit's own plugins, built with the engine and found in the build tree during development.
target_compile_definitions(gk_engine PRIVATE GK_BUILT_PLUGINS="$<TARGET_FILE_DIR:Triad_VST3>/../../..")

add_executable(groovekit-engine src/main.cpp)
target_link_libraries(groovekit-engine PRIVATE gk_engine juce::juce_recommended_warning_flags)
add_dependencies(groovekit-engine Triad_VST3)

if(GK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
