cmake_minimum_required(VERSION 3.22)

project(groovekit VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GK_SANITIZERS "Build everything with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GK_BUILD_TESTS "Build the test suite" ON)

if(GK_SANITIZERS)
    # Applied globally so JUCE and Tracktion are instrumented too.
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

include(FetchContent)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v12.0.1
    GIT_SHALLOW TRUE)
set(USE_TLS OFF CACHE INTERNAL "")
set(USE_ZLIB OFF CACHE INTERNAL "")
set(IXWEBSOCKET_INSTALL OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(ixwebsocket)

# Offline time-stretching for audio clips (GPL-2.0-or-later). Built from its single-file source.
FetchContent_Declare(rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG v4.0.0
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(rubberband)
if(NOT rubberband_POPULATED)
    FetchContent_Populate(rubberband)
endif()

# MP3 encoding for exports (LAME, LGPL-2.0-or-later). The encoder library, built from its sources.
FetchContent_Declare(lame
    URL https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
    URL_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e)
FetchContent_GetProperties(lame)
if(NOT lame_POPULATED)
    FetchContent_Populate(lame)
endif()
set(LAME_SOURCES VbrTag.c bitstream.c encoder.c fft.c gain_analysis.c id3tag.c lame.c newmdct.c presets.c psymodel.c
    quantize.c quantize_pvt.c reservoir.c set_get.c tables.c takehiro.c util.c vbrquantize.c version.c)
list(TRANSFORM LAME_SOURCES PREPEND "${lame_SOURCE_DIR}/libmp3lame/")
add_library(mp3lame STATIC ${LAME_SOURCES})
target_include_directories(mp3lame PUBLIC "${lame_SOURCE_DIR}/include"
    PRIVATE "${lame_SOURCE_DIR}/libmp3lame" "${CMAKE_CURRENT_SOURCE_DIR}/engine/third_party/lame")
target_compile_definitions(mp3lame PRIVATE HAVE_CONFIG_H)
# Its own code, not GrooveKit's: no warnings, and no sanitizers (the ASan build checks ours).
target_compile_options(mp3lame PRIVATE -w -fno-sanitize=all)

# Speech to text for the agent's voice messages, on this Mac (MIT), with Metal.
FetchContent_Declare(whisper
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG v1.9.5
    GIT_SHALLOW TRUE)
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(WHISPER_BUILD_TESTS OFF CACHE INTERNAL "")
set(WHISPER_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(WHISPER_BUILD_SERVER OFF CACHE INTERNAL "")
set(GGML_METAL ON CACHE INTERNAL "")
set(GGML_METAL_EMBED_LIBRARY ON CACHE INTERNAL "")
set(GGML_NATIVE OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(whisper)

# ONNX Runtime's headers (MIT). The library itself is downloaded with the listening models and
# loaded at run time; this package's copy serves the tests.
FetchContent_Declare(onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v1.31.1/onnxruntime-osx-arm64-1.31.1.tgz
    URL_HASH SHA256=2e1344a1926ee50eb25b38153cb6992c3567515aef0e677a1a520a1798896d6f)
FetchContent_MakeAvailable(onnxruntime)

set(TRACKTION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/engine/external/tracktion_engine")
add_subdirectory("${TRACKTION_DIR}/modules/juce" juce EXCLUDE_FROM_ALL)
add_subdirectory("${TRACKTION_DIR}/modules" tracktion EXCLUDE_FROM_ALL)

if(GK_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.16.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    enable_testing()
endif()

add_subdirectory(plugins/triad)
add_subdirectory(engine)
