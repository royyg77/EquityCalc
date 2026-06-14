add_executable(equitycalc Main.cpp)
target_link_libraries(equitycalc PRIVATE equitycalc_core)

# Place the binary at engine/build/equitycalc rather than engine/build/cli/equitycalc.
set_target_properties(equitycalc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

install(TARGETS equitycalc DESTINATION bin)
