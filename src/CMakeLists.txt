set(OPENCDC_CORE_SOURCES
    main.cpp
    ir/graph.cpp
    frontend/slang_adapter.cpp
    clock/domain.cpp
    clock/resolve.cpp
    clock/constraints.cpp
    cdc/crossing.cpp
    cdc/synchronizer.cpp
    cdc/reconvergence.cpp
    cdc/cdc006.cpp
    cdc/waiver.cpp
    cdc/pattern.cpp
    cdc/reset_domain.cpp
    rules/rule.cpp
    report/report.cpp
    report/html_reporter.cpp
    config/config.cpp
    analysis/trend.cpp
    analysis/analyzer.cpp
    lsp/server.cpp
)

add_library(opencdc_core STATIC ${OPENCDC_CORE_SOURCES})
set_target_properties(opencdc_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(opencdc_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(opencdc_core PUBLIC slang::slang nlohmann_json::nlohmann_json)

add_executable(opencdc entry.cpp)
target_link_libraries(opencdc PRIVATE opencdc_core)

option(BUILD_PYTHON_BINDINGS "Build Python bindings" OFF)
if(BUILD_PYTHON_BINDINGS)
    find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
    find_package(pybind11 CONFIG QUIET)
    if(NOT pybind11_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG        v2.12.0
            GIT_SHALLOW    ON
        )
        FetchContent_MakeAvailable(pybind11)
    endif()
    pybind11_add_module(opencdc_python python/module.cpp)
    set_target_properties(opencdc_python PROPERTIES OUTPUT_NAME opencdc)
    target_include_directories(opencdc_python PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(opencdc_python PRIVATE opencdc_core)
endif()

install(TARGETS opencdc RUNTIME DESTINATION bin)
