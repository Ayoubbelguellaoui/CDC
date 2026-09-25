add_executable(opencdc_unit_tests
    cli_test.cpp
    graph_test.cpp
    domain_test.cpp
    crossing_test.cpp
    sync_test.cpp
    sync_comprehensive_test.cpp
    sync_adversarial_test.cpp
    reconvergence_test.cpp
    cdc006_test.cpp
    cdc007_adversarial_test.cpp
    naming_adversarial_test.cpp
    rule_test.cpp
    waiver_test.cpp
    waiver_regex_test.cpp
    report_test.cpp
    config_test.cpp
    pattern_test.cpp
    constraints_test.cpp
    parallel_test.cpp
    thread_determinism_test.cpp
    resolve_test.cpp
    analyzer_test.cpp
    analysis_status_test.cpp
    fix_regression_test.cpp
    profile_test.cpp
    reset_domain_test.cpp
    blackbox_test.cpp
    phase5_test.cpp
    phase6_test.cpp
    golden_corpus_test.cpp
    cdc_mutation_test.cpp
    benchmark_test.cpp
)

target_include_directories(opencdc_unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
)

# Multi-config generators (Visual Studio, Xcode) place binaries in a
# per-config subdirectory (src/Release/opencdc.exe); single-config ones
# put them directly in src/. The suffix below keeps OPENCDC_BIN_DIR valid
# on both (forward slashes work on Windows too).
if(CMAKE_CONFIGURATION_TYPES)
    set(OPENCDC_BIN_SUFFIX "/$<CONFIG>")
else()
    set(OPENCDC_BIN_SUFFIX "")
endif()

target_compile_definitions(opencdc_unit_tests
    PRIVATE
        OPENCDC_BIN_DIR="${CMAKE_BINARY_DIR}/src"
        OPENCDC_BIN_SUFFIX="${OPENCDC_BIN_SUFFIX}"
        FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# The CLI tests shell out to the opencdc binary: ensure it exists before
# the test binary links (matters for single-target builds).
add_dependencies(opencdc_unit_tests opencdc)

target_link_libraries(opencdc_unit_tests
    PRIVATE
        opencdc_core
        GTest::gtest_main
)

gtest_discover_tests(opencdc_unit_tests)
