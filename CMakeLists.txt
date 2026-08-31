cmake_minimum_required(VERSION 3.28)
project(opencdc VERSION 0.4.0 LANGUAGES CXX)

# Generate version.h from the project version — single source of truth.
configure_file(cmake/version.h.in ${CMAKE_BINARY_DIR}/generated/opencdc/version.h @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTING "Build tests" ON)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

include(FetchContent)

# slang — SystemVerilog frontend
FetchContent_Declare(
    slang
    GIT_REPOSITORY https://github.com/MikePopoloski/slang.git
    GIT_TAG        v8.0
    GIT_SHALLOW    ON
    GIT_PROGRESS   TRUE
)

# Disable slang's own tests/examples to speed up build
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_UVM_TEST OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_THREADTEST OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(slang)

# nlohmann/json — used by LSP for proper JSON-RPC parsing
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(src)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
