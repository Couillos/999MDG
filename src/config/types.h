#ifndef POWERMDG_CONFIG_TYPES_H
#define POWERMDG_CONFIG_TYPES_H

#include <array>
#include <map>
#include <string>
#include <vector>

namespace powermdg {

/// Execution mode for the engine.
enum class Mode {
    BACKTEST,
    OPTIMIZE
};

/// Fixed strategy parameters read from JSON strategy block.
///
/// The strategy is now modular: entry_condition, entries_algo, and closes_algo
/// are pluggable modules selected by name. The flat parameters below are shared
/// across all module implementations (each module reads only what it needs).
struct StrategyParams {
    // Module selection (determines which algorithm to use for each part)
    std::string entry_condition_type = "ema_dist_pct";
    std::string entries_algo_type = "martingale";
    std::string closes_algo_type = "simple_grid";

    // Flat parameters (used by various modules — see docs/ for which module
    // reads which parameter)
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
    double time_based_unstuck_pct;
    int time_based_unstuck_age;

    // --- bb_reversion entry_condition params ---
    double bb_std_mult;
    double bb_min_bandwidth_pct;

    // --- dca_linear entries_algo params ---
    double linear_step;

    // --- mean_revert_tp closes_algo params ---
    double revert_close_frac;
    double overshoot_pct;
    double tp_min_upnl_pct;

    // --- zscore_ou + graduated_tp + loss modules params ---
    double zscore_entry_threshold = 2.0;
    int zscore_vwap_lookback = 96;
    double tp1_z_threshold = 1.0;
    double tp1_frac = 0.5;
    double tp2_z_threshold = 0.3;
    double tp2_frac = 0.8;
    double trailing_atr_mult = 1.5;
    double z_stop_threshold = 3.5;
    int atr_period = 14;
    double atr_stop_mult = 2.0;
    double time_stop_hours = 4.0;
    double atr_filter_mult = 1.5;
    // loss_algo (multiple modules, OR logic)
    std::vector<std::string> loss_algo_types;

    // Map of param_name → timeframe for multi-timeframe indicators.
    // e.g. {"atr_period": "1h", "entry_ema_period": "15m"}
    // When a param uses ["1h", value] format in JSON, the timeframe is stored here.
    std::map<std::string, std::string> indicator_timeframes;
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

/// A metric name, its goal ("min" or "max"), and its importance weight.
/// engine_sign = -1.0 for "max", +1.0 for "min".
struct ScoringMetric {
    std::string metric;
    double weight = 1.0;
    std::string goal = "max";
    double engine_sign = -1.0; // -1 for max, +1 for min
};

/// Parameters for the NSGA-II genetic algorithm.
struct GAParams {
    int population_size = 100;
    int n_generations = 50;
    double crossover_prob = 0.8;
    double crossover_eta = 15.0;
    double mutation_prob = 0.2;
    double mutation_eta = 20.0;
    double mutation_indpb = 0.1;
};

/// Configuration for the optimizer (only used in OPTIMIZE mode).
struct OptimizeConfig {
    int n_workers;
    size_t max_iterations;
    std::map<std::string, Limit> limits;
    std::vector<ScoringMetric> scoring;
    std::map<std::string, std::array<double, 3>> bounds;
    GAParams ga;
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

}  // namespace powermdg

#endif  // POWERMDG_CONFIG_TYPES_H
