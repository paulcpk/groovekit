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

add_subdirectory(engine)
