cmake_minimum_required(VERSION 3.20)
project(equitycalc LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd / IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Core library
add_library(equitycalc_core STATIC
    src/Card.cpp
    src/Combo.cpp
    src/Range.cpp
    src/Board.cpp
    src/Parser.cpp
    src/Evaluator.cpp
    src/Calculator.cpp
    src/Result.cpp
)

target_include_directories(equitycalc_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Warnings — applied only to our own sources; third-party code is excluded.
if(MSVC)
    target_compile_options(equitycalc_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(equitycalc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# OMPEval — vendored at third_party/OMPEval/.
# SYSTEM suppresses compiler warnings from OMP headers.
# HandEvaluator.cpp contains the one-time static lookup-table initialization.
target_sources(equitycalc_core PRIVATE
    third_party/OMPEval/omp/HandEvaluator.cpp
)
target_include_directories(equitycalc_core SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/OMPEval
)

# CLI executable
add_subdirectory(cli)
add_subdirectory(bench)

# Tests (opt-in via -DEQUITYCALC_BUILD_TESTS=ON, default ON)
option(EQUITYCALC_BUILD_TESTS "Build tests" ON)
if(EQUITYCALC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
