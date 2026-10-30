add_executable(gk_tests
    AnalysisTests.cpp
    ClockTests.cpp
    EffectTests.cpp
    ApiTests.cpp
    LibraryTests.cpp
    LimiterTests.cpp
    ListeningTests.cpp
    LiveTests.cpp
    MapsTests.cpp
    MixTests.cpp
    ModelTests.cpp
    PluginTests.cpp
    PresetTests.cpp
    RenderTests.cpp
    MusicTests.cpp
    SamplerTests.cpp
    SessionTests.cpp
    TestHelpers.cpp)

target_link_libraries(gk_tests PRIVATE gk_engine Catch2::Catch2WithMain)
target_precompile_headers(gk_tests PRIVATE <tracktion_engine/tracktion_engine.h> <nlohmann/json.hpp> <catch2/catch_test_macros.hpp>)
add_dependencies(gk_tests Triad_VST3)

include(Catch)
catch_discover_tests(gk_tests
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    PROPERTIES ENVIRONMENT "GK_REPO_ROOT=${PROJECT_SOURCE_DIR}")
