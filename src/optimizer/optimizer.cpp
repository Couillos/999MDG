#include "optimizer.h"
#include "metrics/calculator.h"
#include "strategy/strategy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>
#include <zstd.h>

namespace martingale {
namespace {

bool is_int_param(const std::string& name) {
    return name == "entry_ema_period"
        || name == "close_grid_count"
        || name == "n_positions"
        || name == "parkinson_volatility_span";
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

void apply_params(Config& cfg, const ParamAxis& axis, double value) {
    auto const& name = axis.name;
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

double get_metric_value(const Metrics& m, const std::string& name) {
    if (name == "adg_usd") return m.adg_usd;
    if (name == "adg_per_exponential_fit_error_usd") return m.adg_per_exponential_fit_error_usd;
    if (name == "adg_per_exposure_long_usd") return m.adg_per_exposure_long_usd;
    if (name == "adg_per_exposure_short_usd") return m.adg_per_exposure_short_usd;
    if (name == "calmar_ratio_usd") return m.calmar_ratio_usd;
    if (name == "drawdown_worst") return m.drawdown_worst;
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

bool check_limits(const Metrics& m,
                  const std::map<std::string, Limit>& limits) {
    for (const auto& [name, lim] : limits) {
        double const val = get_metric_value(m, name);
        if (lim.has_min && val < lim.min) return false;
        if (lim.has_max && val > lim.max) return false;
    }
    return true;
}

/// Tracks per-metric min/max across observed results for min-max normalization.
struct MetricTracker {
    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::lowest();

    void observe(double val) {
        if (val < min_val) min_val = val;
        if (val > max_val) max_val = val;
    }

    double normalize(double val) const {
        double const range = max_val - min_val;
        if (range < 1e-15) return 0.5;
        return (val - min_val) / range;
    }
};

double compute_normalized_score(
    const Metrics& m,
    const std::vector<ScoringMetric>& scoring,
    const std::vector<MetricTracker>& trackers) {
    double s = 0.0;
    for (size_t i = 0; i < scoring.size(); ++i) {
        s += trackers[i].normalize(get_metric_value(m, scoring[i].metric)) * scoring[i].weight;
    }
    return s;
}

void write_result_json(FILE* f, const std::vector<ParamAxis>& axes,
                       const std::vector<size_t>& indices,
                       const Metrics& m, double score, bool valid) {
    std::fprintf(f, "{\"params\":{");
    bool first = true;
    for (size_t i = 0; i < axes.size(); ++i) {
        if (!first) std::fputc(',', f);
        first = false;
        std::fprintf(f, "\"%s\":%.10f", axes[i].name.c_str(), axes[i].values[indices[i]]);
    }
    std::fprintf(f, "},\"score\":%.10f,\"valid\":%s"
                    ",\"metrics\":{"
                    "\"adg_usd\":%.10f"
                    ",\"sharpe_ratio_usd\":%.10f"
                    ",\"sortino_ratio_usd\":%.10f"
                    ",\"calmar_ratio_usd\":%.10f"
                    ",\"drawdown_worst\":%.10f"
                    ",\"mdg_usd\":%.10f"
                    ",\"gain_usd\":%.10f"
                    ",\"loss_profit_ratio\":%.10f"
                    "}}\n",
                    score, valid ? "true" : "false",
                    m.adg_usd,
                    m.sharpe_ratio_usd,
                    m.sortino_ratio_usd,
                    m.calmar_ratio_usd,
                    m.drawdown_worst,
                    m.mdg_usd,
                    m.gain_usd,
                    m.loss_profit_ratio);
}

void compress_and_cleanup(const std::string& tmp_path,
                          const std::string& zst_path) {
    // Read entire tmp file
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

    // Wrap lines into JSON array "[ ... ]"
    std::string json = "[\n" + content + "]\n";

    // Compress with zstd
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

    std::printf("  Wrote %s (%zu results, zstd compressed)\n",
                zst_path.c_str(), content.size() > 0
                    ? static_cast<size_t>(std::count(content.begin(), content.end(), '\n'))
                    : 0);
}

void write_live_state(const std::string& path,
                       size_t completed, size_t total,
                       const std::vector<RunResult>& top_results,
                       const std::vector<ScoringMetric>& scoring,
                       const std::map<std::string, Limit>& limits)
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

    // top results
    std::fprintf(f, "\"top\":[");
    size_t const n = std::min(size_t{25}, top_results.size());
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) std::fputc(',', f);
        const auto& r = top_results[i];
        std::fprintf(f, "{\"score\":%.10f,\"valid\":%s,\"params\":{",
                     r.score, r.valid ? "true" : "false");
        bool fp = true;
        for (const auto& [k, v] : r.params) {
            if (!fp) std::fputc(',', f);
            fp = false;
            std::fprintf(f, "\"%s\":%.10f", k.c_str(), v);
        }
        // Write all scoring + limit metrics dynamically
        std::vector<std::string> all_metric_names;
        for (const auto& sm : scoring) all_metric_names.push_back(sm.metric);
        for (const auto& [name, _] : limits) all_metric_names.push_back(name);
        std::sort(all_metric_names.begin(), all_metric_names.end());
        all_metric_names.erase(
            std::unique(all_metric_names.begin(), all_metric_names.end()),
            all_metric_names.end());

        std::fprintf(f, "},\"metrics\":{");
        for (size_t mi = 0; mi < all_metric_names.size(); ++mi) {
            if (mi > 0) std::fputc(',', f);
            std::fprintf(f, "\"%s\":%.10f",
                         all_metric_names[mi].c_str(),
                         get_metric_value(r.metrics, all_metric_names[mi]));
        }
        std::fprintf(f, "}}");
    }
    std::fprintf(f, "]}\n");
    std::fclose(f);

    std::rename(tmp_path.c_str(), path.c_str());
}

} // anonymous namespace

std::vector<RunResult> run_optimization(
    const Config& cfg,
    const std::vector<LoadedCandles>& per_symbol_candles,
    const std::vector<SymbolInfo>& symbols_info,
    const std::string& results_path,
    OptimizerCallback callback,
    size_t top_n,
    const std::string& live_state_path)
{
    auto const axes = build_axes(cfg.optimize.bounds);

    // Compute total combos without creating them all
    size_t total = 1;
    for (const auto& ax : axes) total *= ax.values.size();

    std::printf("  Parameter combinations: %zu\n", total);
    if (total == 0) return {};

    // Cap to max_iterations if set
    size_t constexpr MAX_COMBOS = 100000;
    size_t const original_total = total;
    size_t const max_iter = cfg.optimize.max_iterations;
    if (max_iter > 0 && total > max_iter) {
        std::printf("  Capped to %zu iterations (max_iterations)\n", max_iter);
        total = max_iter;
    }

    // Check random sampling against original total before max_iter cap
    bool const random_sample = original_total > MAX_COMBOS;

    // Min-heap: smallest score at front (pop weakest when full)
    auto cmp = [](const RunResult& a, const RunResult& b) {
        return a.score > b.score;
    };
    std::vector<RunResult> top_results;
    top_results.reserve(top_n + 1);

    // Temp file for incremental JSON output
    std::string tmp_path;
    FILE* tmp_file = nullptr;
    if (!results_path.empty()) {
        tmp_path = results_path + ".tmp";
        tmp_file = std::fopen(tmp_path.c_str(), "w");
    }

    auto const& limits = cfg.optimize.limits;
    auto const& scoring = cfg.optimize.scoring;

    // Per-metric trackers for min-max normalized scoring
    std::vector<MetricTracker> trackers(scoring.size());

    // Setup combo iteration
    size_t const n_axes = axes.size();
    std::vector<size_t> sizes(n_axes);
    for (size_t i = 0; i < n_axes; ++i) sizes[i] = axes[i].values.size();

    if (random_sample) {
        std::printf("  Grid too large (%zu), random sampling %zu combos\n", original_total, total);
    }
    std::printf("  Running sequentially...\n");

    size_t live_counter = 0;

    // The combo processing function (called for each set of indices)
    auto process_combo = [&](const std::vector<size_t>& indices, size_t seq_idx) {
        Config local_cfg = cfg;
        for (size_t i = 0; i < n_axes; ++i) {
            apply_params(local_cfg, axes[i], axes[i].values[indices[i]]);
        }
        int const a = local_cfg.strategy.entry_ema_period;
        int const b = local_cfg.strategy.parkinson_volatility_span;
        local_cfg.warmup_candles = (a > b) ? a : b;

        auto const bt = run_backtest(local_cfg, per_symbol_candles, symbols_info, "");
        auto const metrics = compute_metrics(bt.equity_curve, local_cfg);

        for (size_t i = 0; i < scoring.size(); ++i) {
            trackers[i].observe(get_metric_value(metrics, scoring[i].metric));
        }
        double const score = compute_normalized_score(metrics, scoring, trackers);
        bool const valid = check_limits(metrics, limits);

        RunResult rr;
        for (size_t i = 0; i < n_axes; ++i) {
            rr.params[axes[i].name] = axes[i].values[indices[i]];
        }
        rr.metrics = metrics;
        rr.score = score;
        rr.valid = valid;

        if (top_results.size() < top_n) {
            top_results.push_back(rr);
            std::push_heap(top_results.begin(), top_results.end(), cmp);
        } else if (rr.score > top_results.front().score) {
            std::pop_heap(top_results.begin(), top_results.end(), cmp);
            top_results.back() = rr;
            std::push_heap(top_results.begin(), top_results.end(), cmp);
        }

        if (tmp_file) {
            write_result_json(tmp_file, axes, indices, metrics, score, valid);
        }

        if (callback) {
            callback(rr, seq_idx + 1, total);
        }

        ++live_counter;
        if (!live_state_path.empty() && (live_counter % 50 == 0 || live_counter == 1)) {
            // Sort a copy and write live state
            auto sorted_copy = top_results;
            std::sort(sorted_copy.begin(), sorted_copy.end(),
                [](const RunResult& a, const RunResult& b) { return a.score > b.score; });
            write_live_state(live_state_path, live_counter, total,
                             sorted_copy, scoring, limits);
        }
    };

    if (random_sample) {
        // Random sampling: pick `total` random linear indices
        std::mt19937_64 rng(std::random_device{}());
        std::vector<size_t> indices(n_axes);
        for (size_t s = 0; s < total; ++s) {
            size_t linear = std::uniform_int_distribution<size_t>{0, original_total - 1}(rng);
            // Convert linear index to cartesian (mixed-radix, last axis is fastest)
            for (int p = static_cast<int>(n_axes) - 1; p >= 0; --p) {
                indices[static_cast<size_t>(p)] = linear % sizes[static_cast<size_t>(p)];
                linear /= sizes[static_cast<size_t>(p)];
            }
            process_combo(indices, s);
        }
    } else {
        // Lazy grid iteration
        std::vector<size_t> indices(n_axes, 0);
        for (size_t idx = 0; idx < total; ++idx) {
            process_combo(indices, idx);
            // Advance indices (last axis increments fastest)
            size_t pos = n_axes;
            while (pos > 0) {
                --pos;
                ++indices[pos];
                if (indices[pos] < sizes[pos]) break;
                indices[pos] = 0;
            }
        }
    }

    // Final live state write (ensure done=true is captured)
    if (!live_state_path.empty()) {
        // Sort a copy for the final state
        auto sorted_copy = top_results;
        std::sort(sorted_copy.begin(), sorted_copy.end(),
            [](const RunResult& a, const RunResult& b) { return a.score > b.score; });
        write_live_state(live_state_path, total, total,
                         sorted_copy, scoring, limits);
    }

    if (tmp_file) {
        std::fclose(tmp_file);
    }

    // Compress temp file to zst
    if (!results_path.empty()) {
        compress_and_cleanup(tmp_path, results_path + ".zst");
    }

    // Sort top results descending by score
    std::sort(top_results.begin(), top_results.end(),
        [](const RunResult& a, const RunResult& b) {
            return a.score > b.score;
        });

    std::printf("  Top %zu results\n", top_results.size());
    return top_results;
}

} // namespace martingale
