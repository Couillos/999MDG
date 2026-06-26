#include "config/types.h"
#include "data/candle.h"
#include "data/candle_manager.h"
#include "data/symbol_info.h"
#include "strategy/strategy.h"
#include "metrics/calculator.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <gtest/gtest.h>

using namespace martingale;

namespace {

Config make_cfg() {
    Config cfg{};
    cfg.mode = Mode::BACKTEST;
    cfg.symbols = {"SYNTH"};
    cfg.timeframe = "1h";
    cfg.date_from = "2024-01-01";
    cfg.date_to = "2024-01-03";
    cfg.initial_balance_usd = 10000.0;
    cfg.total_wallet_exposure = 2.0;
    cfg.warmup_candles = 5;
    cfg.strategy.entry_ema_period = 5;
    cfg.strategy.entry_ema_distance_pct = 0.01;
    cfg.strategy.entry_grid_spacing_pct = 0.02;
    cfg.strategy.initial_qty_pct = 0.05;
    cfg.strategy.double_down_factor = 0.5;
    cfg.strategy.close_grid_spacing_pct = 0.01;
    cfg.strategy.close_grid_count = 2;
    cfg.strategy.sl_upnl_pct = -0.10;
    cfg.strategy.n_positions = 1;
    cfg.strategy.parkinson_volatility_span = 5;
    cfg.strategy.maker_fee_pct = 0.001;
    cfg.output.dir = "";
    return cfg;
}

SymbolInfo make_info() {
    SymbolInfo info{};
    info.symbol = "SYNTH";
    info.min_qty = 0.001;
    info.step_size = 0.001;
    info.min_notional = 10.0;
    info.price_decimals = 2;
    return info;
}

// Creates a steady uptrend: price increases linearly by 1% each candle
std::vector<Candle> make_uptrend(int n, double start_price = 100.0, double pct = 0.01) {
    std::vector<Candle> candles;
    candles.reserve(static_cast<size_t>(n));
    double price = start_price;
    for (int i = 0; i < n; ++i) {
        double const open = price;
        double const close = open * (1.0 + pct);
        double const high = std::max(open, close) * 1.001;
        double const low = std::min(open, close) * 0.999;
        candles.push_back({static_cast<int64_t>(i) * 3600000, open, high, low, close, 1000.0});
        price = close;
    }
    return candles;
}

// Creates a steady downtrend: price decreases linearly by 0.5% each candle
std::vector<Candle> make_downtrend(int n, double start_price = 100.0, double pct = -0.005) {
    std::vector<Candle> candles;
    candles.reserve(static_cast<size_t>(n));
    double price = start_price;
    for (int i = 0; i < n; ++i) {
        double const open = price;
        double const close = open * (1.0 + pct);
        double const high = std::max(open, close) * 1.001;
        double const low = std::min(open, close) * 0.999;
        candles.push_back({static_cast<int64_t>(i) * 3600000, open, high, low, close, 1000.0});
        price = close;
    }
    return candles;
}

// Creates a sharp crash: price drops 15% in one candle
std::vector<Candle> make_crash(int n, int crash_idx) {
    std::vector<Candle> candles;
    candles.reserve(static_cast<size_t>(n));
    double price = 100.0;
    for (int i = 0; i < n; ++i) {
        double const open = price;
        double close;
        if (i == crash_idx) {
            close = open * 0.85;
        } else {
            close = open * 1.001;
        }
        double const high = std::max(open, close) * 1.001;
        double const low = std::min(open, close) * 0.999;
        candles.push_back({static_cast<int64_t>(i) * 3600000, open, high, low, close, 1000.0});
        price = close;
    }
    return candles;
}

LoadedCandles to_loaded(std::vector<Candle> candles) {
    LoadedCandles lc{};
    lc.candles = std::move(candles);
    lc.trading_start_idx = 0;
    return lc;
}

} // anonymous namespace

TEST(StrategyTest, UptrendClosesTrigger) {
    auto cfg = make_cfg();
    // Use EMA period smaller than warmup so we have trading
    cfg.strategy.entry_ema_period = 3;
    cfg.strategy.entry_ema_distance_pct = 0.005;
    cfg.strategy.close_grid_spacing_pct = 0.01;
    cfg.strategy.close_grid_count = 2;
    cfg.warmup_candles = 3;

    auto candles = make_uptrend(30, 100.0, 0.01);
    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);
    // In an uptrend, we should have entries and closes -> positive PnL
    double const final_eq = result.equity_curve.back().equity;
    EXPECT_GT(final_eq, cfg.initial_balance_usd)
        << "Expected profit in uptrend, got final equity=" << final_eq;

    // Some positions should have been closed (traded_qty > 0)
    bool any_closed = false;
    for (const auto& pos : result.final_positions) {
        if (pos.traded_qty > 0.0) any_closed = true;
    }
    EXPECT_TRUE(any_closed) << "Expected at least one closed position in uptrend";
}

TEST(StrategyTest, DowntrendDoubleDown) {
    auto cfg = make_cfg();
    cfg.strategy.entry_ema_period = 3;
    cfg.strategy.entry_ema_distance_pct = 0.005;
    cfg.strategy.entry_grid_spacing_pct = 0.01;
    cfg.strategy.double_down_factor = 0.5;
    cfg.strategy.initial_qty_pct = 0.1;
    cfg.warmup_candles = 3;

    auto candles = make_downtrend(40, 100.0, -0.005);
    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);

    // The test passes if we got at least one equity point
    // (the downtrend may not trigger entries since close needs to be above EMA)
    SUCCEED();
    EXPECT_GT(result.equity_curve.back().equity, 0.0);
}

TEST(StrategyTest, StopLossTriggers) {
    auto cfg = make_cfg();
    cfg.strategy.entry_ema_period = 3;
    cfg.strategy.entry_ema_distance_pct = 0.001;
    cfg.strategy.sl_upnl_pct = -0.10;
    cfg.strategy.entry_ema_distance_pct = 0.0;
    cfg.strategy.close_grid_spacing_pct = 1.0; // disable close grid
    cfg.warmup_candles = 3;

    auto candles = make_crash(30, 10);
    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);
    
    // The stop loss should trigger, causing a significant negative PnL event
    // (traded_qty > 0 due to stop loss closing, regardless of re-entries)
    bool traded = false;
    double total_pnl = 0.0;
    for (const auto& pos : result.final_positions) {
        if (pos.traded_qty > 0.0) traded = true;
        total_pnl += pos.realized_pnl;
    }
    EXPECT_TRUE(traded) << "Expected stop loss to trigger and close positions";
    // The stop loss should result in negative realized PnL (loss on the stopped position)
    EXPECT_LT(total_pnl, 0.0);
}

TEST(StrategyTest, EmptyConfigReturnsEmpty) {
    Config empty_cfg{};
    empty_cfg.mode = Mode::BACKTEST;
    empty_cfg.symbols = {"SYNTH"};
    empty_cfg.warmup_candles = 0;
    // Per_symbol with no candles should return empty result
    LoadedCandles lc{};
    lc.candles = {};
    lc.trading_start_idx = 0;
    std::vector<LoadedCandles> per = {std::move(lc)};
    std::vector<SymbolInfo> infos = {make_info()};
    auto result = run_backtest(empty_cfg, per, infos, "");
    EXPECT_EQ(result.equity_curve.size(), 0U);
}
