add_executable(opencdc_regression_tests
    frontend_test.cpp
    stress_test.cpp
    lsp_test.cpp
)

target_include_directories(opencdc_regression_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
)

target_compile_definitions(opencdc_regression_tests
    PRIVATE
        FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

target_link_libraries(opencdc_regression_tests
    PRIVATE
        opencdc_core
        GTest::gtest_main
)

gtest_discover_tests(opencdc_regression_tests)
