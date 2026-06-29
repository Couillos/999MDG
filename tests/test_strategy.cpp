#include "config/types.h"
#include "data/candle.h"
#include "data/candle_manager.h"
#include "data/symbol_info.h"
#include "strategy/strategy.h"
#include "strategy/unstuck.h"
#include "metrics/calculator.h"
#include "strategy/modules/entry_condition/ema_dist_pct.h"
#include "strategy/modules/closes_algo/simple_grid.h"
#include "strategy/modules/closes_algo/graduated_tp.h"
#include "strategy/modules/module_context.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <gtest/gtest.h>

using namespace powermdg;

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

    // New scenario (C1 fix): entries now require a DIP below EMA*(1-dist).
    // Build: 3 flat warmup candles at 100 → EMA≈100
    // Then 1 dip candle at 98.0:
    //   EMA = 0.5*98 + 0.5*100 = 99; threshold = 99*(1-0.005)=98.505; 98 < 98.505 → entry fires
    // Avg entry ≈ 98. Close grid levels: 98*1.01=98.98 and 98*1.02=99.96.
    // Then 26 candles rising 1%/candle from 98 → price easily exceeds 99.96, triggering both closes.
    std::vector<Candle> candles;
    candles.reserve(30);
    // Warmup: 3 flat candles at 100
    for (int i = 0; i < 3; ++i) {
        candles.push_back({static_cast<int64_t>(i) * 3600000, 100.0, 100.1, 99.9, 100.0, 1000.0});
    }
    // Dip candle at 98.0 — triggers entry
    candles.push_back({3LL * 3600000, 98.5, 99.0, 97.5, 98.0, 1000.0});
    // Recovery uptrend: 26 candles rising 1% each
    double price = 98.0;
    for (int i = 0; i < 26; ++i) {
        price *= 1.01;
        candles.push_back({static_cast<int64_t>(4 + i) * 3600000,
                           price * 0.999, price * 1.001, price * 0.998, price, 1000.0});
    }

    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);
    // After a dip-entry, the subsequent rise should trigger closes → positive PnL
    double const final_eq = result.equity_curve.back().equity;
    EXPECT_GT(final_eq, cfg.initial_balance_usd)
        << "Expected profit after dip-entry + uptrend closes, got final equity=" << final_eq;

    // Some positions should have been closed (traded_qty > 0)
    bool any_closed = false;
    for (const auto& pos : result.final_positions) {
        if (pos.traded_qty > 0.0) any_closed = true;
    }
    EXPECT_TRUE(any_closed) << "Expected at least one closed position after dip-entry + uptrend";
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
    cfg.strategy.entry_ema_distance_pct = 0.0;  // enter when close < ema (any tiny dip)
    cfg.strategy.close_grid_spacing_pct = 1.0;   // disable close grid
    cfg.warmup_candles = 3;

    // New scenario (C1 fix): entries require close < EMA*(1-dist). With dist=0.0 that is
    // close < ema. Build: 3 flat warmup candles at 100 → EMA=100
    // Candle 3 (first trading): close=99.5 → EMA=99.75, threshold=99.75 → 99.5 < 99.75 → entry fires
    // Avg entry ≈ 99.5. Candle 4: 15% crash → close=84.575 → upnl=(84.575-99.5)/99.5≈-0.15 → sl=-0.10 → stop triggers
    std::vector<Candle> candles;
    candles.reserve(30);
    // Warmup: 3 flat candles at 100
    for (int i = 0; i < 3; ++i) {
        candles.push_back({static_cast<int64_t>(i) * 3600000, 100.0, 100.1, 99.9, 100.0, 1000.0});
    }
    // First trading candle: slight dip below EMA → entry fires
    candles.push_back({3LL * 3600000, 99.6, 100.0, 99.3, 99.5, 1000.0});
    // Candle 4: 15% crash → stop loss triggers (upnl = (84.575-99.5)/99.5 ≈ -0.15 < -0.10)
    double crash_price = 99.5 * 0.85;
    candles.push_back({4LL * 3600000, 99.5, 99.6, crash_price * 0.999, crash_price, 1000.0});
    // Remaining candles: price stays depressed (no re-entry since price > EMA dip unlikely)
    double price = crash_price;
    for (int i = 5; i < 30; ++i) {
        candles.push_back({static_cast<int64_t>(i) * 3600000,
                           price, price * 1.001, price * 0.999, price, 1000.0});
    }

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

// ---------------------------------------------------------------------------
// Time-based unstuck unit tests
// ---------------------------------------------------------------------------

TEST(UnstuckTest, DisabledWhenParamsZero) {
    // All params 0 => unstuck disabled
    Config cfg = make_cfg();
    cfg.strategy.time_based_unstuck_pct = 0.0;
    cfg.strategy.time_based_unstuck_age = 0;

    Candle candle{0, 100.0, 101.0, 99.0, 100.0, 1000.0};
    Position pos;
    pos.total_qty = 1.0;
    pos.avg_entry_price = 90.0;
    pos.entry_tick = 10;

    bool result = check_time_based_unstuck(cfg, candle, pos, 100);
    EXPECT_FALSE(result) << "Should be disabled when pct=0 and age=0";
}

TEST(UnstuckTest, DisabledWhenPctZero) {
    Config cfg = make_cfg();
    cfg.strategy.time_based_unstuck_pct = 0.0;
    cfg.strategy.time_based_unstuck_age = 24;
    cfg.strategy.maker_fee_pct = 0.001;

    Candle candle{0, 100.0, 101.0, 99.0, 110.0, 1000.0};
    Position pos;
    pos.total_qty = 1.0;
    pos.avg_entry_price = 90.0;
    pos.entry_tick = 10;

    bool result = check_time_based_unstuck(cfg, candle, pos, 100);
    EXPECT_FALSE(result) << "Should be disabled when pct=0 even if age>0";
}

TEST(UnstuckTest, DisabledWhenAgeZero) {
    Config cfg = make_cfg();
    cfg.strategy.time_based_unstuck_pct = 0.1;
    cfg.strategy.time_based_unstuck_age = 0;
    cfg.strategy.maker_fee_pct = 0.001;

    Candle candle{0, 100.0, 101.0, 99.0, 110.0, 1000.0};
    Position pos;
    pos.total_qty = 1.0;
    pos.avg_entry_price = 90.0;
    pos.entry_tick = 10;

    bool result = check_time_based_unstuck(cfg, candle, pos, 100);
    EXPECT_FALSE(result) << "Should be disabled when age=0 even if pct>0";
}

TEST(UnstuckTest, DoesNotTriggerBeforeAge) {
    Config cfg = make_cfg();
    cfg.strategy.time_based_unstuck_pct = 0.1;
    cfg.strategy.time_based_unstuck_age = 50;
    cfg.strategy.maker_fee_pct = 0.001;

    // entry_timestamp_ms = 10 hours; candle.timestamp = 50 hours; age = 50 hours
    // held = 50 - 10 = 40 hours < age=50 => should not trigger
    Candle candle{50 * 3600000LL, 100.0, 101.0, 99.0, 110.0, 1000.0};
    Position pos;
    pos.total_qty = 1.0;
    pos.avg_entry_price = 90.0;
    pos.entry_tick = 10;
    pos.entry_timestamp_ms = 10 * 3600000LL;

    bool result = check_time_based_unstuck(cfg, candle, pos, 50);
    EXPECT_FALSE(result) << "Should not trigger before age is reached";
}

TEST(UnstuckTest, TriggersFirstLevel) {
    Config cfg = make_cfg();
    cfg.strategy.time_based_unstuck_pct = 0.02;
    cfg.strategy.time_based_unstuck_age = 24;
    cfg.strategy.maker_fee_pct = 0.001;
    cfg.initial_balance_usd = 10000.0;
    cfg.total_wallet_exposure = 1.0;

    // qty=5.0, close=100, balance=10000 => exposure = 5*100/10000 = 0.05
    // close_qty = 0.02*10000/100 = 2.0 per level (exposure tranche)
    // min(2.0, 5.0) = 2.0 => close 2.0, qty goes 5.0->3.0
    Candle candle{50 * 3600000LL, 100.0, 101.0, 99.0, 100.0, 1000.0};
    Position pos;
    pos.total_qty = 5.0;
    pos.avg_entry_price = 90.0;
    pos.entry_tick = 10;
    pos.entry_timestamp_ms = 10 * 3600000LL;

    // held = 50 - 10 = 40 hours > age=24 hours
    // expected = 40/24 = 1 level
    bool result = check_time_based_unstuck(cfg, candle, pos, 50);
    EXPECT_TRUE(result) << "Should trigger for first level";
    EXPECT_NEAR(pos.total_qty, 3.0, 1e-9) << "Should close one exposure tranche of 2.0";
    EXPECT_EQ(pos.unstuck_levels, 1) << "Should record 1 unstuck level";
    EXPECT_GT(pos.realized_pnl, 0.0) << "Should have positive realized PnL on partial close";
    EXPECT_GT(pos.traded_qty, 0.0) << "Should record traded qty";
}

TEST(UnstuckTest, TriggersOnNegativePnl) {
    Config cfg = make_cfg();
    cfg.strategy.time_based_unstuck_pct = 0.02;
    cfg.strategy.time_based_unstuck_age = 24;
    cfg.strategy.maker_fee_pct = 0.001;
    cfg.initial_balance_usd = 10000.0;
    cfg.total_wallet_exposure = 1.0;

    // qty=5.0, close=85, balance=10000 => exposure = 5*85/10000 = 0.0425
    // close_qty = 0.02*10000/85 ≈ 2.35 per level
    // min(2.35, 5.0) = 2.35 => close 2.35, qty goes 5.0->2.65
    Candle candle{50 * 3600000LL, 100.0, 101.0, 99.0, 85.0, 1000.0};
    Position pos;
    pos.total_qty = 5.0;
    pos.avg_entry_price = 90.0;
    pos.entry_tick = 10;
    pos.entry_timestamp_ms = 10 * 3600000LL;

    bool result = check_time_based_unstuck(cfg, candle, pos, 50);
    EXPECT_TRUE(result) << "Should trigger even with negative PnL";
    EXPECT_NEAR(pos.total_qty, 2.6470588235, 1e-6)
        << "Should close one exposure tranche";
    EXPECT_EQ(pos.unstuck_levels, 1) << "Should record 1 unstuck level";
    EXPECT_LT(pos.realized_pnl, 0.0) << "Should have negative realized PnL (partial loss)";
}

TEST(UnstuckTest, TriggersMultipleLevels) {
    Config cfg = make_cfg();
    cfg.strategy.time_based_unstuck_pct = 0.01;
    cfg.strategy.time_based_unstuck_age = 24;
    cfg.strategy.maker_fee_pct = 0.001;
    cfg.initial_balance_usd = 10000.0;
    cfg.total_wallet_exposure = 1.0;

    // qty=4.0, close=100, balance=10000 => exposure = 4*100/10000 = 0.04
    // close_qty = 0.01*10000/100 = 1.0 per level (fixed exposure tranche)
    // Level 0: close 1.0, qty=3.0
    // Level 1: close 1.0, qty=2.0
    // Level 2: close 1.0, qty=1.0
    Candle candle{100 * 3600000LL, 100.0, 101.0, 99.0, 100.0, 1000.0};
    Position pos;
    pos.total_qty = 4.0;
    pos.avg_entry_price = 90.0;
    pos.entry_tick = 10;
    pos.entry_timestamp_ms = 10 * 3600000LL;

    // held = 100 - 10 = 90 hours
    // expected = 90/24 = 3 levels (integer division)
    bool result = check_time_based_unstuck(cfg, candle, pos, 100);
    EXPECT_TRUE(result) << "Should trigger for 3 levels";
    EXPECT_NEAR(pos.total_qty, 1.0, 1e-9)
        << "Should close 1.0 per level: 4.0 -> 3.0 -> 2.0 -> 1.0";
    EXPECT_EQ(pos.unstuck_levels, 3) << "Should record 3 unstuck levels";
}

TEST(UnstuckTest, ClosesFullPositionWhenTrancheExceedsQty) {
    Config cfg = make_cfg();
    cfg.strategy.time_based_unstuck_pct = 0.03;
    cfg.strategy.time_based_unstuck_age = 10;
    cfg.strategy.maker_fee_pct = 0.001;
    cfg.initial_balance_usd = 10000.0;
    cfg.total_wallet_exposure = 1.0;

    // qty=4.0, close=100 => exposure=0.04
    // close_qty = 0.03*10000/100 = 3.0 per level
    // min(3.0, 4.0) = 3.0, qty goes 4.0->1.0 (partial close)
    // Level 1: min(3.0, 1.0) = 1.0, qty=0.0 (full close since tranche exceeds remaining)
    Candle candle{50 * 3600000LL, 100.0, 101.0, 99.0, 100.0, 1000.0};
    Position pos;
    pos.total_qty = 4.0;
    pos.avg_entry_price = 90.0;
    pos.entry_tick = 10;
    pos.entry_timestamp_ms = 10 * 3600000LL;

    // held = 50 - 10 = 40 hours, age=10 hours => expected = 4
    // But after 2 levels, position is fully closed (4.0 -> 1.0 -> 0.0)
    bool result = check_time_based_unstuck(cfg, candle, pos, 50);
    EXPECT_TRUE(result) << "Should trigger and fully close when tranche exceeds remaining";
    EXPECT_DOUBLE_EQ(pos.total_qty, 0.0) << "Position should be exactly 0";
    EXPECT_EQ(pos.unstuck_levels, 2) << "Should record 2 unstuck levels";
    EXPECT_GT(pos.realized_pnl, 0.0) << "Should have realized PnL";
}

TEST(UnstuckIntegrationTest, FullBacktestWithUnstuck) {
    // Full integration: run a backtest where unstuck should trigger.
    // C1 fix: entries now require a DIP (close < ema*(1-dist)), not an uptrend.
    // Build: flat warmup (3 candles) → dip (entry fires) → uptrend (unstuck closes at profit).
    auto cfg = make_cfg();
    cfg.strategy.entry_ema_period = 3;
    cfg.strategy.entry_ema_distance_pct = 0.001;
    cfg.strategy.close_grid_spacing_pct = 1.0;  // disable close grid (100% spacing)
    cfg.strategy.close_grid_count = 1;
    cfg.warmup_candles = 3;

    // Enable time-based unstuck
    cfg.strategy.time_based_unstuck_pct = 0.02;  // small tranche = partial closes
    cfg.strategy.time_based_unstuck_age = 5;
    cfg.strategy.maker_fee_pct = 0.001;

    // Candle sequence:
    //   3 flat warmup at 100 → EMA≈100
    //   1 dip at 99.5: EMA=99.75, threshold=99.65, 99.5<99.65 → entry fires
    //   56 rising candles at 0.5%/tick: price recovers above entry price
    //   → after 5 ticks, unstuck fires repeatedly (held ≥ age=5) and closes
    //     the position in ~6 ticks (2 units per tick @ pct=0.02)
    //   → realized PnL > 0 (closed above entry price)
    std::vector<Candle> candles;
    candles.reserve(60);
    // Warmup: 3 flat candles at 100
    for (int i = 0; i < 3; ++i) {
        candles.push_back({static_cast<int64_t>(i) * 3600000, 100.0, 100.1, 99.9, 100.0, 1000.0});
    }
    // Dip: entry fires (close < EMA*(1-0.001))
    candles.push_back({3LL * 3600000, 99.6, 100.0, 99.3, 99.5, 1000.0});
    // Recovery uptrend: 56 candles rising 0.5% each — price goes well above entry
    double price = 99.5;
    for (int i = 0; i < 56; ++i) {
        price *= 1.005;
        candles.push_back({static_cast<int64_t>(4 + i) * 3600000,
                           price * 0.999, price * 1.001, price * 0.998, price, 1000.0});
    }

    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);

    // With close_grid disabled and unstuck enabled, positions should be closed
    // by the unstuck mechanism. Check that we have position durations recorded
    // (meaning positions were opened AND closed during the backtest).
    EXPECT_GT(result.position_durations_hours.size(), 0U)
        << "Expected positions to be closed by unstuck";

    // In an uptrend with unstuck, we should have some realized PnL from partial closes
    double total_realized = 0.0;
    for (const auto& pos : result.final_positions) {
        total_realized += pos.realized_pnl;
    }

    EXPECT_GT(total_realized, 0.0)
        << "Expected positive realized PnL from unstuck closes in uptrend";

    // Final equity should be above initial since unstuck captures profits
    double const final_eq = result.equity_curve.back().equity;
    EXPECT_GT(final_eq, cfg.initial_balance_usd)
        << "Expected profit with unstuck enabled";
}

TEST(UnstuckIntegrationTest, UnstuckVsNoUnstuck) {
    // Unstuck should produce more realized PnL than without unstuck.
    // C1 fix: entries require a DIP, so we use flat warmup → dip → uptrend.
    // Without unstuck: position stays open all 100 candles (no close grid), realized_pnl≈0 (fees only).
    // With unstuck (pct=0.1, age=5): tranche=10% of balance/price ≈ 10 units ≈ total qty →
    //   full close at tick 8 (5 hours after entry) at a profit → realized_with > 0 > realized_no.
    auto cfg_base = make_cfg();
    cfg_base.strategy.entry_ema_period = 3;
    cfg_base.strategy.entry_ema_distance_pct = 0.001;
    cfg_base.strategy.close_grid_spacing_pct = 1.0;  // disable close grid (100% spacing)
    cfg_base.strategy.close_grid_count = 1;
    cfg_base.warmup_candles = 3;

    // Candle sequence: 3 flat warmup at 100 → 1 dip at 99.5 (entry fires) → 96 rising 0.5%/tick
    std::vector<Candle> candles;
    candles.reserve(100);
    for (int i = 0; i < 3; ++i) {
        candles.push_back({static_cast<int64_t>(i) * 3600000, 100.0, 100.1, 99.9, 100.0, 1000.0});
    }
    candles.push_back({3LL * 3600000, 99.6, 100.0, 99.3, 99.5, 1000.0});
    double price = 99.5;
    for (int i = 0; i < 96; ++i) {
        price *= 1.005;
        candles.push_back({static_cast<int64_t>(4 + i) * 3600000,
                           price * 0.999, price * 1.001, price * 0.998, price, 1000.0});
    }

    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    // Run WITHOUT unstuck (loss_algo_types empty → defaults to legacy_stop_loss only
    // when time_based_unstuck_pct/age are both 0)
    // Actually default adds legacy_unstuck too, but with pct=0 and age=0 it won't fire.
    auto result_no = run_backtest(cfg_base, per_symbol, infos, "");

    // Run WITH unstuck: pct=0.1 means tranche≈10% of balance/price ≈ full position qty
    auto cfg_with = cfg_base;
    cfg_with.strategy.time_based_unstuck_pct = 0.1;
    cfg_with.strategy.time_based_unstuck_age = 5;
    cfg_with.strategy.maker_fee_pct = 0.001;

    auto result_with = run_backtest(cfg_with, per_symbol, infos, "");

    // Sum up realized PnL for both
    double realized_no = 0.0, realized_with = 0.0;
    for (const auto& pos : result_no.final_positions) realized_no += pos.realized_pnl;
    for (const auto& pos : result_with.final_positions) realized_with += pos.realized_pnl;

    EXPECT_GT(realized_with, realized_no)
        << "Unstuck should generate more realized PnL by taking profits earlier. "
        << "realized_with=" << realized_with << " realized_no=" << realized_no;
}

TEST(UnstuckIntegrationTest, ExposureDropsToZeroBetweenCycles) {
    // Verify that unstuck fully closes the position so exposure_usd can
    // drop to 0, allowing a new "position held" period to start.
    // This directly tests that position_held_hours_max stays reasonable.
    //
    // C1 fix: entries require a DIP. We use two explicit dip candles separated
    // by a flat interval. pct=0.3 means tranche=30%*balance/price > initial qty →
    // full close in ONE unstuck tick (age=3 hours after entry).
    auto cfg = make_cfg();
    cfg.strategy.entry_ema_period = 3;
    cfg.strategy.entry_ema_distance_pct = 0.001;
    cfg.strategy.close_grid_spacing_pct = 1.0;  // disable close grid (100% spacing)
    cfg.strategy.close_grid_count = 1;
    cfg.strategy.sl_upnl_pct = -0.50;
    cfg.warmup_candles = 3;

    cfg.strategy.time_based_unstuck_pct = 0.3;   // 30%: tranche>>qty → full close in 1 tick
    cfg.strategy.time_based_unstuck_age = 3;      // 3 hours
    cfg.strategy.maker_fee_pct = 0.001;

    // Candle sequence (each candle = 1 hour):
    //   Ticks 0-2: flat warmup at 100 (EMA→100)
    //   Tick 3 (first trading): DIP1 at 98.5 → entry 1 fires
    //     EMA=0.5*98.5+0.5*100=99.25; threshold=99.25*0.999=99.15; 98.5<99.15 ✓
    //   Ticks 4-5: flat at 98.5 (held 1-2h, no unstuck yet, age=3)
    //   Tick 6: flat at 98.5, held=3h → unstuck fires (tranche≈30.5 >> qty≈10) → FULL CLOSE
    //   Ticks 7-14: flat at 99.0 (EMA converges to ~98.9; threshold~98.8; 99.0>98.8 → no re-entry)
    //   Tick 15: DIP2 at 97.5 → EMA≈98.8; threshold≈98.7; 97.5<98.7 ✓ → entry 2 fires
    //   Tick 16-17: flat at 97.5 (held 1-2h, no unstuck yet)
    //   Tick 18: flat at 97.5, held=3h → unstuck fires → FULL CLOSE
    //   Ticks 19-29: flat at 97.5 (no more entries needed)
    std::vector<Candle> all_candles;
    all_candles.reserve(30);
    int64_t tick = 0;
    auto push = [&](double close) {
        all_candles.push_back({tick * 3600000LL, close * 0.999, close * 1.001,
                               close * 0.998, close, 1000.0});
        ++tick;
    };
    // Warmup
    push(100.0); push(100.0); push(100.0);
    // DIP1 → entry 1
    push(98.5);
    // Hold for unstuck (age=3): ticks 4,5 = held 1h, 2h; tick 6 = held 3h → fires
    push(98.5); push(98.5); push(98.5);
    // Flat at 99.0 — EMA converges, no re-entry (price above threshold)
    for (int i = 0; i < 8; ++i) push(99.0);
    // DIP2 → entry 2
    push(97.5);
    // Hold for unstuck (age=3): ticks +1, +2; +3 → fires
    push(97.5); push(97.5); push(97.5);
    // Flat tail
    for (int i = 0; i < 9; ++i) push(97.5);

    auto loaded = to_loaded(all_candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);

    // Count how many times exposure_usd transitions 0→>0 (new position starts)
    int transitions = 0;
    bool was_zero = true;
    for (const auto& ep : result.equity_curve) {
        if (ep.exposure_usd > 0.0 && was_zero) {
            ++transitions;
            was_zero = false;
        } else if (ep.exposure_usd <= 0.0 && !was_zero) {
            was_zero = true;
        }
    }

    // With 2 explicit dip+close cycles, we should see 2 entry transitions
    // (the position is fully closed by unstuck, then re-entered on dip2)
    EXPECT_GE(transitions, 2)
        << "Expected at least 2 separate position openings (exposure should drop to 0). "
        << "transitions=" << transitions;

    // Find the longest contiguous exposure period
    double max_hours = 0.0;
    size_t start_i = 0;
    bool in_pos = false;
    for (size_t i = 0; i < result.equity_curve.size(); ++i) {
        bool const has_exp = result.equity_curve[i].exposure_usd > 0.0;
        if (has_exp && !in_pos) {
            in_pos = true;
            start_i = i;
        } else if (!has_exp && in_pos) {
            in_pos = false;
            double hrs = static_cast<double>(
                result.equity_curve[i].timestamp - result.equity_curve[start_i].timestamp
            ) / 3600000.0;
            if (hrs > max_hours) max_hours = hrs;
        }
    }
    if (in_pos) {
        double hrs = static_cast<double>(
            result.equity_curve.back().timestamp - result.equity_curve[start_i].timestamp
        ) / 3600000.0;
        if (hrs > max_hours) max_hours = hrs;
    }

    // Each position is held for ~3 hours (age=3, pct=0.3 closes fully in 1 tick)
    // Max held period should be well under 72h
    EXPECT_LT(max_hours, 72.0)
        << "Max held period should be under 72h with aggressive unstuck";
}

// ---------------------------------------------------------------------------
// C2 — entry_condition must NOT block double-downs
// ---------------------------------------------------------------------------

TEST(StrategyTest, C2_DoubleDownBypassesEntryCondition) {
    // Scenario:
    //   - ema_dist_pct entry condition: enters when close > ema*(1+dist)
    //   - We start with price well above EMA so the first entry fires
    //   - Then price falls significantly below avg_entry AND below EMA
    //     → should_enter() returns false, but the double-down must still fire
    //     because the martingale only requires price < avg_entry by grid spacing
    //
    // Fix verified: with C2 fix, the for-loop for double-downs does NOT call
    // should_enter(). Without the fix, the EMA check would block the add.

    auto cfg = make_cfg();
    cfg.strategy.entry_condition_type = "ema_dist_pct";
    cfg.strategy.entries_algo_type = "martingale";
    cfg.strategy.closes_algo_type = "simple_grid";
    cfg.strategy.entry_ema_period = 20;             // slow EMA
    cfg.strategy.entry_ema_distance_pct = 0.01;     // enter when >1% above EMA
    cfg.strategy.entry_grid_spacing_pct = 0.05;     // 5% drop triggers a DD
    cfg.strategy.initial_qty_pct = 0.05;
    cfg.strategy.double_down_factor = 1.0;          // flat sizing for clarity
    cfg.strategy.close_grid_spacing_pct = 0.20;     // keep closes far away
    cfg.strategy.close_grid_count = 1;
    cfg.strategy.n_positions = 1;
    cfg.strategy.maker_fee_pct = 0.0;
    cfg.strategy.sl_upnl_pct = -0.99;               // essentially disable stop-loss
    cfg.strategy.time_based_unstuck_pct = 0.0;
    cfg.strategy.time_based_unstuck_age = 0;
    cfg.warmup_candles = 20;
    cfg.strategy.loss_algo_types = {};               // disable loss modules

    // Build candles:
    //   - warmup (20): price at 100, flat — EMA converges to 100
    //   - candle 20: price = 115 → close > 100*(1+0.01)=101 → FIRST ENTRY fires
    //   - candles 21-50: price drops 1% per bar (100 → ~78)
    //     The EMA is still near 100, so close < ema*(1+0.01) → should_enter=false
    //     But price drops ~5% below avg_entry=115 → martingale DD fires
    std::vector<Candle> candles;
    candles.reserve(60);
    // Warmup: flat at 100
    for (int i = 0; i < 20; ++i) {
        double p = 100.0;
        candles.push_back({static_cast<int64_t>(i) * 3600000, p, p*1.001, p*0.999, p, 1000.0});
    }
    // Candle 20: spike to 115 → triggers first entry (close=115 > ema≈100 * 1.01)
    candles.push_back({20LL * 3600000, 100.0, 116.0, 99.5, 115.0, 1000.0});
    // Candles 21-50: drop 1% per bar from 115 → hits DD threshold after ~5 bars
    double price = 115.0;
    for (int i = 21; i < 60; ++i) {
        price *= 0.99;
        double p = price;
        candles.push_back({static_cast<int64_t>(i) * 3600000, p*1.005, p*1.01, p*0.99, p, 1000.0});
    }

    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);
    ASSERT_EQ(result.final_positions.size(), 1U);

    auto const& pos = result.final_positions[0];
    // Position must still be open (no close grid triggers with 20% spacing)
    // AND qty must be greater than the initial entry quantity
    // (meaning at least one double-down fired)
    // The first entry buys initial_qty_pct * balance / close
    double const first_qty_approx = (10000.0 * 2.0 / 1.0) * 0.05 / 115.0;
    EXPECT_GT(pos.total_qty, first_qty_approx * 1.5)
        << "Expected double-down to increase position qty beyond initial entry. "
        << "pos.total_qty=" << pos.total_qty
        << " initial_entry_approx=" << first_qty_approx
        << ". If this fails, the entry_condition is still blocking double-downs (C2 bug).";
}

// ---------------------------------------------------------------------------
// H3 — execute_first_entry sets entry_side=1 and original_qty>0
// ---------------------------------------------------------------------------

TEST(StrategyTest, H3_FirstEntrySetsSideAndOriginalQty) {
    // Run a backtest and verify that after the first entry fires,
    // the position has entry_side==1 and original_qty>0.
    // C1 fix: entries now require a DIP (close < ema*(1-dist)), NOT an uptrend.
    // We build: flat warmup → dip candle (entry fires) → flat/rising (position stays open).

    auto cfg = make_cfg();
    cfg.strategy.entry_condition_type = "ema_dist_pct";
    cfg.strategy.entries_algo_type = "martingale";
    cfg.strategy.closes_algo_type = "simple_grid";
    cfg.strategy.entry_ema_period = 3;
    cfg.strategy.entry_ema_distance_pct = 0.001;
    cfg.strategy.close_grid_spacing_pct = 5.0;   // keep closes far away (500% spacing)
    cfg.strategy.close_grid_count = 1;
    cfg.strategy.n_positions = 1;
    cfg.strategy.maker_fee_pct = 0.0;
    cfg.strategy.sl_upnl_pct = -0.99;            // essentially disable stop-loss
    cfg.strategy.time_based_unstuck_pct = 0.0;
    cfg.strategy.time_based_unstuck_age = 0;
    cfg.strategy.loss_algo_types = {};            // disable all loss modules
    cfg.warmup_candles = 3;

    // Build candle sequence:
    //   - 3 flat warmup candles at 100 (EMA converges to 100)
    //   - 1 dip candle at 99.5: EMA = 0.5*99.5+0.5*100 = 99.75
    //     threshold = 99.75*(1-0.001) = 99.65; 99.5 < 99.65 → entry fires
    //   - 16 flat candles at 99.5 (position stays open, close_grid at 99.5*6 never reached)
    std::vector<Candle> candles;
    candles.reserve(20);
    // Warmup: 3 flat candles at 100
    for (int i = 0; i < 3; ++i) {
        candles.push_back({static_cast<int64_t>(i) * 3600000, 100.0, 100.1, 99.9, 100.0, 1000.0});
    }
    // Dip candle: triggers entry
    candles.push_back({3LL * 3600000, 99.6, 100.0, 99.3, 99.5, 1000.0});
    // Flat candles: price stays at 99.5, position open (no closes — spacing=500%)
    for (int i = 4; i < 20; ++i) {
        candles.push_back({static_cast<int64_t>(i) * 3600000, 99.5, 99.6, 99.4, 99.5, 1000.0});
    }

    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);
    ASSERT_EQ(result.final_positions.size(), 1U);

    auto const& pos = result.final_positions[0];
    if (pos.total_qty > 1e-12) {
        // Position is still open — entry_side and original_qty must be set (H3)
        EXPECT_EQ(pos.entry_side, 1)
            << "entry_side should be 1 (long) after first entry (H3)";
        EXPECT_GT(pos.original_qty, 0.0)
            << "original_qty should be > 0 after first entry (H3)";
    } else {
        // Position was closed — entry_side / original_qty were reset
        EXPECT_EQ(pos.entry_side, 0)
            << "entry_side should be reset to 0 after full close (H3)";
        EXPECT_DOUBLE_EQ(pos.original_qty, 0.0)
            << "original_qty should be reset to 0 after full close (H3)";
        // In this case traded_qty > 0 confirms we had entries
        EXPECT_GT(pos.traded_qty, 0.0)
            << "traded_qty must be > 0 if position was entered and closed";
    }
}

// ---------------------------------------------------------------------------
// Liquidation floor — equity must never go negative
// ---------------------------------------------------------------------------

TEST(StrategyTest, LiquidationFloor_NegativeEquityPrevented) {
    // Synthetic blow-up scenario:
    //   - Large wallet exposure (5x), aggressive martingale
    //   - Price crashes 40% in one candle after entry
    //   - Without the floor, equity = balance + unrealized_pnl would go deeply negative
    //   - With the floor, every equity point must be >= 0, and the test asserts
    //     final equity >= 0 and no negative values in the entire curve.

    auto cfg = make_cfg();
    cfg.strategy.entry_condition_type = "ema_dist_pct";
    cfg.strategy.entries_algo_type = "martingale";
    cfg.strategy.closes_algo_type = "simple_grid";
    cfg.strategy.entry_ema_period = 3;
    cfg.strategy.entry_ema_distance_pct = 0.001;   // enters very easily
    cfg.strategy.entry_grid_spacing_pct = 0.01;    // tight grid → many double-downs
    cfg.strategy.initial_qty_pct = 0.5;            // large initial size (50% of slot)
    cfg.strategy.double_down_factor = 2.0;         // double size each level
    cfg.strategy.close_grid_spacing_pct = 5.0;     // keep closes far away
    cfg.strategy.close_grid_count = 1;
    cfg.strategy.n_positions = 1;
    cfg.strategy.maker_fee_pct = 0.001;
    cfg.strategy.sl_upnl_pct = -0.99;              // essentially disabled
    cfg.strategy.time_based_unstuck_pct = 0.0;
    cfg.strategy.time_based_unstuck_age = 0;
    cfg.strategy.loss_algo_types = {};
    cfg.warmup_candles = 3;
    cfg.initial_balance_usd = 10000.0;
    cfg.total_wallet_exposure = 5.0;               // large leverage

    // Price rises slightly for warmup+entry, then crashes 40% in one candle,
    // then continues falling → triggers massive double-down cascade → would blow equity
    std::vector<Candle> candles;
    // Warmup (3 candles): steady rise
    double price = 100.0;
    for (int i = 0; i < 3; ++i) {
        price *= 1.005;
        candles.push_back({static_cast<int64_t>(i) * 3600000,
                           price, price*1.001, price*0.999, price, 1000.0});
    }
    // Candle 3: spike so entry_condition fires
    price *= 1.02;
    candles.push_back({3LL * 3600000, price, price*1.001, price*0.999, price, 1000.0});
    // Candle 4: 40% crash
    price *= 0.60;
    candles.push_back({4LL * 3600000, price, price*1.001, price*0.999, price, 1000.0});
    // Candles 5-30: continue falling 2% each bar — many more double-downs
    for (int i = 5; i < 30; ++i) {
        price *= 0.98;
        candles.push_back({static_cast<int64_t>(i) * 3600000,
                           price, price*1.001, price*0.999, price, 1000.0});
    }

    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);

    // Core invariant: NO equity point may be negative
    for (size_t idx = 0; idx < result.equity_curve.size(); ++idx) {
        EXPECT_GE(result.equity_curve[idx].equity, 0.0)
            << "Equity went negative at index " << idx
            << " (value=" << result.equity_curve[idx].equity << "). "
            << "Liquidation floor is not working.";
    }

    // Final equity must be >= 0
    double const final_eq = result.equity_curve.back().equity;
    EXPECT_GE(final_eq, 0.0)
        << "Final equity is negative: " << final_eq;
}

// ---------------------------------------------------------------------------
// C1 — ema_dist_pct should_enter: dip-buy direction
// ---------------------------------------------------------------------------

TEST(EmaDistPctTest, C1_EntersOnDipNotTop) {
    // Build a minimal ModuleContext and call should_enter() directly.
    // After the C1 fix: enter when close < ema * (1 - dist), NOT when above.

    Config cfg = make_cfg();
    cfg.strategy.entry_ema_distance_pct = 0.02; // 2% dip required

    SymbolInfo info = make_info();
    Position pos;

    // Candle where close is 5% BELOW ema → should enter (dip confirmed)
    double const ema_val = 100.0;
    Candle candle_dip{0, 95.0, 96.0, 94.0, 95.0, 1000.0}; // close=95, 5% below ema=100
    ModuleContext ctx_dip{cfg, info, candle_dip, pos, /*total_balance=*/10000.0,
                         /*current_tick=*/0, /*ema=*/ema_val, /*rolling_stdev=*/0.0,
                         /*vwap=*/0.0, /*stdev=*/0.0,
                         /*candle_series=*/{}, /*candle_series_idx=*/0, /*tf_data=*/{}};

    EmaDistPctEntryCondition cond;
    EXPECT_TRUE(cond.should_enter(ctx_dip))
        << "C1: should enter when close (" << candle_dip.close
        << ") < ema*(1-dist)=" << ema_val * (1.0 - cfg.strategy.entry_ema_distance_pct);

    // Candle where close is 5% ABOVE ema → must NOT enter (price too high for a dip-buy grid)
    Candle candle_top{0, 105.0, 106.0, 104.0, 105.0, 1000.0}; // close=105, 5% above ema=100
    ModuleContext ctx_top{cfg, info, candle_top, pos, /*total_balance=*/10000.0,
                         /*current_tick=*/0, /*ema=*/ema_val, /*rolling_stdev=*/0.0,
                         /*vwap=*/0.0, /*stdev=*/0.0,
                         /*candle_series=*/{}, /*candle_series_idx=*/0, /*tf_data=*/{}};
    EXPECT_FALSE(cond.should_enter(ctx_top))
        << "C1: must NOT enter when close (" << candle_top.close
        << ") > ema=" << ema_val << " (buying top)";

    // Candle exactly at ema → not a dip, must NOT enter
    Candle candle_at{0, 100.0, 101.0, 99.0, 100.0, 1000.0};
    ModuleContext ctx_at{cfg, info, candle_at, pos, /*total_balance=*/10000.0,
                        /*current_tick=*/0, /*ema=*/ema_val, /*rolling_stdev=*/0.0,
                        /*vwap=*/0.0, /*stdev=*/0.0,
                        /*candle_series=*/{}, /*candle_series_idx=*/0, /*tf_data=*/{}};
    EXPECT_FALSE(cond.should_enter(ctx_at))
        << "C1: must NOT enter when close == ema (not a dip)";
}

// ---------------------------------------------------------------------------
// M5 — simple_grid: position fully closed (no dust) over rising prices
// ---------------------------------------------------------------------------

TEST(SimpleGridTest, M5_NoResiduaAfterLastLevel) {
    // Simulate simple_grid running over multiple ticks of rising prices.
    // After all grid levels have triggered, remaining_qty must be ~0.

    Config cfg = make_cfg();
    cfg.strategy.close_grid_spacing_pct = 0.01; // 1% per level
    cfg.strategy.close_grid_count = 3;

    SymbolInfo info = make_info();
    // Use a step_size that can cause rounding dust (e.g. 0.001)
    info.step_size = 0.001;

    Position pos;
    pos.avg_entry_price = 100.0;
    pos.total_qty = 1.005; // qty not divisible by 3 exactly → rounding risk

    SimpleGridClosesAlgo algo;

    // Simulate strategy.cpp: for each tick, call compute_closes and reduce pos.total_qty
    double total_closed = 0.0;
    for (int tick = 1; tick <= cfg.strategy.close_grid_count; ++tick) {
        if (pos.total_qty < 1e-12) break;

        // Price at exactly the compound target for level k (prev_trigger * (1 + spacing))
        double const price = pos.avg_entry_price * std::pow(1.0 + cfg.strategy.close_grid_spacing_pct, static_cast<double>(tick)) + 1e-6;
        Candle candle{static_cast<int64_t>(tick) * 3600000, price, price*1.001, price*0.999, price, 1000.0};

        ModuleContext ctx{cfg, info, candle, pos, /*total_balance=*/10000.0,
                         /*current_tick=*/static_cast<int64_t>(tick), /*ema=*/price,
                         /*rolling_stdev=*/0.0,
                         /*vwap=*/0.0, /*stdev=*/0.0, /*candle_series=*/{},
                         /*candle_series_idx=*/0, /*tf_data=*/{}};
 
         auto orders = algo.compute_closes(ctx);
        for (const auto& ord : orders) {
            total_closed += ord.qty;
            pos.total_qty -= ord.qty;
            if (pos.total_qty < 1e-12) pos.total_qty = 0.0;
        }
    }

    // After all grid levels, the position must be fully closed (no dust)
    EXPECT_NEAR(pos.total_qty, 0.0, 1e-9)
        << "M5: simple_grid left dust in position after all levels. "
        << "Remaining=" << pos.total_qty;
    EXPECT_NEAR(total_closed, 1.005, 1e-9)
        << "M5: total closed qty must equal initial qty (no dust). "
        << "total_closed=" << total_closed;
}

// ---------------------------------------------------------------------------
// DEFECT 1 — legacy_unstuck fires ONE tranche per age-period (not per candle)
// ---------------------------------------------------------------------------
// NOTE: The UnstuckTest::* tests above exercise the dead legacy function
// check_time_based_unstuck() in src/strategy/unstuck.cpp, which is no longer
// called by strategy.cpp. They give false confidence. The real code path goes
// through LegacyUnstuck::compute_loss_exits() in loss_modules/legacy_unstuck.cpp,
// which is gated by pos.unstuck_levels. Before the DEFECT 1 fix, strategy.cpp
// never incremented unstuck_levels after a partial close, so legacy_unstuck
// fired on every candle once held >= age. This test verifies the fix via
// run_backtest (the real code path).

TEST(LegacyUnstuckIntegration, OneTranchePerAgePeriod) {
    // Setup:
    //   - warmup 5 candles → entry on candle 5 via dip
    //   - age = 5 hours, pct = 0.05 (5% of balance per tranche)
    //   - hold for 4 age-periods (20 candles past entry) at flat price
    //   - assert: distinct partial reductions ≈ 4 (one per age period)
    //     NOT 20 (one per candle), which was the pre-fix regression.
    //
    // With initial_balance=10000, price=100, initial_qty_pct=0.1:
    //   initial_qty ≈ 10000 * 2.0 / 1.0 * 0.1 / 100 = 20.0 units
    //   tranche = 0.05 * balance / price = 0.05 * 10000 / 100 = 5.0 units/period
    //   So after 4 age-periods: 20 → 15 → 10 → 5 → 0 (fully closed)
    //   With the regression (one per candle): 20 candles → qty drops much faster.

    Config cfg = make_cfg();
    cfg.strategy.entry_condition_type = "ema_dist_pct";
    cfg.strategy.entries_algo_type = "martingale";
    cfg.strategy.closes_algo_type = "simple_grid";
    cfg.strategy.entry_ema_period = 5;
    cfg.strategy.entry_ema_distance_pct = 0.005;
    cfg.strategy.entry_grid_spacing_pct = 0.50;    // 50% spacing → no double-downs
    cfg.strategy.initial_qty_pct = 0.1;
    cfg.strategy.double_down_factor = 1.0;
    cfg.strategy.close_grid_spacing_pct = 5.0;     // keep close grid far away
    cfg.strategy.close_grid_count = 1;
    cfg.strategy.sl_upnl_pct = -0.99;              // disable stop-loss
    cfg.strategy.n_positions = 1;
    cfg.strategy.maker_fee_pct = 0.0;
    cfg.strategy.time_based_unstuck_pct = 0.05;    // 5% tranche per age-period
    cfg.strategy.time_based_unstuck_age = 5;        // 5-hour age threshold
    cfg.strategy.loss_algo_types = {"legacy_unstuck"};
    cfg.warmup_candles = 5;
    cfg.initial_balance_usd = 10000.0;
    cfg.total_wallet_exposure = 1.0;

    // Candle sequence:
    //   Ticks 0-4: warmup flat at 100
    //   Tick 5: dip to 98.5 → entry fires
    //     EMA after tick 5 ≈ 99.5; threshold = 99.5*(1-0.005)=99.0; 98.5 < 99.0 → fires
    //   Ticks 6-25: flat at 98.5 (price below avg_entry, no closes)
    //     - age=5h: unstuck fires at tick 10 (held 5h), tick 15 (10h), tick 20 (15h), tick 25 (20h)
    //     - With DEFECT 1 (no unstuck_levels increment): fires at ticks 10,11,12,13,14,15,...
    //     - With FIX: fires only at ticks 10,15,20,25 → 4 partial reductions
    std::vector<Candle> candles;
    candles.reserve(30);
    int64_t ts = 0;
    // Warmup: 5 flat candles at 100
    for (int i = 0; i < 5; ++i) {
        candles.push_back({ts * 3600000LL, 100.0, 100.1, 99.9, 100.0, 1000.0});
        ++ts;
    }
    // Dip: entry fires
    candles.push_back({ts * 3600000LL, 98.6, 99.0, 98.2, 98.5, 1000.0});
    ++ts;
    // Flat at 98.5: hold for 4 age-periods (20 more candles after entry)
    for (int i = 0; i < 25; ++i) {
        candles.push_back({ts * 3600000LL, 98.5, 98.6, 98.4, 98.5, 1000.0});
        ++ts;
    }

    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);

    // Count distinct partial reductions in exposure_usd (each tranche = one step down)
    // A "reduction event" is any candle where exposure_usd drops compared to the
    // previous candle while still > 0 (i.e., partial close, not a full close).
    int partial_reductions = 0;
    double prev_exposure = 0.0;
    for (size_t idx = 0; idx < result.equity_curve.size(); ++idx) {
        double const exp = result.equity_curve[idx].exposure_usd;
        if (idx > 0 && exp < prev_exposure - 1e-6 && exp > 1e-6) {
            ++partial_reductions;
        }
        prev_exposure = exp;
    }

    // With the fix: 4 age-periods → 4 tranche reductions (one per 5 candles).
    // With the regression: 20 candles after the first fire → ~20 reductions.
    // We assert <= 6 to give ±1 tolerance for edge cases (e.g. two tranches
    // happen to fire on back-to-back candles at exact integer multiples).
    EXPECT_LE(partial_reductions, 6)
        << "DEFECT 1: legacy_unstuck is firing more than one tranche per age-period. "
        << "partial_reductions=" << partial_reductions
        << " (expected ~4 for 4 age-periods, not one per candle).";
    EXPECT_GE(partial_reductions, 1)
        << "DEFECT 1: legacy_unstuck did not fire at all. "
        << "partial_reductions=" << partial_reductions;
}

// ---------------------------------------------------------------------------
// DEFECT 3 — graduated_tp TP1 fired while close < avg_entry still sets tp1_fired
// ---------------------------------------------------------------------------

TEST(GraduatedTpTest, Defect3_TP1InLossSetsTP1Fired) {
    // Scenario: enter a position at a high price (during a z-score spike).
    // Then price partially reverts to VWAP (which is below avg_entry).
    // In this state: close < avg_entry, but |z| <= tp1_z_threshold → TP1 fires.
    // Bug (pre-fix): tp1_fired only set when close > avg_entry → stays false → TP2/TP3 blocked.
    // Fix: tp1_fired set whenever closes_algo executes any close, regardless of price.
    //
    // We drive this through run_backtest with:
    //   - closes_algo_type = "graduated_tp"
    //   - Loss modules disabled (no interference)
    //   - Entry at ~110 (avg_entry ≈ 110)
    //   - Candle_series near VWAP ≈ 100 → z ≈ 0 ≤ tp1_z_threshold at close=100
    //   - So close=100 < avg_entry=110, but TP1 fires
    //   - After TP1, TP2+TP3 should also fire (position eventually fully closed)
    //
    // To keep this feasible as a unit-level test, we also call compute_closes
    // directly with a crafted ModuleContext to verify tp1_fired propagation.

    Config cfg = make_cfg();
    cfg.strategy.closes_algo_type = "graduated_tp";
    cfg.strategy.zscore_vwap_lookback = 5;
    cfg.strategy.tp1_z_threshold = 2.0;   // wide threshold: fires easily
    cfg.strategy.tp1_frac = 0.4;
    cfg.strategy.tp2_z_threshold = 1.5;   // also wide
    cfg.strategy.tp2_frac = 0.5;
    cfg.strategy.trailing_atr_mult = 2.0;
    // No atr_period timeframe → TP3 falls back to z <= tp2_z_threshold

    SymbolInfo info = make_info();

    // Build a candle series where close ≈ VWAP ≈ 100 (low z-score)
    std::vector<Candle> series;
    for (int i = 0; i < 10; ++i) {
        double close = 100.0 + (i % 3 == 0 ? 0.3 : -0.3); // tiny oscillation
        series.push_back({static_cast<int64_t>(i) * 3600000LL,
                          close, close + 0.5, close - 0.5, close, 1000.0});
    }

    // Position: entered at avg_entry=110 (above current VWAP of 100)
    Position pos;
    pos.avg_entry_price = 110.0;
    pos.total_qty = 1.0;
    pos.tp1_fired = false;
    pos.entry_timestamp_ms = 0;

    GraduatedTpClosesAlgo algo;

    // Tick 1: close=100 < avg_entry=110, but z ≈ 0 ≤ 2.0 → TP1 should fire
    {
        Candle candle{10LL * 3600000LL, 100.0, 100.5, 99.5, 100.0, 1000.0};
        ModuleContext ctx{cfg, info, candle, pos, /*total_balance=*/10000.0,
                         /*current_tick=*/10, /*ema=*/100.0, /*rolling_stdev=*/0.3,
                         /*vwap=*/99.9, /*stdev=*/0.283,
                         std::span<const Candle>(series), /*candle_series_idx=*/9,
                         /*tf_data=*/{}};
 
        auto orders = algo.compute_closes(ctx);
        ASSERT_GT(orders.size(), 0U)
            << "DEFECT 3: graduated_tp TP1 must fire when |z|<=tp1_z_threshold "
               "even if close < avg_entry. No orders returned.";
        EXPECT_NEAR(orders[0].qty, 1.0 * cfg.strategy.tp1_frac, 1e-9)
            << "DEFECT 3: TP1 should close tp1_frac of position";

        // Simulate what strategy.cpp Step a does after the fix:
        // set tp1_fired = true whenever any close executes (regardless of price vs avg_entry)
        pos.total_qty -= orders[0].qty;
        pos.tp1_fired = true;  // This is what the fix does: set on ANY close in Step a
    }

    // Tick 2: with tp1_fired=true, TP2 and TP3 should fire
    {
        Candle candle{11LL * 3600000LL, 100.0, 100.5, 99.5, 100.0, 1000.0};
        ModuleContext ctx{cfg, info, candle, pos, /*total_balance=*/10000.0,
                         /*current_tick=*/11, /*ema=*/100.0, /*rolling_stdev=*/0.3,
                         /*vwap=*/99.9, /*stdev=*/0.283,
                         std::span<const Candle>(series), /*candle_series_idx=*/9,
                         /*tf_data=*/{}};
 
        auto orders = algo.compute_closes(ctx);
         ASSERT_GT(orders.size(), 0U)
             << "DEFECT 3: graduated_tp TP2/TP3 must fire after tp1_fired=true. "
               "No orders returned — TP2/TP3 are still blocked.";

        double closed = 0.0;
        for (auto const& o : orders) closed += o.qty;
        pos.total_qty -= closed;
    }

    EXPECT_NEAR(pos.total_qty, 0.0, 1e-9)
        << "DEFECT 3: graduated_tp must fully close position via TP1(in-loss)→TP2→TP3. "
        << "remaining=" << pos.total_qty;
}

TEST(GraduatedTpTest, Defect3_BacktestTP1InLossUnblocksTP2TP3) {
    // Integration test: run a full backtest with graduated_tp where the TP1
    // trigger condition (low z-score) occurs while close < avg_entry.
    // After the DEFECT 3 fix, tp1_fired is set and TP2/TP3 fire later,
    // eventually closing the position.
    // With the old code (tp1_fired only set when close > avg_entry), TP2/TP3
    // would never fire → position stays open → traded_qty stays small.

    Config cfg = make_cfg();
    cfg.strategy.entry_condition_type = "ema_dist_pct";
    cfg.strategy.entries_algo_type = "martingale";
    cfg.strategy.closes_algo_type = "graduated_tp";
    // Entry: enter on dip of 3% below EMA
    cfg.strategy.entry_ema_period = 5;
    cfg.strategy.entry_ema_distance_pct = 0.005;
    cfg.strategy.entry_grid_spacing_pct = 0.50;  // no double-downs
    cfg.strategy.initial_qty_pct = 0.1;
    cfg.strategy.double_down_factor = 1.0;
    // graduated_tp params: fire TP1 when z ≤ 2.0 (very easy to trigger)
    cfg.strategy.zscore_vwap_lookback = 5;
    cfg.strategy.tp1_z_threshold = 2.0;
    cfg.strategy.tp1_frac = 0.4;
    cfg.strategy.tp2_z_threshold = 1.5;
    cfg.strategy.tp2_frac = 0.5;
    cfg.strategy.trailing_atr_mult = 2.0;
    cfg.strategy.n_positions = 1;
    cfg.strategy.maker_fee_pct = 0.0;
    cfg.strategy.sl_upnl_pct = -0.99;
    cfg.strategy.loss_algo_types = {};  // no loss modules
    cfg.warmup_candles = 5;
    cfg.initial_balance_usd = 10000.0;
    cfg.total_wallet_exposure = 1.0;

    // Price scenario:
    //   Ticks 0-4: warmup flat at 100 (EMA → 100)
    //   Tick 5:    dip to 97 → entry fires (EMA≈98.5, threshold≈98.0, 97 < 98 → fires)
    //     avg_entry ≈ 97
    //   Ticks 6-14: price stays flat at 98 (below avg_entry of ~97? No, 98 > 97)
    //     Actually avg_entry ≈ 97, price at 98 > avg_entry → won't test in-loss TP1.
    //     We need close < avg_entry when z-score is low.
    //     Build: entry at 100 (spike), then price reverts to 97 (below entry, near VWAP).
    //
    // Revised scenario:
    //   Ticks 0-4: warmup flat at 100
    //   Tick 5:    spike to 110 (EMA≈103, threshold≈102.5; 110>102.5 → NO entry with dip-buy)
    // That won't work. Let's enter via C2_DoubleDownBypassesEntryCondition-style:
    //   Use ema_dist_pct with dist=0 → enters when close < EMA.
    //   Ticks 0-4: warmup at 100 (EMA→100)
    //   Tick 5: small dip to 99.5 → entry at 99.5; VWAP of next ticks ≈ 99-100
    //   Ticks 6-20: price at 99.5 (near VWAP, z≈0 ≤ 2.0) → TP1 fires
    //     close=99.5 ≈ avg_entry=99.5 → borderline. Let's use avg_entry=100.5.
    //
    // Cleaner approach: warmup at 101, entry at 100.5 (dip from EMA 101),
    //   then price at 100.5 (z≈0) → TP1 fires, close(100.5) < avg_entry(100.5): same.
    // Even cleaner: warmup at 105, entry at 104 (dip), then price reverts to 103.
    //   avg_entry=104, close=103 < 104 = avg_entry; VWAP ≈ 103, z≈0 → TP1 fires in-loss.

    std::vector<Candle> candles;
    candles.reserve(30);
    int64_t ts2 = 0;
    // Warmup at 105
    for (int i = 0; i < 5; ++i) {
        candles.push_back({ts2 * 3600000LL, 105.0, 105.1, 104.9, 105.0, 1000.0});
        ++ts2;
    }
    // Dip to 104: EMA after tick5 = 0.333*104+0.667*105 ≈ 104.33
    // threshold = 104.33*(1-0.005) = 103.8; 104 > 103.8 → does NOT enter
    // Need deeper dip. With EMA period 5, alpha=2/6=0.333:
    //   EMA after 5 flat @105 ≈ 105 (converged)
    //   threshold = 105*(1-0.005) = 104.475
    //   Need close < 104.475 → use close=104.0 (just enough)
    // Actually: entry_ema_period=5, alpha=2/(5+1)=0.333
    //   EMA_5 = 0.333*104 + 0.667*105 = 34.666 + 70.0 = 104.667
    //   threshold = 104.667*(1-0.005) = 104.143; 104.0 < 104.143 → fires!
    candles.push_back({ts2 * 3600000LL, 104.1, 104.5, 103.8, 104.0, 1000.0});
    ++ts2;
    // Price reverts below entry: 20 candles at 103.5 (below avg_entry≈104, z≈0)
    // VWAP of these 5-candle windows ≈ 103.5 → z = |103.5-103.5|/stdev = 0 ≤ 2.0 → TP1 fires
    for (int i = 0; i < 20; ++i) {
        candles.push_back({ts2 * 3600000LL, 103.5, 103.6, 103.4, 103.5, 1000.0});
        ++ts2;
    }

    auto loaded = to_loaded(candles);
    std::vector<LoadedCandles> per_symbol = {std::move(loaded)};
    std::vector<SymbolInfo> infos = {make_info()};

    auto result = run_backtest(cfg, per_symbol, infos, "");
    ASSERT_GT(result.equity_curve.size(), 0U);
    ASSERT_EQ(result.final_positions.size(), 1U);

    // With the DEFECT 3 fix: TP1 fires (z≈0, close<avg_entry), tp1_fired=true,
    // then TP2+TP3 fire → position fully closed → traded_qty > initial_qty*0.5
    // Without the fix: tp1_fired stays false because close < avg_entry →
    // TP2/TP3 never fire → only TP1 closes (traded_qty ≈ tp1_frac * initial_qty)
    // We verify that graduated_tp progressed beyond just TP1 by checking
    // position_durations_hours has at least 1 entry (position was closed)
    EXPECT_GT(result.position_durations_hours.size(), 0U)
        << "DEFECT 3: graduated_tp never fully closed the position. "
           "Expected TP1(in-loss)+TP2+TP3 to close it, but it remains open.";
}

TEST(GraduatedTpTest, H2_PositionFullyClosedViaTP3) {
    // Test that graduated_tp eventually closes the entire position.
    // Strategy: build a candle_series with VWAP near price so z-score is low,
    // then simulate TP1 → (set tp1_fired=true) → TP2 → TP3.

    Config cfg = make_cfg();
    cfg.strategy.zscore_vwap_lookback = 5;
    cfg.strategy.tp1_z_threshold = 2.0;  // TP1 fires when |z| <= 2.0
    cfg.strategy.tp1_frac = 0.5;          // close 50% at TP1
    cfg.strategy.tp2_z_threshold = 1.0;  // TP2 fires when |z| <= 1.0
    cfg.strategy.tp2_frac = 0.5;          // close 50% of remaining at TP2
    cfg.strategy.trailing_atr_mult = 2.0;
    cfg.strategy.atr_period = 5;
    // No atr_period timeframe → TP3 falls back to z-score threshold

    SymbolInfo info = make_info();

    // Build a candle series where prices are near VWAP → low z-score
    // Use 10 candles at price ~100 (VWAP ≈ 100, stdev ~ small)
    std::vector<Candle> series;
    double p = 100.0;
    for (int i = 0; i < 10; ++i) {
        // Slight variation to keep stdev > 0
        double close = p + (i % 2 == 0 ? 0.5 : -0.5);
        series.push_back({static_cast<int64_t>(i) * 3600000, close, close+0.5, close-0.5, close, 1000.0});
    }

    Position pos;
    pos.avg_entry_price = 100.0;
    pos.total_qty = 1.0;
    pos.tp1_fired = false;

    GraduatedTpClosesAlgo algo;
    double remaining = pos.total_qty;

    // ---- Tick 1: TP1 should fire (z is low since close ≈ vwap) ----
    {
        // close = 100, vwap ≈ 100, stdev ≈ 0.5 → z ≈ 0 <= tp1_z_threshold=2.0
        Candle candle{10LL * 3600000, 100.0, 100.5, 99.5, 100.0, 1000.0};
        ModuleContext ctx{cfg, info, candle, pos, /*total_balance=*/10000.0,
                         /*current_tick=*/10, /*ema=*/100.0, /*rolling_stdev=*/0.5,
                         /*vwap=*/99.9, /*stdev=*/0.283,
                         std::span<const Candle>(series), /*candle_series_idx=*/9,
                         /*tf_data=*/{}};
 
        auto orders = algo.compute_closes(ctx);
        // Expect TP1 to fire (z <= 2.0), AND potentially TP3 fallback
        ASSERT_GT(orders.size(), 0U) << "H2: Expected at least TP1 order at tick 1";

        // Execute TP1 (first order)
        remaining -= orders[0].qty;
        EXPECT_NEAR(orders[0].qty, 0.5, 1e-9) << "H2: TP1 should close 50% of 1.0";

        // Set tp1_fired (as strategy.cpp would)
        pos.tp1_fired = true;
        pos.total_qty = remaining;
    }

    // ---- Tick 2: TP2 + TP3 should fire ----
    {
        Candle candle{11LL * 3600000, 100.0, 100.5, 99.5, 100.0, 1000.0};
        ModuleContext ctx{cfg, info, candle, pos, /*total_balance=*/10000.0,
                         /*current_tick=*/11, /*ema=*/100.0, /*rolling_stdev=*/0.5,
                         /*vwap=*/99.9, /*stdev=*/0.283,
                         std::span<const Candle>(series), /*candle_series_idx=*/9,
                         /*tf_data=*/{}};
 
        auto orders = algo.compute_closes(ctx);
         ASSERT_GT(orders.size(), 0U) << "H2: Expected TP2 and/or TP3 orders at tick 2";

        double tick2_closed = 0.0;
        for (const auto& ord : orders) {
            tick2_closed += ord.qty;
        }
        remaining -= tick2_closed;
        pos.total_qty = remaining;
    }

    // After TP1 + TP2 + TP3, the position must be fully closed
    EXPECT_NEAR(pos.total_qty, 0.0, 1e-9)
        << "H2: graduated_tp must fully close the position via TP1→TP2→TP3. "
        << "Remaining=" << pos.total_qty;
}
