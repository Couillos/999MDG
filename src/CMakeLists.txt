set(POWERMDG_SOURCES
    config/loader.cpp
    data/binance_client.cpp
    data/symbol_info.cpp
    data/cache.cpp
    data/candle_manager.cpp
    plot/plotter.cpp
    strategy/stop_loss.cpp
    strategy/unstuck.cpp
    strategy/parkinson_volatility.cpp
    strategy/strategy.cpp
    strategy/modules/entry_condition/entry_condition.cpp
    strategy/modules/entry_condition/ema_dist_pct.cpp
    strategy/modules/entry_condition/bb_reversion.cpp
    strategy/modules/entries_algo/entries_algo.cpp
    strategy/modules/entries_algo/martingale.cpp
    strategy/modules/entries_algo/dca_linear.cpp
    strategy/modules/closes_algo/closes_algo.cpp
    strategy/modules/closes_algo/simple_grid.cpp
    strategy/modules/closes_algo/mean_revert_tp.cpp
    metrics/calculator.cpp
    utils/thread_pool.cpp
    optimizer/optimizer.cpp
    optimizer/nsga2.cpp
    ui/tui.cpp
)

add_library(powermdg_lib STATIC
    ${POWERMDG_SOURCES}
)

target_link_libraries(powermdg_lib PUBLIC
    simdjson::simdjson
    PkgConfig::CURL
    zstd::libzstd_static
    ${CURSES_LIBRARIES}
)

target_include_directories(powermdg_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CURSES_INCLUDE_DIRS}
)

# Apply strict warn flags to the library (was previously only on the exe — audit issue #2)
target_compile_options(powermdg_lib PUBLIC
    ${POWERMDG_WARN_FLAGS}
)

# Strict flags (no-rtti, no-exceptions) applied as PRIVATE so tests can override
target_compile_options(powermdg_lib PRIVATE
    ${POWERMDG_STRICT_FLAGS}
)

add_executable(powermdg
    main.cpp
)

target_link_libraries(powermdg PRIVATE
    powermdg_lib
)

target_include_directories(powermdg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
