# What GrooveKit's rhythm machines share: the step sequencer and its patterns, the machine that
# plays it with the host, the panel's look and the voices' building blocks. An interface library
# like gk_plugin_common: each machine compiles these sources itself.
add_library(gk_rhythm INTERFACE)

set(GK_RHYTHM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/MachineEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MachineLook.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MachineProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Sequencer.cpp
    CACHE INTERNAL "")

target_sources(gk_rhythm INTERFACE ${GK_RHYTHM_SOURCES})
target_include_directories(gk_rhythm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Links a machine to it, with warnings for these sources as for the machine's own (a source's
# options belong to the directory whose target compiles it).
function(gk_link_rhythm target)
    target_link_libraries(${target} PRIVATE gk_rhythm)
    set_source_files_properties(${GK_RHYTHM_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wfloat-equal")
endfunction()
