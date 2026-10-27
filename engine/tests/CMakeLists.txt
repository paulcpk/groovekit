add_executable(gk_tests
    AnalysisTests.cpp
    ClockTests.cpp
    EffectTests.cpp
    ApiTests.cpp
    LibraryTests.cpp
    LimiterTests.cpp
    ListeningTests.cpp
    LiveTests.cpp
    ModelTests.cpp
    PluginTests.cpp
    PresetTests.cpp
    RenderTests.cpp
    MusicTests.cpp
    SamplerTests.cpp
    SessionTests.cpp
    TestHelpers.cpp)

target_link_libraries(gk_tests PRIVATE gk_engine Catch2::Catch2WithMain)
add_dependencies(gk_tests Triad_VST3)

include(Catch)
catch_discover_tests(gk_tests
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    PROPERTIES ENVIRONMENT "GK_REPO_ROOT=${PROJECT_SOURCE_DIR}")
