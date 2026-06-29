# Tests use GTest which REQUIRES RTTI and exceptions, so we do NOT apply
# POWERMDG_STRICT_FLAGS here. We also relax -Werror to avoid GTest macro
# trips (sign-compare on size(), float-equal on EXPECT_DOUBLE_EQ, etc.).

set(POWERMDG_TEST_WARN_FLAGS
    -Wall -Wextra -Wpedantic
    -Wunused-parameter -Wunused-variable -Wunused-function
    -Wshadow -Wnon-virtual-dtor
)

add_executable(test_config
    test_config.cpp
)

target_compile_options(test_config PRIVATE
    ${POWERMDG_TEST_WARN_FLAGS}
)

target_link_libraries(test_config PRIVATE
    GTest::gtest
    GTest::gtest_main
    powermdg_lib
)

target_include_directories(test_config PRIVATE ../src)

add_test(NAME test_config COMMAND test_config)

add_executable(test_strategy
    test_strategy.cpp
)

target_compile_options(test_strategy PRIVATE
    ${POWERMDG_TEST_WARN_FLAGS}
)

target_link_libraries(test_strategy PRIVATE
    GTest::gtest
    GTest::gtest_main
    powermdg_lib
)

target_include_directories(test_strategy PRIVATE ../src)

add_test(NAME test_strategy COMMAND test_strategy)

add_executable(test_metrics
    test_metrics.cpp
)

target_compile_options(test_metrics PRIVATE
    ${POWERMDG_TEST_WARN_FLAGS}
)

target_link_libraries(test_metrics PRIVATE
    GTest::gtest
    GTest::gtest_main
    powermdg_lib
)

target_include_directories(test_metrics PRIVATE ../src)

add_test(NAME test_metrics COMMAND test_metrics)

# Constraint-aware sort validation
add_executable(test_constraint_sort
    test_constraint_sort.cpp
)

target_compile_options(test_constraint_sort PRIVATE
    ${POWERMDG_TEST_WARN_FLAGS}
)

target_link_libraries(test_constraint_sort PRIVATE
    powermdg_lib
)

target_include_directories(test_constraint_sort PRIVATE ../src)

add_test(NAME test_constraint_sort COMMAND test_constraint_sort)

add_executable(test_metrics_invariants
    test_metrics_invariants.cpp
)

target_compile_options(test_metrics_invariants PRIVATE
    ${POWERMDG_TEST_WARN_FLAGS}
)

target_link_libraries(test_metrics_invariants PRIVATE
    GTest::gtest
    GTest::gtest_main
    powermdg_lib
)

target_include_directories(test_metrics_invariants PRIVATE ../src)

add_test(NAME test_metrics_invariants COMMAND test_metrics_invariants)
