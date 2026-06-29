#include "optimizer.h"
#include "metrics/calculator.h"
#include "optimizer/nsga2.h"
#include "strategy/strategy.h"
#include "utils/thread_pool.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>
#include <zstd.h>

namespace powermdg {
namespace {

// ---------------------------------------------------------------------------
// Existing helpers
// ---------------------------------------------------------------------------

bool is_int_param(const std::string& name) {
    return name == "entry_ema_period"
        || name == "close_grid_count"
        || name == "n_positions"
        || name == "parkinson_volatility_span"
        || name == "time_based_unstuck_age";
}

std::vector<double> axis_values(const std::string& name,
                                const BoundSpec& bound) {
    double const lo = bound.lo;
    double const hi = bound.hi;
    double const step = bound.step;
    if (std::abs(lo - hi) < 1e-15) {
        return {lo};
    }
    if (step > 0.0) {
        std::vector<double> vals;
        if (is_int_param(name)) {
            int const istep = static_cast<int>(step);
            if (istep < 1) return {lo};
            int const imin = static_cast<int>(std::ceil(lo));
            int const imax_val = static_cast<int>(std::floor(hi));
            for (int v = imin; v <= imax_val; v += istep)
                vals.push_back(static_cast<double>(v));
        } else {
            for (double v = lo; v <= hi + 1e-12; v += step)
                vals.push_back(v);
        }
        return vals;
    }
    if (is_int_param(name)) {
        int const imin = static_cast<int>(std::ceil(lo));
        int const imax = static_cast<int>(std::floor(hi));
        if (imax < imin) return {static_cast<double>(imin)};
        int const n = imax - imin + 1;
        int constexpr max_vals = 10;
        std::vector<double> vals;
        if (n <= max_vals) {
            vals.reserve(static_cast<size_t>(n));
            for (int v = imin; v <= imax; ++v) vals.push_back(static_cast<double>(v));
        } else {
            int const int_step = static_cast<int>(std::ceil(static_cast<double>(n) / max_vals));
            vals.reserve(static_cast<size_t>(n / int_step + 1));
            for (int v = imin; v <= imax; v += int_step) vals.push_back(static_cast<double>(v));
        }
        return vals;
    }
    std::vector<double> vals;
    vals.reserve(5);
    for (int i = 0; i < 5; ++i) {
        vals.push_back(lo + (static_cast<double>(i) / 4.0) * (hi - lo));
    }
    return vals;
}

struct ParamAxis {
    std::string name;
    std::vector<double> values;
};

std::vector<ParamAxis> build_axes(
    const std::map<std::string, BoundSpec>& bounds) {
    std::vector<ParamAxis> axes;
    axes.reserve(bounds.size());
    for (const auto& [name, bound] : bounds) {
        axes.push_back({name, axis_values(name, bound)});
    }
    return axes;
}

} // close anonymous namespace — apply_param_to_cfg and compute_objectives_for_population
  // are defined below in the named powermdg:: namespace so tests can link to them.

// ---------------------------------------------------------------------------
// apply_param_to_cfg — handles ALL strategy parameters including time_based_*
//
// Defined in the named powermdg:: namespace (not anonymous) so that unit
// tests in other translation units can link to and call it directly.
//
// M4 note: loss-module-specific parameters (z_stop_threshold, atr_period,
// atr_stop_mult, time_stop_hours, atr_filter_mult) are applied unconditionally
// here even when the corresponding loss module is not active.  The guard
// "only optimise a param when its module is selected" lives in
// loader.cpp::is_valid_bound_param(), which currently accepts these params
// regardless of loss_algo_types (see loader.cpp:258).  A future fix in
// loader.cpp should reject bound axes whose module is absent so the GA does
// not waste a genome dimension.  The optimizer itself cannot enforce this
// without reading loader.cpp logic (out of scope for this agent).
// ---------------------------------------------------------------------------

void apply_param_to_cfg(Config& cfg, const std::string& name, double value) {
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
    } else if (name == "time_based_unstuck_pct") {
        cfg.strategy.time_based_unstuck_pct = value;
    } else if (name == "time_based_unstuck_age") {
        cfg.strategy.time_based_unstuck_age = static_cast<int>(value);
    } else if (name == "bb_std_mult") {
        cfg.strategy.bb_std_mult = value;
    } else if (name == "bb_min_bandwidth_pct") {
        cfg.strategy.bb_min_bandwidth_pct = value;
    } else if (name == "linear_step") {
        cfg.strategy.linear_step = value;
    } else if (name == "revert_close_frac") {
        cfg.strategy.revert_close_frac = value;
    } else if (name == "overshoot_pct") {
        cfg.strategy.overshoot_pct = value;
    } else if (name == "tp_min_upnl_pct") {
        cfg.strategy.tp_min_upnl_pct = value;
    } else if (name == "zscore_entry_threshold") {
        cfg.strategy.zscore_entry_threshold = value;
    } else if (name == "zscore_vwap_lookback") {
        cfg.strategy.zscore_vwap_lookback = static_cast<int>(value);
    } else if (name == "tp1_z_threshold") {
        cfg.strategy.tp1_z_threshold = value;
    } else if (name == "tp1_frac") {
        cfg.strategy.tp1_frac = value;
    } else if (name == "tp2_z_threshold") {
        cfg.strategy.tp2_z_threshold = value;
    } else if (name == "tp2_frac") {
        cfg.strategy.tp2_frac = value;
    } else if (name == "trailing_atr_mult") {
        cfg.strategy.trailing_atr_mult = value;
    } else if (name == "z_stop_threshold") {
        cfg.strategy.z_stop_threshold = value;
    } else if (name == "atr_period") {
        cfg.strategy.atr_period = static_cast<int>(value);
    } else if (name == "atr_stop_mult") {
        cfg.strategy.atr_stop_mult = value;
    } else if (name == "time_stop_hours") {
        cfg.strategy.time_stop_hours = value;
    } else if (name == "atr_filter_mult") {
        // C3 fix: only set atr_filter_mult. The two stray lines that also
        // overwrote tp_min_upnl_pct and time_based_unstuck_age were a merge
        // artifact and have been removed.
        cfg.strategy.atr_filter_mult = value;
    }
}

namespace {  // reopen anonymous namespace for internal-only helpers

// ---------------------------------------------------------------------------
// get_metric_value — includes all Metrics fields
// ---------------------------------------------------------------------------

double get_metric_value(const Metrics& m, const std::string& name) {
    if (name == "adg_smoothed") return m.adg_smoothed;
    if (name == "adg_usd") return m.adg_usd;
    if (name == "adg_per_exponential_fit_error_usd") return m.adg_per_exponential_fit_error_usd;
    if (name == "adg_per_exposure_long_usd") return m.adg_per_exposure_long_usd;
    if (name == "adg_per_exposure_short_usd") return m.adg_per_exposure_short_usd;
    if (name == "calmar_ratio_usd") return m.calmar_ratio_usd;
    if (name == "drawdown_worst") return m.drawdown_worst;
    if (name == "drawdown_worst_mean_1pct") return m.drawdown_worst_mean_1pct;
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
    if (name == "gain") return m.gain;
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
    if (name == "sterling_ratio") return m.sterling_ratio;
    if (name == "volume_pct_per_day_avg") return m.volume_pct_per_day_avg;
    return 0.0;
}

// ---------------------------------------------------------------------------
// Constraint penalty
// ---------------------------------------------------------------------------

double compute_total_penalty(const Metrics& m,
                             const std::map<std::string, Limit>& limits) {
    // Passivbot-style absolute penalty: 1e6 × (absolute breach) per constraint.
    // The previous (relative_diff)² penalty was far too small (a 10% breach gave
    // penalty = 0.01, drowned by objective values ~0.06). With 1e6 absolute,
    // any breach produces a penalty ≥ 1e6 which dominates all objectives and
    // forces the sort to treat the candidate as infeasible.
    constexpr double PENALTY_WEIGHT = 1e6;
    double penalty = 0.0;
    for (const auto& [name, lim] : limits) {
        double const val = get_metric_value(m, name);
        if (lim.has_min && val < lim.min) {
            penalty += PENALTY_WEIGHT * (lim.min - val);
        }
        if (lim.has_max && val > lim.max) {
            penalty += PENALTY_WEIGHT * (val - lim.max);
        }
    }
    return penalty;
}

// ---------------------------------------------------------------------------
// build_lo_hi — from axes
// ---------------------------------------------------------------------------

void build_lo_hi(const std::vector<ParamAxis>& axes,
                 std::vector<double>& lo, std::vector<double>& hi) {
    lo.resize(axes.size());
    hi.resize(axes.size());
    for (size_t i = 0; i < axes.size(); ++i) {
        lo[i] = axes[i].values.front();
        hi[i] = axes[i].values.back();
    }
}

// ---------------------------------------------------------------------------
// make_config_from_genes
// ---------------------------------------------------------------------------

Config make_config_from_genes(const Config& base,
                              const std::vector<ParamAxis>& axes,
                              const std::vector<double>& genes) {
    Config cfg = base;
    for (size_t i = 0; i < genes.size(); ++i) {
        apply_param_to_cfg(cfg, axes[i].name, genes[i]);
    }

    // M3: revalidate after gene application — clamp/guard degenerate values
    // that the loader would normally reject, preventing div-by-zero / NaN in
    // genomes where the GA chose boundary values the engine cannot handle.
    if (cfg.strategy.parkinson_volatility_span < 2) {
        cfg.strategy.parkinson_volatility_span = 2;
    }
    if (cfg.strategy.entry_grid_spacing_pct <= 0.0) {
        // Use base value as fallback; if base is also 0, force a tiny positive
        // step so martingale / dca_linear do not divide by zero.
        cfg.strategy.entry_grid_spacing_pct =
            (base.strategy.entry_grid_spacing_pct > 0.0)
                ? base.strategy.entry_grid_spacing_pct
                : 0.001;
    }

    // IMPORTANT: use base.warmup_candles (= max_warmup from bounds, computed
    // once in run_optimization before any gene evaluation) so that the
    // backtest's trading start matches the data loading's warmup.
    cfg.warmup_candles = base.warmup_candles;
    return cfg;
}

// ---------------------------------------------------------------------------
// evaluate_individual
// ---------------------------------------------------------------------------

Individual evaluate_individual(
    const Config& base_cfg,
    const std::vector<LoadedCandles>& per_symbol_candles,
    const std::vector<SymbolInfo>& symbols_info,
    const std::vector<ParamAxis>& axes,
    const std::map<std::string, Limit>& limits,
    const std::vector<double>& genes,
    int generation,
    const std::map<std::string, std::vector<LoadedCandles>>* mtf_candles = nullptr)
{
    Individual ind;
    ind.genes = genes;
    ind.generation = generation;
    ind.config = make_config_from_genes(base_cfg, axes, genes);
    auto const bt = run_backtest(ind.config, per_symbol_candles, symbols_info, "", mtf_candles);
    ind.metrics = compute_metrics(bt, ind.config);
    ind.constraint_violation = compute_total_penalty(ind.metrics, limits);
    return ind;
}

} // close anonymous namespace — compute_objectives_for_population defined below
  // in the named powermdg:: namespace so unit tests can link to it.

// ---------------------------------------------------------------------------
// compute_objectives_for_population — raw engine-space values (no z-score)
//
// Defined in the named powermdg:: namespace (not anonymous) so that unit
// tests in other translation units can link to and call it directly.
// ---------------------------------------------------------------------------

void compute_objectives_for_population(
    std::vector<Individual>& population,
    const std::vector<ScoringMetric>& scoring)
{
    size_t const n_obj = scoring.size();
    if (n_obj == 0 || population.empty()) return;

    // DEFECT B – NSGA-II per-axis scale invariance (honest comment):
    // NSGA-II Pareto dominance is scale-invariant per axis: multiplying objective j
    // by a constant weight does NOT change which individuals dominate which others,
    // because dominance only compares the SAME objective across individuals.
    // Crowding distance is re-normalised per axis by the sort, so weight also has
    // negligible effect there.  Weights therefore have ~no real effect on the
    // evolutionary selection process.
    //
    // Weights DO matter for the FINAL best-candidate selection (see the weighted-sum
    // scorer at the bottom of run_optimization()).  There, per-objective values are
    // normalised across the population and the weighted sum picks the single best
    // candidate from the feasible Pareto front, so a higher weight genuinely pulls
    // selection toward that metric.
    //
    // We keep the weight multiplication here only so that the stored objectives[]
    // values reflect the configured sign (engine_sign).  A weight of 0 would collapse
    // the axis — absent/zero weights default to 1.0 (guaranteed by the ScoringMetric
    // struct default).  For the NSGA-II evolution itself, weights are cosmetic;
    // for the final pick they are decisive.
    for (auto& ind : population) {
        ind.objectives.resize(n_obj);
        for (size_t j = 0; j < n_obj; ++j) {
            double const raw = get_metric_value(ind.metrics, scoring[j].metric);
            // Store sign-flipped raw value; weight applied in final selection only.
            ind.objectives[j] = raw * scoring[j].engine_sign;
        }
    }
}

namespace {  // reopen anonymous namespace for write_result_json and remaining helpers

// ---------------------------------------------------------------------------
// write_result_json — one individual as JSON line
// ---------------------------------------------------------------------------

void write_result_json(FILE* f, const Individual& ind,
                       const std::vector<ParamAxis>& axes)
{
    std::fprintf(f, "{\"generation\":%d,\"params\":{", ind.generation);
    bool first = true;
    for (size_t i = 0; i < axes.size(); ++i) {
        if (!first) std::fputc(',', f);
        first = false;
        std::fprintf(f, "\"%s\":%.10f", axes[i].name.c_str(), ind.genes[i]);
    }
    std::fprintf(f, "},\"objectives\":[");
    for (size_t i = 0; i < ind.objectives.size(); ++i) {
        if (i > 0) std::fputc(',', f);
        std::fprintf(f, "%.10f", ind.objectives[i]);
    }
    std::fprintf(f, "],\"constraint_violation\":%.10f", ind.constraint_violation);
    std::fprintf(f, ",\"metrics\":{");
    std::fprintf(f, "\"adg_usd\":%.10f", ind.metrics.adg_usd);
    std::fprintf(f, ",\"adg_smoothed\":%.10f", ind.metrics.adg_smoothed);
    std::fprintf(f, ",\"sharpe_ratio_usd\":%.10f", ind.metrics.sharpe_ratio_usd);
    std::fprintf(f, ",\"sortino_ratio_usd\":%.10f", ind.metrics.sortino_ratio_usd);
    std::fprintf(f, ",\"calmar_ratio_usd\":%.10f", ind.metrics.calmar_ratio_usd);
    std::fprintf(f, ",\"drawdown_worst\":%.10f", ind.metrics.drawdown_worst);
    std::fprintf(f, ",\"drawdown_worst_mean_1pct\":%.10f", ind.metrics.drawdown_worst_mean_1pct);
    std::fprintf(f, ",\"mdg_usd\":%.10f", ind.metrics.mdg_usd);
    std::fprintf(f, ",\"gain\":%.10f", ind.metrics.gain);
    std::fprintf(f, ",\"gain_usd\":%.10f", ind.metrics.gain_usd);
    std::fprintf(f, ",\"loss_profit_ratio\":%.10f", ind.metrics.loss_profit_ratio);
    std::fprintf(f, ",\"sterling_ratio\":%.10f", ind.metrics.sterling_ratio);
    std::fprintf(f, "}}\n");
}

// ---------------------------------------------------------------------------
// write_pareto_json — rank==1 individuals (constraint-aware)
// Only FEASIBLE candidates (cv <= eps) are written to the Pareto front.
// If no feasible candidate exists, falls back to writing rank==1 infeasible
// ones (so the user still sees something).
// ---------------------------------------------------------------------------

void write_pareto_json(const std::string& path,
                       const std::vector<Individual>& population,
                       const std::vector<ParamAxis>& axes)
{
    // Constraint-aware sort: feasible candidates dominate infeasible ones.
    std::vector<std::vector<double>> all_obj(population.size());
    std::vector<double> all_cv(population.size());
    for (size_t i = 0; i < population.size(); ++i) {
        all_obj[i] = population[i].objectives;
        all_cv[i] = population[i].constraint_violation;
    }
    auto ranks = fast_non_dominated_sort(all_obj, all_cv);

    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "Cannot write %s\n", path.c_str());
        return;
    }

    constexpr double CV_EPS = 1e-10;
    std::fprintf(f, "[\n");
    bool first_entry = true;
    size_t pareto_count = 0;
    // First pass: rank==1 AND feasible
    for (size_t i = 0; i < population.size(); ++i) {
        if (ranks[i] != 1) continue;
        if (population[i].constraint_violation > CV_EPS) continue;
        if (!first_entry) std::fprintf(f, ",\n");
        first_entry = false;

        const auto& ind = population[i];
        std::fprintf(f, "{\"params\":{");
        bool first = true;
        for (size_t j = 0; j < axes.size(); ++j) {
            if (!first) std::fputc(',', f);
            first = false;
            std::fprintf(f, "\"%s\":%.10f", axes[j].name.c_str(), ind.genes[j]);
        }
        std::fprintf(f, "},\"objectives\":[");
        for (size_t j = 0; j < ind.objectives.size(); ++j) {
            if (j > 0) std::fputc(',', f);
            std::fprintf(f, "%.10f", ind.objectives[j]);
        }
        std::fprintf(f, "],\"constraint_violation\":%.10f", ind.constraint_violation);
        std::fprintf(f, ",\"metrics\":{");
        std::fprintf(f, "\"adg_usd\":%.10f", ind.metrics.adg_usd);
        std::fprintf(f, ",\"adg_smoothed\":%.10f", ind.metrics.adg_smoothed);
        std::fprintf(f, ",\"sharpe_ratio_usd\":%.10f", ind.metrics.sharpe_ratio_usd);
        std::fprintf(f, ",\"sortino_ratio_usd\":%.10f", ind.metrics.sortino_ratio_usd);
        std::fprintf(f, ",\"calmar_ratio_usd\":%.10f", ind.metrics.calmar_ratio_usd);
        std::fprintf(f, ",\"drawdown_worst\":%.10f", ind.metrics.drawdown_worst);
        std::fprintf(f, ",\"drawdown_worst_mean_1pct\":%.10f", ind.metrics.drawdown_worst_mean_1pct);
        std::fprintf(f, ",\"mdg_usd\":%.10f", ind.metrics.mdg_usd);
        std::fprintf(f, ",\"sterling_ratio\":%.10f", ind.metrics.sterling_ratio);
        std::fprintf(f, "}}");
        ++pareto_count;
    }
    // Fallback pass: if no feasible rank==1 was written, write infeasible rank==1
    // so the user can see what's happening.
    if (pareto_count == 0) {
        for (size_t i = 0; i < population.size(); ++i) {
            if (ranks[i] != 1) continue;
            if (!first_entry) std::fprintf(f, ",\n");
            first_entry = false;

            const auto& ind = population[i];
            std::fprintf(f, "{\"params\":{");
            bool first = true;
            for (size_t j = 0; j < axes.size(); ++j) {
                if (!first) std::fputc(',', f);
                first = false;
                std::fprintf(f, "\"%s\":%.10f", axes[j].name.c_str(), ind.genes[j]);
            }
            std::fprintf(f, "},\"objectives\":[");
            for (size_t j = 0; j < ind.objectives.size(); ++j) {
                if (j > 0) std::fputc(',', f);
                std::fprintf(f, "%.10f", ind.objectives[j]);
            }
            std::fprintf(f, "],\"constraint_violation\":%.10f", ind.constraint_violation);
            std::fprintf(f, ",\"metrics\":{");
            std::fprintf(f, "\"adg_usd\":%.10f", ind.metrics.adg_usd);
            std::fprintf(f, ",\"adg_smoothed\":%.10f", ind.metrics.adg_smoothed);
            std::fprintf(f, ",\"sharpe_ratio_usd\":%.10f", ind.metrics.sharpe_ratio_usd);
            std::fprintf(f, ",\"sortino_ratio_usd\":%.10f", ind.metrics.sortino_ratio_usd);
            std::fprintf(f, ",\"calmar_ratio_usd\":%.10f", ind.metrics.calmar_ratio_usd);
            std::fprintf(f, ",\"drawdown_worst\":%.10f", ind.metrics.drawdown_worst);
            std::fprintf(f, ",\"drawdown_worst_mean_1pct\":%.10f", ind.metrics.drawdown_worst_mean_1pct);
            std::fprintf(f, ",\"mdg_usd\":%.10f", ind.metrics.mdg_usd);
            std::fprintf(f, ",\"sterling_ratio\":%.10f", ind.metrics.sterling_ratio);
            std::fprintf(f, "}}");
            ++pareto_count;
        }
    }
    std::fprintf(f, "\n]\n");
    std::fclose(f);
    std::printf("  Wrote %s (%zu pareto-optimal solutions)\n",
                path.c_str(), pareto_count);
}

// ---------------------------------------------------------------------------
// compress_and_cleanup
// ---------------------------------------------------------------------------

void compress_and_cleanup(const std::string& tmp_path,
                          const std::string& zst_path) {
    FILE* f = std::fopen(tmp_path.c_str(), "rb");
    if (!f) return;
    std::fseek(f, 0, SEEK_END);
    long const fsize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (fsize <= 0) {
        std::fclose(f);
        std::remove(tmp_path.c_str());
        return;
    }
    std::string content(static_cast<size_t>(fsize), '\0');
    if (std::fread(content.data(), 1, static_cast<size_t>(fsize), f) != static_cast<size_t>(fsize)) {
        std::fclose(f);
        std::remove(tmp_path.c_str());
        return;
    }
    std::fclose(f);
    std::remove(tmp_path.c_str());

    std::string json = "[\n" + content + "]\n";

    size_t const bound = ZSTD_compressBound(json.size());
    std::vector<char> compressed(bound);
    size_t const csize = ZSTD_compress(compressed.data(), bound,
                                       json.data(), json.size(), 3);
    if (ZSTD_isError(csize)) {
        std::fprintf(stderr, "ZSTD compress failed: %s\n",
                     ZSTD_getErrorName(csize));
        return;
    }

    f = std::fopen(zst_path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "Cannot write %s\n", zst_path.c_str());
        return;
    }
    std::fwrite(compressed.data(), 1, csize, f);
    std::fclose(f);

    std::printf("  Wrote %s (zstd compressed)\n", zst_path.c_str());
}

// ---------------------------------------------------------------------------
// write_live_state — GA generation-based, format compatible with TUI
// ---------------------------------------------------------------------------

void write_live_state(const std::string& path,
                      size_t completed, size_t total,
                      const std::vector<Individual>& sorted_population,
                      const std::vector<ScoringMetric>& scoring,
                      const std::map<std::string, Limit>& limits,
                      const std::vector<ParamAxis>& axes)
{
    std::string tmp_path = path + ".tmp";
    FILE* f = std::fopen(tmp_path.c_str(), "w");
    if (!f) return;

    std::fprintf(f, "{\"done\":%s,\"completed\":%zu,\"total\":%zu,",
                 completed >= total ? "true" : "false", completed, total);

    // scoring
    std::fprintf(f, "\"scoring\":[");
    bool first = true;
    for (const auto& sm : scoring) {
        if (!first) std::fputc(',', f);
        first = false;
        std::fprintf(f, "{\"metric\":\"%s\",\"weight\":%.10f}", sm.metric.c_str(), sm.weight);
    }
    std::fprintf(f, "],");

    // limits
    std::fprintf(f, "\"limits\":{");
    first = true;
    for (const auto& [name, lim] : limits) {
        if (!first) std::fputc(',', f);
        first = false;
        std::fprintf(f, "\"%s\":{", name.c_str());
        if (lim.has_min) std::fprintf(f, "\"min\":%.10f", lim.min);
        if (lim.has_min && lim.has_max) std::fputc(',', f);
        if (lim.has_max) std::fprintf(f, "\"max\":%.10f", lim.max);
        std::fprintf(f, "}");
    }
    std::fprintf(f, "},");

    // top results (top 25 FEASIBLE candidates by rank then crowding distance).
    // If fewer than 25 feasible candidates exist, fall back to top infeasible
    // ones (so the TUI always has something to display, with cv visible).
    std::fprintf(f, "\"top\":[");
    constexpr double CV_EPS_LS = 1e-10;
    size_t written = 0;
    size_t const max_top = 25;
    // First pass: feasible only
    for (size_t i = 0; i < sorted_population.size() && written < max_top; ++i) {
        if (sorted_population[i].constraint_violation > CV_EPS_LS) continue;
        if (written > 0) std::fputc(',', f);
        const auto& ind = sorted_population[i];
        std::fprintf(f, "{\"rank\":%d,\"constraint_violation\":%.10f,\"objectives\":[",
                     ind.rank, ind.constraint_violation);
        for (size_t oi = 0; oi < ind.objectives.size(); ++oi) {
            if (oi > 0) std::fputc(',', f);
            std::fprintf(f, "%.10f", ind.objectives[oi]);
        }
        std::fprintf(f, "],\"params\":{");
        bool fp = true;
        for (size_t j = 0; j < ind.genes.size(); ++j) {
            if (!fp) std::fputc(',', f);
            fp = false;
            std::fprintf(f, "\"%s\":%.10f", axes[j].name.c_str(), ind.genes[j]);
        }
        std::fprintf(f, "},\"metrics\":{");
        std::vector<std::string> all_metric_names;
        for (const auto& sm : scoring) all_metric_names.push_back(sm.metric);
        for (const auto& [nm, _] : limits) all_metric_names.push_back(nm);
        std::sort(all_metric_names.begin(), all_metric_names.end());
        all_metric_names.erase(
            std::unique(all_metric_names.begin(), all_metric_names.end()),
            all_metric_names.end());
        for (size_t mi = 0; mi < all_metric_names.size(); ++mi) {
            if (mi > 0) std::fputc(',', f);
            std::fprintf(f, "\"%s\":%.10f",
                         all_metric_names[mi].c_str(),
                         get_metric_value(ind.metrics, all_metric_names[mi]));
        }
        std::fprintf(f, "}}");
        ++written;
    }
    // Fallback pass: if no feasible was written, write top infeasible
    if (written == 0) {
        for (size_t i = 0; i < sorted_population.size() && written < max_top; ++i) {
            if (written > 0) std::fputc(',', f);
            const auto& ind = sorted_population[i];
            std::fprintf(f, "{\"rank\":%d,\"constraint_violation\":%.10f,\"objectives\":[",
                         ind.rank, ind.constraint_violation);
            for (size_t oi = 0; oi < ind.objectives.size(); ++oi) {
                if (oi > 0) std::fputc(',', f);
                std::fprintf(f, "%.10f", ind.objectives[oi]);
            }
            std::fprintf(f, "],\"params\":{");
            bool fp = true;
            for (size_t j = 0; j < ind.genes.size(); ++j) {
                if (!fp) std::fputc(',', f);
                fp = false;
                std::fprintf(f, "\"%s\":%.10f", axes[j].name.c_str(), ind.genes[j]);
            }
            std::fprintf(f, "},\"metrics\":{");
            std::vector<std::string> all_metric_names;
            for (const auto& sm : scoring) all_metric_names.push_back(sm.metric);
            for (const auto& [nm, _] : limits) all_metric_names.push_back(nm);
            std::sort(all_metric_names.begin(), all_metric_names.end());
            all_metric_names.erase(
                std::unique(all_metric_names.begin(), all_metric_names.end()),
                all_metric_names.end());
            for (size_t mi = 0; mi < all_metric_names.size(); ++mi) {
                if (mi > 0) std::fputc(',', f);
                std::fprintf(f, "\"%s\":%.10f",
                             all_metric_names[mi].c_str(),
                             get_metric_value(ind.metrics, all_metric_names[mi]));
            }
            std::fprintf(f, "}}");
            ++written;
        }
    }
    std::fprintf(f, "]}\n");
    std::fclose(f);

    std::rename(tmp_path.c_str(), path.c_str());
}

// ---------------------------------------------------------------------------
// Build RunResult from Individual (helper for callback)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Duplicate detection — hash genes vector and perturb on collision
// ---------------------------------------------------------------------------

size_t hash_genes(const std::vector<double>& genes) {
    size_t h = 0;
    for (double g : genes) {
        long long const rounded = static_cast<long long>(g * 1e9 + (g < 0 ? -0.5 : 0.5));
        h ^= static_cast<size_t>(rounded) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
}

void deduplicate_genes(std::vector<double>& genes,
                       std::unordered_set<size_t>& seen_hashes,
                       const std::vector<double>& lo,
                       const std::vector<double>& hi,
                       std::mt19937_64& rng) {
    size_t h = hash_genes(genes);
    if (seen_hashes.find(h) == seen_hashes.end()) {
        seen_hashes.insert(h);
        return;
    }

    std::normal_distribution<double> gauss{0.0, 0.01};
    std::uniform_real_distribution<double> unit{0.0, 1.0};

    for (int attempt = 0; attempt < 10; ++attempt) {
        for (size_t i = 0; i < genes.size(); ++i) {
            genes[i] += gauss(rng) * (hi[i] - lo[i]);
            if (genes[i] < lo[i]) genes[i] = lo[i];
            if (genes[i] > hi[i]) genes[i] = hi[i];
        }
        h = hash_genes(genes);
        if (seen_hashes.find(h) == seen_hashes.end()) {
            seen_hashes.insert(h);
            return;
        }
    }
    // Fallback: uniform random
    for (size_t i = 0; i < genes.size(); ++i) {
        genes[i] = lo[i] + unit(rng) * (hi[i] - lo[i]);
    }
    seen_hashes.insert(hash_genes(genes));
}

RunResult individual_to_result(const Individual& ind,
                               const std::vector<ParamAxis>& axes) {
    RunResult rr;
    for (size_t i = 0; i < axes.size(); ++i) {
        rr.params[axes[i].name] = ind.genes[i];
    }
    rr.metrics = ind.metrics;
    rr.objectives = ind.objectives;
    rr.constraint_violation = ind.constraint_violation;
    rr.rank = ind.rank;
    rr.crowding_distance = ind.crowding_distance;
    rr.generation = ind.generation;
    return rr;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// run_optimization — NSGA-II main loop
// ---------------------------------------------------------------------------

OptimizerResult run_optimization(
    const Config& cfg,
    const std::vector<LoadedCandles>& per_symbol_candles,
    const std::vector<SymbolInfo>& symbols_info,
    const std::map<std::string, std::vector<LoadedCandles>>& mtf_candles,
    const std::string& results_path,
    OptimizerCallback callback,
    const std::string& live_state_path)
{
    // 1. Parse bounds → axes → lo/hi
    auto const axes = build_axes(cfg.optimize.bounds);
    std::vector<double> lo, hi;
    build_lo_hi(axes, lo, hi);

    size_t const n_params = axes.size();
    if (n_params == 0) {
        std::fprintf(stderr, "No parameters to optimize.\n");
        return {};
    }

    auto const& limits = cfg.optimize.limits;
    auto const& scoring = cfg.optimize.scoring;
    auto const& ga = cfg.optimize.ga;

    int const pop_size = ga.population_size;
    int const n_gen = ga.n_generations;

    // M1: compute warmup sized for the GENOME, not just the base config.
    // The loader sets cfg.warmup_candles from the base config values; if the
    // GA explores entry_ema_period / parkinson_volatility_span /
    // zscore_vwap_lookback / atr_period near the top of their bounds, those
    // bounds may exceed the base warmup and leave the indicator with too little
    // history.  We scan every axis and take the maximum bound-hi for the
    // relevant parameters, then store the result in base_cfg which is passed
    // to all evaluation calls (make_config_from_genes copies
    // base.warmup_candles verbatim so every genome gets the same warmup).
    //
    // This does NOT edit loader.cpp (out of scope).  The final --backtest-best
    // run inherits the enlarged warmup because it reads the saved config from
    // the optimizer run.
    Config base_cfg = cfg;
    {
        static constexpr const char* WARMUP_PARAMS[] = {
            "entry_ema_period",
            "parkinson_volatility_span",
            "zscore_vwap_lookback",
            "atr_period",
        };
        int max_warmup = cfg.warmup_candles;
        for (const auto& axis : axes) {
            for (const char* p : WARMUP_PARAMS) {
                if (axis.name == p) {
                    int const bound_hi = static_cast<int>(axis.values.back());
                    if (bound_hi > max_warmup) max_warmup = bound_hi;
                }
            }
        }
        base_cfg.warmup_candles = max_warmup;
        std::printf("  warmup_candles: base=%d, bound-aware=%d\n",
                    cfg.warmup_candles, max_warmup);
    }

    std::printf("  NSGA-II: pop=%d, gen=%d, params=%zu, objectives=%zu\n",
                pop_size, n_gen, n_params, scoring.size());

    // 2. Random initial population
    std::mt19937_64 rng(std::random_device{}());
    auto gene_pop = random_population(static_cast<size_t>(pop_size), lo, hi, rng);
    // Deduplicate initial population
    std::unordered_set<size_t> seen_hashes;
    for (auto& g : gene_pop) {
        deduplicate_genes(g, seen_hashes, lo, hi, rng);
    }

    // 3. Temp file for incremental results
    std::string tmp_path;
    FILE* tmp_file = nullptr;
    if (!results_path.empty()) {
        tmp_path = results_path + ".tmp";
        tmp_file = std::fopen(tmp_path.c_str(), "w");
    }

    // 4. Thread pool for parallel evaluation
    ThreadPool pool(cfg.optimize.n_workers > 0 ? cfg.optimize.n_workers : 1);

    // 5. Evaluate initial population in parallel
    std::vector<Individual> population(static_cast<size_t>(pop_size));
    {
        std::atomic<size_t> eval_idx{0};
        int const nw = pool.worker_count();
        for (int w = 0; w < nw; ++w) {
            pool.submit([&]() {
                while (true) {
                    size_t const idx = eval_idx.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= population.size()) break;
                    population[idx] = evaluate_individual(
                        base_cfg, per_symbol_candles, symbols_info, axes, limits,
                        gene_pop[idx], 0, &mtf_candles);
                }
            });
        }
        pool.wait();
    }

    // 6. Compute objectives + environmental selection for initial pop
    compute_objectives_for_population(population, scoring);
    population = select_next_generation(population, static_cast<size_t>(pop_size));
    compute_objectives_for_population(population, scoring);

    // Write initial population to temp file
    if (tmp_file) {
        for (const auto& ind : population) {
            write_result_json(tmp_file, ind, axes);
        }
    }

    // 7. Generations loop
    for (int gen = 1; gen <= n_gen; ++gen) {
        size_t const off_size = static_cast<size_t>(pop_size);
        std::vector<Individual> offspring(off_size);

        // Create offspring (sequential, uses main RNG)
        for (size_t i = 0; i + 1 < off_size; i += 2) {
            size_t p1_idx = tournament_select(population, 2, rng);
            size_t p2_idx = tournament_select(population, 2, rng);

            std::vector<double> c1 = population[p1_idx].genes;
            std::vector<double> c2 = population[p2_idx].genes;

            sbx_crossover(c1, c2, lo, hi, ga.crossover_prob, ga.crossover_eta, rng);
            polynomial_mutation(c1, lo, hi, ga.mutation_prob, ga.mutation_indpb, ga.mutation_eta, rng);
            polynomial_mutation(c2, lo, hi, ga.mutation_prob, ga.mutation_indpb, ga.mutation_eta, rng);

            deduplicate_genes(c1, seen_hashes, lo, hi, rng);
            deduplicate_genes(c2, seen_hashes, lo, hi, rng);
            offspring[i].genes = std::move(c1);
            offspring[i].generation = gen;
            offspring[i + 1].genes = std::move(c2);
            offspring[i + 1].generation = gen;
        }

        // Evaluate offspring in parallel
        {
            std::atomic<size_t> eval_idx{0};
            int const nw = pool.worker_count();
            for (int w = 0; w < nw; ++w) {
                pool.submit([&]() {
                    while (true) {
                        size_t const idx = eval_idx.fetch_add(1, std::memory_order_relaxed);
                        if (idx >= off_size) break;
                        offspring[idx] = evaluate_individual(
                            base_cfg, per_symbol_candles, symbols_info, axes, limits,
                            offspring[idx].genes, gen, &mtf_candles);
                    }
                });
            }
            pool.wait();
        }

        // Combined = parents + offspring
        std::vector<Individual> combined;
        combined.reserve(population.size() + offspring.size());
        combined.insert(combined.end(), population.begin(), population.end());
        combined.insert(combined.end(), offspring.begin(), offspring.end());

        // Compute objectives + environmental selection
        compute_objectives_for_population(combined, scoring);
        population = select_next_generation(combined, static_cast<size_t>(pop_size));
        compute_objectives_for_population(population, scoring);

        // Write offspring results to temp file
        if (tmp_file) {
            for (const auto& ind : offspring) {
                write_result_json(tmp_file, ind, axes);
            }
        }

        // Find best of generation (by rank then crowding distance)
        auto best_it = std::max_element(population.begin(), population.end(),
            [](const Individual& a, const Individual& b) {
                if (a.rank != b.rank) return a.rank > b.rank;
                return a.crowding_distance < b.crowding_distance;
            });

        // Callback with best of generation
        if (callback && best_it != population.end()) {
            RunResult const rr = individual_to_result(*best_it, axes);
            callback(rr, static_cast<size_t>(gen), static_cast<size_t>(n_gen));
        }

        // Live state
        if (!live_state_path.empty()) {
            auto sorted = population;
            std::sort(sorted.begin(), sorted.end(),
                [](const Individual& a, const Individual& b) {
                    if (a.rank != b.rank) return a.rank < b.rank;
                    return a.crowding_distance > b.crowding_distance;
                });
            write_live_state(live_state_path, static_cast<size_t>(gen),
                             static_cast<size_t>(n_gen),
                             sorted, scoring, limits, axes);
        }
    }

    // 8. Close temp file
    if (tmp_file) {
        std::fclose(tmp_file);
    }

    // 9. Compress results
    if (!results_path.empty()) {
        compress_and_cleanup(tmp_path, results_path + ".zst");
    }

    // 10. Build OptimizerResult
    OptimizerResult result;

    // Constraint-aware final sort: feasible candidates dominate infeasible ones.
    std::vector<std::vector<double>> final_obj(population.size());
    std::vector<double> final_cv(population.size());
    for (size_t i = 0; i < population.size(); ++i) {
        final_obj[i] = population[i].objectives;
        final_cv[i] = population[i].constraint_violation;
    }
    auto final_ranks = fast_non_dominated_sort(final_obj, final_cv);

    constexpr double CV_EPS = 1e-10;
    for (size_t i = 0; i < population.size(); ++i) {
        RunResult const rr = individual_to_result(population[i], axes);
        result.all_results.push_back(rr);
        // Pareto front = rank==1 AND feasible (cv <= eps).
        // If we included infeasible rank==1, the user would see candidates
        // that violate the constraints in the "best" list.
        if (final_ranks[i] == 1 && population[i].constraint_violation <= CV_EPS) {
            result.pareto_front.push_back(rr);
        }
    }

    // DEFECT B fix: weighted-sum best-candidate selection.
    //
    // NSGA-II Pareto dominance is scale-invariant per objective axis, so weight
    // multiplied into objectives[] during evolution has ~no real effect on which
    // individuals survive (see compute_objectives_for_population comment).
    // To make weights GENUINELY influence which single candidate is declared
    // "best" (#1 in all_results / used by --backtest-best), we compute a
    // WEIGHTED-SUM SCALAR over the feasible Pareto-optimal set:
    //
    //   weighted_score(ind) = sum_j [ weight_j * norm_j(ind) ]
    //
    // where norm_j(ind) = (obj_j(ind) - min_j) / (max_j - min_j + eps) and
    // obj_j = raw_metric * engine_sign  (engine-space: lower is worse).
    //
    // Because engine_sign = -1 for "max" goals, a HIGHER obj_j means a WORSE
    // outcome.  We want the BEST candidate to have the LOWEST weighted-sum, so
    // we normalise as "closeness to min" (lower obj = better = higher normalised
    // score):   norm_j = (max_j - obj_j) / (max_j - min_j + eps)
    //
    // A feasible rank-1 candidate with the highest weighted_score is the one that
    // best satisfies the user's weighting across metrics.  This is computed once
    // here on the final population; the evolutionary loop is NOT touched.
    {
        size_t const n_obj = scoring.size();
        // Compute per-axis min/max across ALL feasible population members.
        // Using the full population (not just rank-1) gives a stable normalisation
        // range even when the Pareto front is small.
        std::vector<double> obj_min(n_obj,  1e300);
        std::vector<double> obj_max(n_obj, -1e300);
        for (const auto& ind : population) {
            if (ind.constraint_violation > CV_EPS) continue;
            if (ind.objectives.size() < n_obj) continue;
            for (size_t j = 0; j < n_obj; ++j) {
                if (ind.objectives[j] < obj_min[j]) obj_min[j] = ind.objectives[j];
                if (ind.objectives[j] > obj_max[j]) obj_max[j] = ind.objectives[j];
            }
        }

        // Lambda: compute weighted score for a RunResult.
        // Higher weighted_score = better candidate.
        auto weighted_score = [&](const RunResult& rr) -> double {
            if (rr.objectives.size() < n_obj) return -1e300;
            double score = 0.0;
            for (size_t j = 0; j < n_obj; ++j) {
                double const range = obj_max[j] - obj_min[j];
                double const eps   = 1e-12;
                // norm ∈ [0,1]; 1 = best (closest to min), 0 = worst (= max)
                double const norm  = (range > eps) ? (obj_max[j] - rr.objectives[j]) / (range + eps) : 1.0;
                double const w     = (scoring[j].weight > 0.0) ? scoring[j].weight : 1.0;
                score += w * norm;
            }
            return score;
        };

        // Sort all_results: feasible rank-1 by weighted_score DESC first,
        // then remainder by rank ASC / crowding DESC.
        std::stable_sort(result.all_results.begin(), result.all_results.end(),
            [&](const RunResult& a, const RunResult& b) {
                bool const a_best = (a.rank == 1 && a.constraint_violation <= CV_EPS);
                bool const b_best = (b.rank == 1 && b.constraint_violation <= CV_EPS);
                if (a_best != b_best) return a_best > b_best; // feasible rank-1 first
                if (a_best && b_best) {
                    // Both on feasible Pareto front: rank by weighted sum
                    return weighted_score(a) > weighted_score(b);
                }
                // Outside Pareto front: rank by Pareto rank then crowding distance
                if (a.rank != b.rank) return a.rank < b.rank;
                return a.crowding_distance > b.crowding_distance;
            });

        // Sort pareto_front by weighted_score DESC so results[0] == pareto_front[0].
        std::stable_sort(result.pareto_front.begin(), result.pareto_front.end(),
            [&](const RunResult& a, const RunResult& b) {
                return weighted_score(a) > weighted_score(b);
            });
    }

    // 11. Write Pareto JSON
    if (!results_path.empty()) {
        write_pareto_json(results_path + "_pareto.json", population, axes);
    }

    std::printf("  Pareto front: %zu solutions, total evaluated: %zu\n",
                result.pareto_front.size(),
                result.all_results.size());

    return result;
}

} // namespace powermdg
