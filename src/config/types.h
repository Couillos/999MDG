#ifndef MARTINGALE_CONFIG_TYPES_H
#define MARTINGALE_CONFIG_TYPES_H

#include <array>
#include <map>
#include <string>
#include <vector>

namespace martingale {

/// Execution mode for the engine.
enum class Mode {
    BACKTEST,
    OPTIMIZE
};

/// Fixed strategy parameters read from JSON strategy block.
struct StrategyParams {
    int entry_ema_period;
    double entry_ema_distance_pct;
    double entry_grid_spacing_pct;
    double initial_qty_pct;
    double double_down_factor;
    double close_grid_spacing_pct;
    int close_grid_count;
    double sl_upnl_pct;
    int n_positions;
    int parkinson_volatility_span;
    double maker_fee_pct;
};

/// Output redirection config.
struct OutputConfig {
    std::string dir;
};

/// Single limit constraint with optional min/max bounds.
struct Limit {
    bool has_min;
    double min;
    bool has_max;
    double max;
};

/// A metric name and its weight in scoring.
struct ScoringMetric {
    std::string metric;
    double weight;
};

/// Configuration for the optimizer (only used in OPTIMIZE mode).
struct OptimizeConfig {
    int n_workers;
    size_t max_iterations;
    std::map<std::string, Limit> limits;
    std::vector<ScoringMetric> scoring;
    std::map<std::string, std::array<double, 2>> bounds;
};

/// Top-level configuration deserialized from the JSON config file.
struct Config {
    Mode mode;
    std::vector<std::string> symbols;
    std::string timeframe;
    std::string date_from;
    std::string date_to;
    double initial_balance_usd;
    double total_wallet_exposure;
    StrategyParams strategy;
    OptimizeConfig optimize;
    OutputConfig output;
    int warmup_candles;
};

}  // namespace martingale

#endif  // MARTINGALE_CONFIG_TYPES_H
