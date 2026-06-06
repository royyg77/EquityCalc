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

add_executable(test_card TestCard.cpp)
target_link_libraries(test_card PRIVATE equitycalc_core GTest::gtest_main)
gtest_discover_tests(test_card)

add_executable(test_combo TestCombo.cpp)
target_link_libraries(test_combo PRIVATE equitycalc_core GTest::gtest_main)
gtest_discover_tests(test_combo)

add_executable(test_range TestRange.cpp)
target_link_libraries(test_range PRIVATE equitycalc_core GTest::gtest_main)
gtest_discover_tests(test_range)

add_executable(test_board TestBoard.cpp)
target_link_libraries(test_board PRIVATE equitycalc_core GTest::gtest_main)
gtest_discover_tests(test_board)

add_executable(test_parser TestParser.cpp)
target_link_libraries(test_parser PRIVATE equitycalc_core GTest::gtest_main)
gtest_discover_tests(test_parser)

add_executable(test_evaluator TestEvaluator.cpp)
target_link_libraries(test_evaluator PRIVATE equitycalc_core GTest::gtest_main)
gtest_discover_tests(test_evaluator)
