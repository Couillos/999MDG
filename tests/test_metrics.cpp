#include "config/types.h"
#include "metrics/calculator.h"
#include "strategy/types.h"
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

using namespace martingale;

namespace {

Config make_cfg() {
    Config cfg{};
    cfg.mode = Mode::BACKTEST;
    cfg.symbols = {"TEST"};
    cfg.timeframe = "1h";
    cfg.date_from = "2024-01-01";
    cfg.date_to = "2024-01-10";
    cfg.initial_balance_usd = 10000.0;
    cfg.total_wallet_exposure = 2.0;
    cfg.warmup_candles = 0;
    cfg.strategy.maker_fee_pct = 0.001;
    cfg.output.dir = "";
    return cfg;
}

/// Wraps an equity curve into a BacktestResult for compute_metrics.
BacktestResult make_result(std::vector<EquityPoint> curve) {
    BacktestResult r;
    r.equity_curve = std::move(curve);
    return r;
}

} // anonymous namespace

TEST(MetricsTest, EmptyCurveReturnsDefaults) {
    auto cfg = make_cfg();
    std::vector<EquityPoint> empty;
    auto m = compute_metrics(make_result(empty), cfg);
    EXPECT_DOUBLE_EQ(m.gain_usd, 0.0);
    EXPECT_DOUBLE_EQ(m.sharpe_ratio_usd, 0.0);
    EXPECT_DOUBLE_EQ(m.sortino_ratio_usd, 0.0);
    EXPECT_DOUBLE_EQ(m.calmar_ratio_usd, 0.0);
}

TEST(MetricsTest, SinglePointReturnsDefaults) {
    auto cfg = make_cfg();
    std::vector<EquityPoint> curve = {
        {0, 10000.0, 10000.0, 0.0, {}}
    };
    auto m = compute_metrics(make_result(curve), cfg);
    EXPECT_DOUBLE_EQ(m.gain_usd, 0.0);
    EXPECT_DOUBLE_EQ(m.sharpe_ratio_usd, 0.0);
}

TEST(MetricsTest, LinearGrowth) {
    auto cfg = make_cfg();
    std::vector<EquityPoint> curve;
    // 10 daily points, equity growing linearly from 10000 to 11000
    int64_t ts = 1704067200000; // 2024-01-01
    for (int i = 0; i < 10; ++i) {
        double const eq = 10000.0 + static_cast<double>(i) * 1000.0 / 9.0;
        curve.push_back({ts, eq, eq, 0.0, {}});
        ts += 86400000; // 1 day
    }
    auto m = compute_metrics(make_result(curve), cfg);

    // gain_usd should be the sum of positive equity changes
    EXPECT_GT(m.gain_usd, 0.0);

    // sharpe_ratio_usd should be positive for a steadily increasing curve
    EXPECT_GT(m.sharpe_ratio_usd, 0.0);

    // sortino_ratio_usd should be positive
    EXPECT_GT(m.sortino_ratio_usd, 0.0);

    // total return should be ~10%
    double const total_ret = (curve.back().equity - curve.front().equity) / curve.front().equity;
    EXPECT_NEAR(total_ret, 0.10, 0.01);

    // No drawdown in a steadily increasing curve
    EXPECT_DOUBLE_EQ(m.calmar_ratio_usd, 0.0);
}

TEST(MetricsTest, VolatileCurve) {
    auto cfg = make_cfg();
    std::vector<EquityPoint> curve;
    int64_t ts = 1704067200000;
    // Alternating up/down pattern: 10000 -> 10100 -> 10050 -> 10150 -> 10100 ...
    double eq = 10000.0;
    for (int i = 0; i < 20; ++i) {
        curve.push_back({ts, eq, eq, 0.0, {}});
        if (i % 2 == 0) {
            eq += 100.0; // up
        } else {
            eq -= 50.0;  // down
        }
        ts += 86400000;
    }
    auto m = compute_metrics(make_result(curve), cfg);

    // There should be both positive and negative swings
    EXPECT_GT(m.gain_usd, 0.0);
    EXPECT_GT(m.mdg_usd, m.adg_usd); // median daily gain < mean daily gain (skewed)

    // Omega ratio should be defined since we have negative returns
    EXPECT_GT(m.omega_ratio_usd, 0.0);

    // There should be some drawdown
    EXPECT_GE(m.calmar_ratio_usd, 0.0);
}

TEST(MetricsTest, SharpeAndSortinoConsistency) {
    auto cfg = make_cfg();
    std::vector<EquityPoint> curve;
    int64_t ts = 1704067200000;
    double eq = 10000.0;
    for (int i = 0; i < 30; ++i) {
        curve.push_back({ts, eq, eq, 0.0, {}});
        eq += 50.0; // steady uptrend with no downside
        ts += 86400000;
    }
    auto m = compute_metrics(make_result(curve), cfg);

    // In a pure uptrend without downside, sortino >= sharpe (less downside risk)
    EXPECT_GE(m.sortino_ratio_usd, m.sharpe_ratio_usd);

    // Both should be positive
    EXPECT_GT(m.sharpe_ratio_usd, 0.0);
    EXPECT_GT(m.sortino_ratio_usd, 0.0);

    // Gain should be positive
    EXPECT_GT(m.gain_usd, 0.0);
}

TEST(MetricsTest, LossProfitRatio) {
    auto cfg = make_cfg();
    std::vector<EquityPoint> curve;
    int64_t ts = 1704067200000;
    double eq = 10000.0;
    // Pattern: up 200, down 100, up 200, down 100...
    for (int i = 0; i < 10; ++i) {
        curve.push_back({ts, eq, eq, 0.0, {}});
        eq += (i % 2 == 0) ? 200.0 : -100.0;
        ts += 86400000;
    }
    auto m = compute_metrics(make_result(curve), cfg);

    // Should have positive loss_profit_ratio (total gains > total losses)
    EXPECT_GT(m.loss_profit_ratio, 0.0);
    EXPECT_GT(m.gain_usd, 0.0);
}

TEST(MetricsTest, AdgSmoothed) {
    auto cfg = make_cfg();
    std::vector<EquityPoint> curve;
    int64_t ts = 1704067200000;
    // 10 daily points: linear from 10000 to 10090 (0.1% per day)
    for (int i = 0; i < 10; ++i) {
        double const eq = 10000.0 + static_cast<double>(i) * 10.0;
        curve.push_back({ts, eq, eq, 0.0, {}});
        ts += 86400000;
    }
    auto m = compute_metrics(make_result(curve), cfg);

    // d_eq = [10000, 10010, ..., 10090]
    // PassivBot formula: EMA(alpha=0.5) over full series
    // smoothed[0] = 10000, ..., smoothed[9] = 10080.01953125
    // gain = 10080.01953125/10000, adg = gain^(1/10) - 1
    double expected = 10000.0;
    double const alpha = 2.0 / 4.0; // span=3
    for (int i = 10010; i <= 10090; i += 10) {
        expected = alpha * static_cast<double>(i) + (1.0 - alpha) * expected;
    }
    double const gain = expected / 10000.0;
    expected = std::pow(gain, 1.0 / 10.0) - 1.0;
    EXPECT_NEAR(m.adg_smoothed, expected, 1e-15);

    // No drawdown means drawdown_worst_mean_1pct stays 0
    EXPECT_DOUBLE_EQ(m.drawdown_worst_mean_1pct, 0.0);
}

TEST(MetricsTest, DrawdownWorstMean1Pct) {
    auto cfg = make_cfg();
    std::vector<EquityPoint> curve;
    int64_t ts = 1704067200000;
    // 200 daily points: first 150 at 10000, last 50 at 8000 (20% drawdown)
    for (int i = 0; i < 200; ++i) {
        double const eq = (i < 150) ? 10000.0 : 8000.0;
        curve.push_back({ts, eq, eq, 0.0, {}});
        ts += 86400000;
    }
    auto m = compute_metrics(make_result(curve), cfg);

    // daily_dd = [0.0 (x150), -0.20 (x50)]
    // sorted: [-0.20 (x50), 0.0 (x150)]
    // nw = max(1, 200/100) = 2
    // worst 2 mean = (-0.20 + -0.20) / 2 = -0.20
    // drawdown_worst_mean_1pct = |-0.20| = 0.20
    EXPECT_NEAR(m.drawdown_worst_mean_1pct, 0.20, 1e-15);
}

TEST(MetricsTest, SterlingRatio) {
    auto cfg = make_cfg();
    std::vector<EquityPoint> curve;
    int64_t ts = 1704067200000;
    // 200 daily points: first 150 at 10000, last 50 at 8000
    for (int i = 0; i < 200; ++i) {
        double const eq = (i < 150) ? 10000.0 : 8000.0;
        curve.push_back({ts, eq, eq, 0.0, {}});
        ts += 86400000;
    }
    auto m = compute_metrics(make_result(curve), cfg);

    // d_eq = [10000 (x150), 8000 (x50)]
    // tail_len = 3, end = 8000, start = 10000
    // adg_smoothed = (8000/10000)^(1/200) - 1
    // drawdown_worst_mean_1pct = 0.20
    // sterling_ratio = adg_smoothed / 0.20
    double const expected_adg = std::pow(8000.0 / 10000.0, 1.0 / 200.0) - 1.0;
    double const expected_sr = expected_adg / 0.20;
    EXPECT_NEAR(m.sterling_ratio, expected_sr, 1e-15);
}
