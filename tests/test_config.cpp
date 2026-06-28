#include "config/loader.h"
#include "config/types.h"
#include <cstdio>
#include <cstring>
#include <gtest/gtest.h>

using namespace powermdg;

namespace {

/// Writes content to a temporary file and returns the path.
std::string write_temp(char const* content) {
    char path[] = "/tmp/powermdg_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd == -1) {
        std::fprintf(stderr, "mkstemp failed\n");
        std::exit(1);
    }
    FILE* f = fdopen(fd, "w");
    if (!f) {
        std::fprintf(stderr, "fdopen failed\n");
        std::exit(1);
    }
    std::fprintf(f, "%s", content);
    std::fclose(f);
    return std::string(path);
}

/// Removes a temporary file.
void remove_temp(std::string const& path) {
    std::remove(path.c_str());
}

char const* VALID_JSON = R"({
    "symbols": ["BTCUSDT", "ETHUSDT"],
    "timeframe": "1h",
    "date_from": "2024-01-01",
    "date_to": "2024-12-31",
    "initial_balance_usd": 10000.0,
    "total_wallet_exposure": 2.0,
    "strategy": {
        "entry_condition": {
            "ema_dist_pct": {
                "entry_ema_period": 200,
                "entry_ema_distance_pct": 0.02
            }
        },
        "entries_algo": {
            "martingale": {
                "entry_grid_spacing_pct": 0.03,
                "double_down_factor": 0.5
            }
        },
        "closes_algo": {
            "simple_grid": {
                "close_grid_spacing_pct": 0.02,
                "close_grid_count": 3
            }
        },
        "initial_qty_pct": 0.03,
        "sl_upnl_pct": -0.05,
        "n_positions": 2,
        "parkinson_volatility_span": 24,
        "maker_fee_pct": 0.001
    },
    "output": {
        "dir": "results"
    }
})";

}  // anonymous namespace

TEST(ConfigTest, ValidParsing) {
    auto path = write_temp(VALID_JSON);
    auto cfg = load_config(path, Mode::BACKTEST);

    EXPECT_EQ(cfg.mode, Mode::BACKTEST);
    ASSERT_EQ(cfg.symbols.size(), 2U);
    EXPECT_EQ(cfg.symbols[0], "BTCUSDT");
    EXPECT_EQ(cfg.symbols[1], "ETHUSDT");
    EXPECT_EQ(cfg.timeframe, "1h");
    EXPECT_EQ(cfg.date_from, "2024-01-01");
    EXPECT_EQ(cfg.date_to, "2024-12-31");
    EXPECT_DOUBLE_EQ(cfg.initial_balance_usd, 10000.0);
    EXPECT_DOUBLE_EQ(cfg.total_wallet_exposure, 2.0);

    auto const& s = cfg.strategy;
    // Module types
    EXPECT_EQ(s.entry_condition_type, "ema_dist_pct");
    EXPECT_EQ(s.entries_algo_type, "martingale");
    EXPECT_EQ(s.closes_algo_type, "simple_grid");
    // Flat params (parsed from nested modules)
    EXPECT_EQ(s.entry_ema_period, 200);
    EXPECT_DOUBLE_EQ(s.entry_ema_distance_pct, 0.02);
    EXPECT_DOUBLE_EQ(s.entry_grid_spacing_pct, 0.03);
    EXPECT_DOUBLE_EQ(s.initial_qty_pct, 0.03);
    EXPECT_DOUBLE_EQ(s.double_down_factor, 0.5);
    EXPECT_DOUBLE_EQ(s.close_grid_spacing_pct, 0.02);
    EXPECT_EQ(s.close_grid_count, 3);
    EXPECT_DOUBLE_EQ(s.sl_upnl_pct, -0.05);
    EXPECT_EQ(s.n_positions, 2);
    EXPECT_EQ(s.parkinson_volatility_span, 24);
    EXPECT_DOUBLE_EQ(s.maker_fee_pct, 0.001);

    EXPECT_EQ(cfg.output.dir, "results");
    EXPECT_EQ(cfg.warmup_candles, 200);

    remove_temp(path);
}

TEST(ConfigTest, InvalidJson) {
    auto path = write_temp("{ invalid json }");
    EXPECT_DEATH(load_config(path, Mode::BACKTEST), ".*");
    remove_temp(path);
}

TEST(ConfigTest, MissingField) {
    auto path = write_temp(R"({ "symbols": ["BTCUSDT"] })");
    EXPECT_DEATH(load_config(path, Mode::BACKTEST), ".*");
    remove_temp(path);
}

TEST(ConfigTest, WarmupCandles) {
    // parkinson_volatility_span = 48 > entry_ema_period = 20
    auto path = write_temp(R"({
        "symbols": ["BTCUSDT"],
        "timeframe": "1h",
        "date_from": "2024-01-01",
        "date_to": "2024-12-31",
        "initial_balance_usd": 1000.0,
        "total_wallet_exposure": 1.0,
        "strategy": {
            "entry_condition": {
                "ema_dist_pct": {
                    "entry_ema_period": 20,
                    "entry_ema_distance_pct": 0.01
                }
            },
            "entries_algo": {
                "martingale": {
                    "entry_grid_spacing_pct": 0.01,
                    "double_down_factor": 0.5
                }
            },
            "closes_algo": {
                "simple_grid": {
                    "close_grid_spacing_pct": 0.01,
                    "close_grid_count": 2
                }
            },
            "initial_qty_pct": 0.1,
            "sl_upnl_pct": -0.05,
            "n_positions": 1,
            "parkinson_volatility_span": 48,
            "maker_fee_pct": 0.001
        },
        "output": { "dir": "out" }
    })");
    auto cfg = load_config(path, Mode::BACKTEST);
    EXPECT_EQ(cfg.warmup_candles, 48);
    remove_temp(path);
}

TEST(ConfigTest, ModeBacktest) {
    auto path = write_temp(VALID_JSON);
    auto cfg = load_config(path, Mode::BACKTEST);
    EXPECT_EQ(cfg.mode, Mode::BACKTEST);
    remove_temp(path);
}

TEST(ConfigTest, ModeOptimize) {
    auto path = write_temp(VALID_JSON);
    auto cfg = load_config(path, Mode::OPTIMIZE);
    EXPECT_EQ(cfg.mode, Mode::OPTIMIZE);
    remove_temp(path);
}
