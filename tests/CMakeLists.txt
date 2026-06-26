add_executable(test_config
    test_config.cpp
)

target_compile_options(test_config PRIVATE
    ${MARTINGALE_WARN_FLAGS}
    -fno-rtti
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
    ${MARTINGALE_WARN_FLAGS}
    -fno-rtti
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
    ${MARTINGALE_WARN_FLAGS}
    -fno-rtti
)

target_link_libraries(test_metrics PRIVATE
    GTest::gtest
    GTest::gtest_main
    martingale_lib
)

target_include_directories(test_metrics PRIVATE ../src)

add_test(NAME test_metrics COMMAND test_metrics)
