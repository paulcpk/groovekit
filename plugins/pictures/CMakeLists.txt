# Pictures of GrooveKit's plugin windows, drawn off-screen from the plugins' own code (a hosted
# plugin's window is a native view and can't be drawn that way), for the website and the docs:
#   build/<preset>/plugins/pictures/PluginPictures_artefacts/<config>/PluginPictures <folder>
# Not part of the default build (it compiles JUCE once more): cmake --build --preset dev --target PluginPictures
juce_add_console_app(PluginPictures PRODUCT_NAME "PluginPictures")
set_target_properties(PluginPictures PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_sources(PluginPictures PRIVATE main.cpp)

# Every plugin's sources, each with its plugin factory renamed so they don't clash.
foreach(plugin triad quartet sextet kessel chamber echo)
    file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/plugins/${plugin}/Source/*.cpp")
    target_sources(PluginPictures PRIVATE ${sources})
    set_source_files_properties(${sources} PROPERTIES COMPILE_DEFINITIONS "createPluginFilter=gkCreate_${plugin}")
endforeach()

target_compile_definitions(PluginPictures PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(PluginPictures PRIVATE gk_plugin_common juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
