add_executable(opencdc_regression_tests
    frontend_test.cpp
    ${CMAKE_SOURCE_DIR}/src/ir/graph.cpp
    ${CMAKE_SOURCE_DIR}/src/frontend/slang_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/clock/domain.cpp
    ${CMAKE_SOURCE_DIR}/src/clock/resolve.cpp
    ${CMAKE_SOURCE_DIR}/src/cdc/crossing.cpp
    ${CMAKE_SOURCE_DIR}/src/cdc/synchronizer.cpp
    ${CMAKE_SOURCE_DIR}/src/cdc/reconvergence.cpp
    ${CMAKE_SOURCE_DIR}/src/cdc/waiver.cpp
    ${CMAKE_SOURCE_DIR}/src/rules/rule.cpp
    ${CMAKE_SOURCE_DIR}/src/report/report.cpp
)

target_include_directories(opencdc_regression_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/ir
        ${CMAKE_SOURCE_DIR}/src/frontend
        ${CMAKE_SOURCE_DIR}/src/clock
        ${CMAKE_SOURCE_DIR}/src/cdc
        ${CMAKE_SOURCE_DIR}/src/rules
        ${CMAKE_SOURCE_DIR}/src/report
)

target_compile_definitions(opencdc_regression_tests
    PRIVATE
        FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

target_link_libraries(opencdc_regression_tests
    PRIVATE
        GTest::gtest_main
        slang::slang
)

gtest_discover_tests(opencdc_regression_tests)
