#include "optimizer.h"
#include "metrics/calculator.h"
#include "strategy/strategy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace martingale {
namespace {

/// Checks if a parameter name represents an integer-valued parameter.
bool is_int_param(const std::string& name) {
    return name == "entry_ema_period"
        || name == "close_grid_count"
        || name == "n_positions"
        || name == "parkinson_volatility_span";
}

/// Generates candidate values for a single bound range (linear or stepped).
std::vector<double> axis_values(const std::string& name,
                                const std::array<double, 2>& bound) {
    double const lo = bound[0];
    double const hi = bound[1];
    if (std::abs(lo - hi) < 1e-15) {
        return {lo};
    }
    if (is_int_param(name)) {
        int const imin = static_cast<int>(std::ceil(lo));
        int const imax = static_cast<int>(std::floor(hi));
        if (imax < imin) return {static_cast<double>(imin)};
        std::vector<double> vals;
        vals.reserve(static_cast<size_t>(imax - imin + 1));
        for (int v = imin; v <= imax; ++v) {
            vals.push_back(static_cast<double>(v));
        }
        return vals;
    }
    // 5 linearly spaced double values
    std::vector<double> vals;
    vals.reserve(5);
    for (int i = 0; i < 5; ++i) {
        double const t = static_cast<double>(i) / 4.0;
        vals.push_back(lo + t * (hi - lo));
    }
    return vals;
}

/// One parameter axis with name and its candidate values.
struct ParamAxis {
    std::string name;
    std::vector<double> values;
};

/// Builds parameter axes from all optimization bounds.
std::vector<ParamAxis> build_axes(
    const std::map<std::string, std::array<double, 2>>& bounds) {
    std::vector<ParamAxis> axes;
    axes.reserve(bounds.size());
    for (const auto& [name, bound] : bounds) {
        axes.push_back({name, axis_values(name, bound)});
    }
    return axes;
}

/// Generates Cartesian product of all parameter axes.
std::vector<std::vector<std::pair<std::string, double>>>
generate_combinations(const std::vector<ParamAxis>& axes) {
    if (axes.empty()) {
        return {{}};
    }
    // Pre-compute total number of combinations
    size_t total = 1;
    for (const auto& ax : axes) {
        total *= ax.values.size();
    }
    std::vector<size_t> indices(axes.size(), 0);
    std::vector<std::vector<std::pair<std::string, double>>> combos;
    combos.reserve(total);
    for (size_t c = 0; c < total; ++c) {
        std::vector<std::pair<std::string, double>> combo;
        combo.reserve(axes.size());
        for (size_t i = 0; i < axes.size(); ++i) {
            combo.emplace_back(axes[i].name, axes[i].values[indices[i]]);
        }
        combos.push_back(std::move(combo));
        // Increment multi-base counter
        size_t pos = axes.size();
        while (pos > 0) {
            --pos;
            ++indices[pos];
            if (indices[pos] < axes[pos].values.size()) {
                break;
            }
            indices[pos] = 0;
        }
    }
    return combos;
}

/// Applies a parameter combination to a Config by mutating strategy fields.
/// Also recomputes warmup_candles from the updated entry_ema_period and parkinson_volatility_span.
void apply_params(Config& cfg,
                  const std::vector<std::pair<std::string, double>>& params) {
    for (const auto& [name, value] : params) {
        if (name == "entry_ema_period") {
            cfg.strategy.entry_ema_period = static_cast<int>(value);
        } else if (name == "entry_ema_distance_pct") {
            cfg.strategy.entry_ema_distance_pct = value;
        } else if (name == "entry_grid_spacing_pct") {
            cfg.strategy.entry_grid_spacing_pct = value;
        } else if (name == "initial_qty_pct") {
            cfg.strategy.initial_qty_pct = value;
        } else if (name == "double_down_factor") {
            cfg.strategy.double_down_factor = value;
        } else if (name == "close_grid_spacing_pct") {
            cfg.strategy.close_grid_spacing_pct = value;
        } else if (name == "close_grid_count") {
            cfg.strategy.close_grid_count = static_cast<int>(value);
        } else if (name == "sl_upnl_pct") {
            cfg.strategy.sl_upnl_pct = value;
        } else if (name == "n_positions") {
            cfg.strategy.n_positions = static_cast<int>(value);
        } else if (name == "parkinson_volatility_span") {
            cfg.strategy.parkinson_volatility_span = static_cast<int>(value);
        } else if (name == "maker_fee_pct") {
            cfg.strategy.maker_fee_pct = value;
        } else if (name == "total_wallet_exposure") {
            cfg.total_wallet_exposure = value;
        }
    }
    // Recompute warmup candles
    int const a = cfg.strategy.entry_ema_period;
    int const b = cfg.strategy.parkinson_volatility_span;
    cfg.warmup_candles = (a > b) ? a : b;
}

/// Looks up a metric value by its string name from the Metrics struct.
double get_metric_value(const Metrics& m, const std::string& name) {
    if (name == "adg_usd") return m.adg_usd;
    if (name == "adg_per_exponential_fit_error_usd") return m.adg_per_exponential_fit_error_usd;
    if (name == "adg_per_exposure_long_usd") return m.adg_per_exposure_long_usd;
    if (name == "adg_per_exposure_short_usd") return m.adg_per_exposure_short_usd;
    if (name == "calmar_ratio_usd") return m.calmar_ratio_usd;
    if (name == "entry_initial_balance_pct_long") return m.entry_initial_balance_pct_long;
    if (name == "entry_initial_balance_pct_short") return m.entry_initial_balance_pct_short;
    if (name == "equity_balance_diff_neg_max_usd") return m.equity_balance_diff_neg_max_usd;
    if (name == "equity_balance_diff_neg_mean_usd") return m.equity_balance_diff_neg_mean_usd;
    if (name == "equity_balance_diff_pos_max_usd") return m.equity_balance_diff_pos_max_usd;
    if (name == "equity_balance_diff_pos_mean_usd") return m.equity_balance_diff_pos_mean_usd;
    if (name == "equity_choppiness_usd") return m.equity_choppiness_usd;
    if (name == "equity_jerkiness_usd") return m.equity_jerkiness_usd;
    if (name == "expected_shortfall_1pct_usd") return m.expected_shortfall_1pct_usd;
    if (name == "exponential_fit_error_usd") return m.exponential_fit_error_usd;
    if (name == "gain_usd") return m.gain_usd;
    if (name == "gain_per_exposure_long_usd") return m.gain_per_exposure_long_usd;
    if (name == "gain_per_exposure_short_usd") return m.gain_per_exposure_short_usd;
    if (name == "loss_profit_ratio") return m.loss_profit_ratio;
    if (name == "loss_profit_ratio_long") return m.loss_profit_ratio_long;
    if (name == "loss_profit_ratio_short") return m.loss_profit_ratio_short;
    if (name == "mdg_usd") return m.mdg_usd;
    if (name == "mdg_per_exponential_fit_error_usd") return m.mdg_per_exponential_fit_error_usd;
    if (name == "mdg_per_exposure_long_usd") return m.mdg_per_exposure_long_usd;
    if (name == "mdg_per_exposure_short_usd") return m.mdg_per_exposure_short_usd;
    if (name == "omega_ratio_usd") return m.omega_ratio_usd;
    if (name == "peak_recovery_hours_equity_usd") return m.peak_recovery_hours_equity_usd;
    if (name == "position_held_hours_max") return m.position_held_hours_max;
    if (name == "position_held_hours_mean") return m.position_held_hours_mean;
    if (name == "position_held_hours_median") return m.position_held_hours_median;
    if (name == "position_unchanged_hours_max") return m.position_unchanged_hours_max;
    if (name == "positions_held_per_day") return m.positions_held_per_day;
    if (name == "sharpe_ratio_usd") return m.sharpe_ratio_usd;
    if (name == "sortino_ratio_usd") return m.sortino_ratio_usd;
    if (name == "sterling_ratio_usd") return m.sterling_ratio_usd;
    if (name == "volume_pct_per_day_avg") return m.volume_pct_per_day_avg;
    return 0.0;
}

/// Checks whether a Metrics satisfies all limit constraints.
bool check_limits(const Metrics& m,
                  const std::map<std::string, Limit>& limits) {
    for (const auto& [name, lim] : limits) {
        double const val = get_metric_value(m, name);
        if (lim.has_min && val < lim.min) return false;
        if (lim.has_max && val > lim.max) return false;
    }
    return true;
}

/// Computes the weighted score for a Metrics given the scoring configuration.
double compute_score(const Metrics& m,
                     const std::vector<ScoringMetric>& scoring) {
    double s = 0.0;
    for (const auto& sm : scoring) {
        s += get_metric_value(m, sm.metric) * sm.weight;
    }
    return s;
}

} // anonymous namespace

std::vector<RunResult> run_optimization(
    const Config& cfg,
    const std::vector<LoadedCandles>& per_symbol_candles,
    const std::vector<SymbolInfo>& symbols_info)
{
    auto const axes = build_axes(cfg.optimize.bounds);
    auto const combos = generate_combinations(axes);
    size_t const n_combos = combos.size();

    std::printf("  Parameter combinations: %zu\n", n_combos);

    std::vector<RunResult> results(n_combos);

    std::printf("  Running sequentially...\n");
    for (size_t idx = 0; idx < n_combos; ++idx) {
        if (idx % 50 == 0) {
            std::printf("    combo %zu/%zu\n", idx, n_combos);
            std::fflush(stdout);
        }

        Config local_cfg = cfg;
        apply_params(local_cfg, combos[idx]);

        auto const bt = run_backtest(local_cfg, per_symbol_candles, symbols_info, "");
        auto const metrics = compute_metrics(bt.equity_curve, local_cfg);

        bool const valid = check_limits(metrics, cfg.optimize.limits);
        double const score = valid
            ? compute_score(metrics, cfg.optimize.scoring)
            : 0.0;

        RunResult rr;
        for (const auto& [name, val] : combos[idx]) {
            rr.params[name] = val;
        }
        rr.metrics = metrics;
        rr.score = score;
        rr.valid = valid;
        results[idx] = rr;
    }

    // Sort by score descending
    std::sort(results.begin(), results.end(),
        [](const RunResult& a, const RunResult& b) {
            return a.score > b.score;
        });

    // Return top 100
    size_t const top = std::min(size_t{100}, results.size());
    std::vector<RunResult> top_results;
    top_results.reserve(top);
    for (size_t i = 0; i < top; ++i) {
        top_results.push_back(results[i]);
    }

    std::printf("  Top %zu results\n", top);
    return top_results;
}

} // namespace martingale
