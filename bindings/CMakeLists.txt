cmake_minimum_required(VERSION 3.20)
project(equitycalc_bindings LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch pybind11
include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.13.6
)
FetchContent_MakeAvailable(pybind11)

# The engine library is expected at ../engine, built separately.
# When building bindings standalone, point CMake at the engine source dir:
#   cmake -S bindings -B build -DENGINE_DIR=${PWD}/engine
if(NOT TARGET equitycalc_core)
    if(NOT DEFINED ENGINE_DIR)
        set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../engine")
    endif()
    add_subdirectory(${ENGINE_DIR} ${CMAKE_BINARY_DIR}/engine_build)
endif()

pybind11_add_module(_equitycalc module.cpp)
target_link_libraries(_equitycalc PRIVATE equitycalc_core)
