add_executable(opencdc_unit_tests
    cli_test.cpp
    graph_test.cpp
    domain_test.cpp
    crossing_test.cpp
    sync_test.cpp
    reconvergence_test.cpp
    cdc006_test.cpp
    rule_test.cpp
    waiver_test.cpp
    waiver_regex_test.cpp
    report_test.cpp
    config_test.cpp
    pattern_test.cpp
    constraints_test.cpp
    parallel_test.cpp
    resolve_test.cpp
    analyzer_test.cpp
    fix_regression_test.cpp
)

target_include_directories(opencdc_unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
)

target_compile_definitions(opencdc_unit_tests
    PRIVATE
        OPENCDC_BIN_DIR="${CMAKE_BINARY_DIR}/src"
        FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

target_link_libraries(opencdc_unit_tests
    PRIVATE
        opencdc_core
        GTest::gtest_main
)

gtest_discover_tests(opencdc_unit_tests)
