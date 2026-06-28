# Tests use GTest which REQUIRES RTTI and exceptions, so we do NOT apply
# MARTINGALE_STRICT_FLAGS here. We also relax -Werror to avoid GTest macro
# trips (sign-compare on size(), float-equal on EXPECT_DOUBLE_EQ, etc.).

set(MARTINGALE_TEST_WARN_FLAGS
    -Wall -Wextra -Wpedantic
    -Wunused-parameter -Wunused-variable -Wunused-function
    -Wshadow -Wnon-virtual-dtor
)

add_executable(test_config
    test_config.cpp
)

target_compile_options(test_config PRIVATE
    ${MARTINGALE_TEST_WARN_FLAGS}
)

target_link_libraries(test_config PRIVATE
    GTest::gtest
    GTest::gtest_main
    martingale_lib
)

target_include_directories(test_config PRIVATE ../src)

add_test(NAME test_config COMMAND test_config)

add_executable(test_strategy
    test_strategy.cpp
)

target_compile_options(test_strategy PRIVATE
    ${MARTINGALE_TEST_WARN_FLAGS}
)

target_link_libraries(test_strategy PRIVATE
    GTest::gtest
    GTest::gtest_main
    martingale_lib
)

target_include_directories(test_strategy PRIVATE ../src)

add_test(NAME test_strategy COMMAND test_strategy)

add_executable(test_metrics
    test_metrics.cpp
)

target_compile_options(test_metrics PRIVATE
    ${MARTINGALE_TEST_WARN_FLAGS}
)

target_link_libraries(test_metrics PRIVATE
    GTest::gtest
    GTest::gtest_main
    martingale_lib
)

target_include_directories(test_metrics PRIVATE ../src)

add_test(NAME test_metrics COMMAND test_metrics)

# Constraint-aware sort validation
add_executable(test_constraint_sort
    test_constraint_sort.cpp
)

target_compile_options(test_constraint_sort PRIVATE
    ${MARTINGALE_TEST_WARN_FLAGS}
)

target_link_libraries(test_constraint_sort PRIVATE
    martingale_lib
)

target_include_directories(test_constraint_sort PRIVATE ../src)

add_test(NAME test_constraint_sort COMMAND test_constraint_sort)
