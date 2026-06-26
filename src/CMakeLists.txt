set(MARTINGALE_SOURCES
    config/loader.cpp
    data/binance_client.cpp
    data/symbol_info.cpp
    data/cache.cpp
    data/candle_manager.cpp
    plot/plotter.cpp
    strategy/entry_grid.cpp
    strategy/close_grid.cpp
    strategy/stop_loss.cpp
    strategy/unstuck.cpp
    strategy/parkinson_volatility.cpp
    strategy/strategy.cpp
    metrics/calculator.cpp
    utils/thread_pool.cpp
    optimizer/optimizer.cpp
    ui/tui.cpp
)

add_library(martingale_lib STATIC
    ${MARTINGALE_SOURCES}
)

target_link_libraries(martingale_lib PUBLIC
    simdjson::simdjson
    CURL::libcurl
    zstd::libzstd_static
    ncurses
)

target_include_directories(martingale_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(martingale
    main.cpp
)

target_compile_options(martingale PRIVATE
    ${MARTINGALE_WARN_FLAGS}
    ${MARTINGALE_STRICT_FLAGS}
)

target_link_libraries(martingale PRIVATE
    martingale_lib
)

target_include_directories(martingale PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
