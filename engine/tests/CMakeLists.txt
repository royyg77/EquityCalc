include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
# Prevents GTest from overriding compiler/linker flags on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Each class gets its own test executable linked against equitycalc_core.
# Run all with: ctest --test-dir build --output-on-failure

add_executable(test_card test_card.cpp)
target_link_libraries(test_card PRIVATE equitycalc_core GTest::gtest_main)
gtest_discover_tests(test_card)

add_executable(test_combo test_combo.cpp)
target_link_libraries(test_combo PRIVATE equitycalc_core GTest::gtest_main)
gtest_discover_tests(test_combo)

add_executable(test_range test_range.cpp)
target_link_libraries(test_range PRIVATE equitycalc_core GTest::gtest_main)
gtest_discover_tests(test_range)
