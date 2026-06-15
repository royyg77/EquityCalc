# engine/bench/CMakeLists.txt
#
# Cross-version benchmark + correctness anchor. Links the same engine library
# the CLI and tests use. Mirrors the CLI's target setup so the binary lands at
# engine/build/benchmark (next to engine/build/equitycalc).

add_executable(benchmark Benchmark.cpp)
target_link_libraries(benchmark PRIVATE equitycalc_core)

set_target_properties(benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)