add_executable(opencdc_unit_tests
    cli_test.cpp
    graph_test.cpp
    domain_test.cpp
    crossing_test.cpp
    sync_test.cpp
    reconvergence_test.cpp
    rule_test.cpp
    waiver_test.cpp
    report_test.cpp
    ${CMAKE_SOURCE_DIR}/src/ir/graph.cpp
    ${CMAKE_SOURCE_DIR}/src/clock/domain.cpp
    ${CMAKE_SOURCE_DIR}/src/clock/resolve.cpp
    ${CMAKE_SOURCE_DIR}/src/cdc/crossing.cpp
    ${CMAKE_SOURCE_DIR}/src/cdc/synchronizer.cpp
    ${CMAKE_SOURCE_DIR}/src/cdc/reconvergence.cpp
    ${CMAKE_SOURCE_DIR}/src/cdc/waiver.cpp
    ${CMAKE_SOURCE_DIR}/src/rules/rule.cpp
    ${CMAKE_SOURCE_DIR}/src/report/report.cpp
)

target_include_directories(opencdc_unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/ir
        ${CMAKE_SOURCE_DIR}/src/clock
        ${CMAKE_SOURCE_DIR}/src/cdc
        ${CMAKE_SOURCE_DIR}/src/rules
        ${CMAKE_SOURCE_DIR}/src/report
)

target_compile_definitions(opencdc_unit_tests
    PRIVATE
        OPENCDC_BIN_DIR="${CMAKE_BINARY_DIR}/src"
)

target_link_libraries(opencdc_unit_tests
    PRIVATE
        GTest::gtest_main
)

gtest_discover_tests(opencdc_unit_tests)
