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

namespace martingale {
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
                                const std::array<double, 3>& bound) {
    double const lo = bound[0];
    double const hi = bound[1];
    double const step = bound[2];
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
            int const step = static_cast<int>(std::ceil(static_cast<double>(n) / max_vals));
            vals.reserve(static_cast<size_t>(n / step + 1));
            for (int v = imin; v <= imax; v += step) vals.push_back(static_cast<double>(v));
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
    const std::map<std::string, std::array<double, 3>>& bounds) {
    std::vector<ParamAxis> axes;
    axes.reserve(bounds.size());
    for (const auto& [name, bound] : bounds) {
        axes.push_back({name, axis_values(name, bound)});
    }
    return axes;
}

// ---------------------------------------------------------------------------
// apply_param_to_cfg — handles ALL strategy parameters including time_based_*
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
    } else if (name == "time_based_unstuck_threshold") {
        cfg.strategy.time_based_unstuck_threshold = value;
    } else if (name == "time_based_unstuck_age") {
        cfg.strategy.time_based_unstuck_age = static_cast<int>(value);
    }
}

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
    double penalty = 0.0;
    for (const auto& [name, lim] : limits) {
        double const val = get_metric_value(m, name);
        if (lim.has_min && val < lim.min) {
            double const diff = (lim.min - val) / std::max(std::abs(lim.min), 1e-10);
            penalty += diff * diff;
        }
        if (lim.has_max && val > lim.max) {
            double const diff = (val - lim.max) / std::max(std::abs(lim.max), 1e-10);
            penalty += diff * diff;
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
    int const a = cfg.strategy.entry_ema_period;
    int const b = cfg.strategy.parkinson_volatility_span;
    cfg.warmup_candles = (a > b) ? a : b;
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
    int generation)
{
    Individual ind;
    ind.genes = genes;
    ind.generation = generation;
    ind.config = make_config_from_genes(base_cfg, axes, genes);
    auto const bt = run_backtest(ind.config, per_symbol_candles, symbols_info, "");
    ind.metrics = compute_metrics(bt.equity_curve, ind.config);
    ind.constraint_violation = compute_total_penalty(ind.metrics, limits);
    return ind;
}

// ---------------------------------------------------------------------------
// compute_objectives_for_population — raw engine-space values (no z-score)
// ---------------------------------------------------------------------------

void compute_objectives_for_population(
    std::vector<Individual>& population,
    const std::vector<ScoringMetric>& scoring)
{
    size_t const n_obj = scoring.size();
    if (n_obj == 0 || population.empty()) return;

    for (auto& ind : population) {
        ind.objectives.resize(n_obj);
        for (size_t j = 0; j < n_obj; ++j) {
            double const raw = get_metric_value(ind.metrics, scoring[j].metric);
            ind.objectives[j] = raw * scoring[j].engine_sign;
        }
    }
}

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
    std::fprintf(f, ",\"gain_usd\":%.10f", ind.metrics.gain_usd);
    std::fprintf(f, ",\"loss_profit_ratio\":%.10f", ind.metrics.loss_profit_ratio);
    std::fprintf(f, ",\"sterling_ratio\":%.10f", ind.metrics.sterling_ratio);
    std::fprintf(f, "}}\n");
}

// ---------------------------------------------------------------------------
// write_pareto_json — rank==1 individuals
// ---------------------------------------------------------------------------

void write_pareto_json(const std::string& path,
                       const std::vector<Individual>& population,
                       const std::vector<ParamAxis>& axes)
{
    // Compute ranks
    std::vector<std::vector<double>> all_obj(population.size());
    for (size_t i = 0; i < population.size(); ++i) {
        all_obj[i] = population[i].objectives;
    }
    auto ranks = fast_non_dominated_sort(all_obj);

    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "Cannot write %s\n", path.c_str());
        return;
    }

    std::fprintf(f, "[\n");
    bool first_entry = true;
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
    }
    std::fprintf(f, "\n]\n");
    std::fclose(f);
    // Count how many we wrote
    size_t pareto_count = 0;
    for (size_t i = 0; i < population.size(); ++i) {
        if (ranks[i] == 1) ++pareto_count;
    }
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

    // top results (top 25 by rank then crowding distance)
    std::fprintf(f, "\"top\":[");
    size_t const n = std::min(size_t{25}, sorted_population.size());
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) std::fputc(',', f);
        const auto& ind = sorted_population[i];

        std::fprintf(f, "{\"constraint_violation\":%.10f,\"params\":{",
                     ind.constraint_violation);
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
                        cfg, per_symbol_candles, symbols_info, axes, limits,
                        gene_pop[idx], 0);
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
                            cfg, per_symbol_candles, symbols_info, axes, limits,
                            offspring[idx].genes, gen);
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

    std::vector<std::vector<double>> final_obj(population.size());
    for (size_t i = 0; i < population.size(); ++i) {
        final_obj[i] = population[i].objectives;
    }
    auto final_ranks = fast_non_dominated_sort(final_obj);

    for (size_t i = 0; i < population.size(); ++i) {
        RunResult const rr = individual_to_result(population[i], axes);
        result.all_results.push_back(rr);
        if (final_ranks[i] == 1) {
            result.pareto_front.push_back(rr);
        }
    }

    // Sort all_results by constraint_violation ascending (lowest penalty first)
    // then by average objective (lower is better in engine-space)
    std::sort(result.all_results.begin(), result.all_results.end(),
        [](const RunResult& a, const RunResult& b) {
            if (a.constraint_violation != b.constraint_violation)
                return a.constraint_violation < b.constraint_violation;
            double a_sum = 0.0, b_sum = 0.0;
            for (size_t i = 0; i < std::min(a.objectives.size(), b.objectives.size()); ++i) {
                a_sum += a.objectives[i];
                b_sum += b.objectives[i];
            }
            return a_sum < b_sum;
        });

    // Sort pareto front by constraint_violation then average objective too
    std::sort(result.pareto_front.begin(), result.pareto_front.end(),
        [](const RunResult& a, const RunResult& b) {
            if (a.constraint_violation != b.constraint_violation)
                return a.constraint_violation < b.constraint_violation;
            double a_sum = 0.0, b_sum = 0.0;
            for (size_t i = 0; i < std::min(a.objectives.size(), b.objectives.size()); ++i) {
                a_sum += a.objectives[i];
                b_sum += b.objectives[i];
            }
            return a_sum < b_sum;
        });

    // 11. Write Pareto JSON
    if (!results_path.empty()) {
        write_pareto_json(results_path + "_pareto.json", population, axes);
    }

    std::printf("  Pareto front: %zu solutions, total evaluated: %zu\n",
                result.pareto_front.size(),
                result.all_results.size());

    return result;
}

} // namespace martingale
