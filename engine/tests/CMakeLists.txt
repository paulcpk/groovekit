add_executable(gk_tests
    ModelTests.cpp)

target_link_libraries(gk_tests PRIVATE gk_engine Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(gk_tests
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    PROPERTIES ENVIRONMENT "GK_REPO_ROOT=${PROJECT_SOURCE_DIR}")
