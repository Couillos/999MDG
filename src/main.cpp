#include "config/loader.h"
#include "config/types.h"
#include "data/candle_manager.h"
#include "data/symbol_info.h"
#include "metrics/calculator.h"
#include "optimizer/optimizer.h"
#include "plot/plotter.h"
#include "strategy/strategy.h"
#include "ui/tui.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <simdjson.h>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace powermdg;

// ============================================================================
// Constants
// ============================================================================

/// Optimization results always go here (separate from backtest results).
static constexpr char const* OPT_DIR = "optimization_results";

/// Backtest results always go here (separate from optimization results).
static constexpr char const* BT_DIR = "backtests";

// ============================================================================
// Helpers
// ============================================================================

[[noreturn]] static void usage(char const* prog) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s backtest <config.json>\n"
        "  %s optimize <config.json> [--backtest-best]\n"
        "  %s --tui                     Watch live optimization progress (ncurses)\n"
        "  %s --backtest-best           Backtest the #1 candidate from the most recent optimization\n"
        "\n"
        "Modes:\n"
        "  backtest <config>            Run a single backtest, output to backtests/\n"
        "  optimize <config>            Run NSGA-II optimization (progress to stdout),\n"
        "                               output to optimization_results/\n"
        "  optimize <config> --backtest-best\n"
        "                               Same as optimize, then backtest the #1 candidate\n"
        "  --tui                        Watch live state of a running optimization\n"
        "                               (run in parallel with 'optimize' in another terminal)\n"
        "  --backtest-best              Backtest the best candidate from the most\n"
        "                               recent optimization (no config needed)\n",
        prog, prog, prog, prog);
    std::exit(1);
}

static char const* mode_str(Mode m) {
    if (m == Mode::BACKTEST) return "backtest";
    return "optimize";
}

static std::string create_results_dir(const std::string& base) {
    auto const now = std::chrono::system_clock::now();
    std::time_t const t = std::chrono::system_clock::to_time_t(now);
    struct tm utc;
    gmtime_r(&t, &utc);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s/%04d-%02d-%02d_%02d-%02d-%02d",
                  base.c_str(),
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec);

    std::error_code ec;
    std::filesystem::create_directories(buf, ec);
    return std::string(buf);
}

static void write_analysis_json(const std::string& path, const Metrics& m,
                                const Config& cfg,
                                const std::vector<EquityPoint>& curve) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "Cannot write %s\n", path.c_str());
        return;
    }

    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"backtest\": {\n");
    std::fprintf(f, "    \"date_from\": \"%s\",\n", cfg.date_from.c_str());
    std::fprintf(f, "    \"date_to\": \"%s\",\n", cfg.date_to.c_str());
    std::fprintf(f, "    \"timeframe\": \"%s\",\n", cfg.timeframe.c_str());
    std::fprintf(f, "    \"symbols\": [");
    for (size_t i = 0; i < cfg.symbols.size(); ++i) {
        if (i > 0) std::fprintf(f, ", ");
        std::fprintf(f, "\"%s\"", cfg.symbols[i].c_str());
    }
    std::fprintf(f, "],\n");
    std::fprintf(f, "    \"parameters\": {\n");
    std::fprintf(f, "      \"entry_ema_period\": %d,\n", cfg.strategy.entry_ema_period);
    std::fprintf(f, "      \"entry_ema_distance_pct\": %.4f,\n", cfg.strategy.entry_ema_distance_pct);
    std::fprintf(f, "      \"entry_grid_spacing_pct\": %.4f,\n", cfg.strategy.entry_grid_spacing_pct);
    std::fprintf(f, "      \"initial_qty_pct\": %.4f,\n", cfg.strategy.initial_qty_pct);
    std::fprintf(f, "      \"double_down_factor\": %.4f,\n", cfg.strategy.double_down_factor);
    std::fprintf(f, "      \"close_grid_spacing_pct\": %.4f,\n", cfg.strategy.close_grid_spacing_pct);
    std::fprintf(f, "      \"close_grid_count\": %d,\n", cfg.strategy.close_grid_count);
    std::fprintf(f, "      \"sl_upnl_pct\": %.4f,\n", cfg.strategy.sl_upnl_pct);
    std::fprintf(f, "      \"n_positions\": %d,\n", cfg.strategy.n_positions);
    std::fprintf(f, "      \"parkinson_volatility_span\": %d,\n", cfg.strategy.parkinson_volatility_span);
    std::fprintf(f, "      \"maker_fee_pct\": %.6f,\n", cfg.strategy.maker_fee_pct);
    std::fprintf(f, "      \"time_based_unstuck_pct\": %.4f,\n", cfg.strategy.time_based_unstuck_pct);
    std::fprintf(f, "      \"time_based_unstuck_age\": %d\n", cfg.strategy.time_based_unstuck_age);
    std::fprintf(f, "    },\n");
    std::fprintf(f, "    \"initial_balance_usd\": %.2f,\n", cfg.initial_balance_usd);
    std::fprintf(f, "    \"total_wallet_exposure\": %.4f\n", cfg.total_wallet_exposure);
    std::fprintf(f, "  },\n");

    std::fprintf(f, "  \"equity_curve_size\": %zu,\n", curve.size());
    std::fprintf(f, "  \"final_equity\": %.2f,\n", curve.empty() ? 0.0 : curve.back().equity);
    std::fprintf(f, "  \"final_balance\": %.2f,\n", curve.empty() ? 0.0 : curve.back().balance);

    double const total_return = curve.empty() ? 0.0
        : (curve.back().equity - curve.front().equity) / curve.front().equity;
    std::fprintf(f, "  \"total_return_pct\": %.4f,\n", total_return);

    std::fprintf(f, "  \"metrics\": {\n");
    std::fprintf(f, "    \"gain\": %.10f,\n", m.gain);
    std::fprintf(f, "    \"adg_smoothed\": %.10f,\n", m.adg_smoothed);
    std::fprintf(f, "    \"adg_usd\": %.10f,\n", m.adg_usd);
    std::fprintf(f, "    \"adg_per_exponential_fit_error_usd\": %.10f,\n", m.adg_per_exponential_fit_error_usd);
    std::fprintf(f, "    \"adg_per_exposure_long_usd\": %.10f,\n", m.adg_per_exposure_long_usd);
    std::fprintf(f, "    \"adg_per_exposure_short_usd\": %.10f,\n", m.adg_per_exposure_short_usd);
    std::fprintf(f, "    \"calmar_ratio_usd\": %.10f,\n", m.calmar_ratio_usd);
    std::fprintf(f, "    \"drawdown_worst\": %.10f,\n", m.drawdown_worst);
    std::fprintf(f, "    \"drawdown_worst_mean_1pct\": %.10f,\n", m.drawdown_worst_mean_1pct);
    std::fprintf(f, "    \"entry_initial_balance_pct_long\": %.10f,\n", m.entry_initial_balance_pct_long);
    std::fprintf(f, "    \"entry_initial_balance_pct_short\": %.10f,\n", m.entry_initial_balance_pct_short);
    std::fprintf(f, "    \"equity_balance_diff_neg_max_usd\": %.10f,\n", m.equity_balance_diff_neg_max_usd);
    std::fprintf(f, "    \"equity_balance_diff_neg_mean_usd\": %.10f,\n", m.equity_balance_diff_neg_mean_usd);
    std::fprintf(f, "    \"equity_balance_diff_pos_max_usd\": %.10f,\n", m.equity_balance_diff_pos_max_usd);
    std::fprintf(f, "    \"equity_balance_diff_pos_mean_usd\": %.10f,\n", m.equity_balance_diff_pos_mean_usd);
    std::fprintf(f, "    \"equity_choppiness_usd\": %.10f,\n", m.equity_choppiness_usd);
    std::fprintf(f, "    \"equity_jerkiness_usd\": %.10f,\n", m.equity_jerkiness_usd);
    std::fprintf(f, "    \"expected_shortfall_1pct_usd\": %.10f,\n", m.expected_shortfall_1pct_usd);
    std::fprintf(f, "    \"exponential_fit_error_usd\": %.10f,\n", m.exponential_fit_error_usd);
    std::fprintf(f, "    \"gain_usd\": %.10f,\n", m.gain_usd);
    std::fprintf(f, "    \"gain_per_exposure_long_usd\": %.10f,\n", m.gain_per_exposure_long_usd);
    std::fprintf(f, "    \"gain_per_exposure_short_usd\": %.10f,\n", m.gain_per_exposure_short_usd);
    std::fprintf(f, "    \"loss_profit_ratio\": %.10f,\n", m.loss_profit_ratio);
    std::fprintf(f, "    \"loss_profit_ratio_long\": %.10f,\n", m.loss_profit_ratio_long);
    std::fprintf(f, "    \"loss_profit_ratio_short\": %.10f,\n", m.loss_profit_ratio_short);
    std::fprintf(f, "    \"mdg_usd\": %.10f,\n", m.mdg_usd);
    std::fprintf(f, "    \"mdg_per_exponential_fit_error_usd\": %.10f,\n", m.mdg_per_exponential_fit_error_usd);
    std::fprintf(f, "    \"mdg_per_exposure_long_usd\": %.10f,\n", m.mdg_per_exposure_long_usd);
    std::fprintf(f, "    \"mdg_per_exposure_short_usd\": %.10f,\n", m.mdg_per_exposure_short_usd);
    std::fprintf(f, "    \"omega_ratio_usd\": %.10f,\n", m.omega_ratio_usd);
    std::fprintf(f, "    \"peak_recovery_hours_equity_usd\": %.10f,\n", m.peak_recovery_hours_equity_usd);
    std::fprintf(f, "    \"position_held_hours_max\": %.10f,\n", m.position_held_hours_max);
    std::fprintf(f, "    \"position_held_hours_mean\": %.10f,\n", m.position_held_hours_mean);
    std::fprintf(f, "    \"position_held_hours_median\": %.10f,\n", m.position_held_hours_median);
    std::fprintf(f, "    \"position_unchanged_hours_max\": %.10f,\n", m.position_unchanged_hours_max);
    std::fprintf(f, "    \"positions_held_per_day\": %.10f,\n", m.positions_held_per_day);
    std::fprintf(f, "    \"sharpe_ratio_usd\": %.10f,\n", m.sharpe_ratio_usd);
    std::fprintf(f, "    \"sortino_ratio_usd\": %.10f,\n", m.sortino_ratio_usd);
    std::fprintf(f, "    \"sterling_ratio\": %.10f,\n", m.sterling_ratio);
    std::fprintf(f, "    \"volume_pct_per_day_avg\": %.10f\n", m.volume_pct_per_day_avg);
    std::fprintf(f, "  }\n");
    std::fprintf(f, "}\n");

    std::fclose(f);
}

static std::vector<LoadedCandles> split_candles(const LoadedCandles& loaded,
                                                  size_t n_sym) {
    size_t const total = loaded.candles.size();
    size_t const per = total / n_sym;
    std::vector<LoadedCandles> result;
    result.reserve(n_sym);
    for (size_t i = 0; i < n_sym; ++i) {
        LoadedCandles lc{};
        lc.candles.assign(loaded.candles.begin() + static_cast<ptrdiff_t>(i * per),
                          loaded.candles.begin() + static_cast<ptrdiff_t>((i + 1) * per));
        lc.trading_start_idx = loaded.trading_start_idx;
        result.push_back(std::move(lc));
    }
    return result;
}

static void apply_params_to_cfg(Config& cfg, const std::map<std::string, double>& params) {
    for (const auto& [k, v] : params) {
        if (k == "entry_ema_period")
            cfg.strategy.entry_ema_period = static_cast<int>(v);
        else if (k == "entry_ema_distance_pct")
            cfg.strategy.entry_ema_distance_pct = v;
        else if (k == "entry_grid_spacing_pct")
            cfg.strategy.entry_grid_spacing_pct = v;
        else if (k == "initial_qty_pct")
            cfg.strategy.initial_qty_pct = v;
        else if (k == "double_down_factor")
            cfg.strategy.double_down_factor = v;
        else if (k == "close_grid_spacing_pct")
            cfg.strategy.close_grid_spacing_pct = v;
        else if (k == "close_grid_count")
            cfg.strategy.close_grid_count = static_cast<int>(v);
        else if (k == "sl_upnl_pct")
            cfg.strategy.sl_upnl_pct = v;
        else if (k == "n_positions")
            cfg.strategy.n_positions = static_cast<int>(v);
        else if (k == "parkinson_volatility_span")
            cfg.strategy.parkinson_volatility_span = static_cast<int>(v);
        else if (k == "maker_fee_pct")
            cfg.strategy.maker_fee_pct = v;
        else if (k == "time_based_unstuck_pct")
            cfg.strategy.time_based_unstuck_pct = v;
        else if (k == "time_based_unstuck_age")
            cfg.strategy.time_based_unstuck_age = static_cast<int>(v);
        else if (k == "total_wallet_exposure")
            cfg.total_wallet_exposure = v;
    }
    // Don't recompute warmup_candles here — keep whatever was set by the caller.
    // In OPTIMIZE mode, warmup_candles was set to max_warmup from bounds (matching
    // the data loading). Recomputing it here would break the consistency.
}

/// Find the most recent optimization run directory in optimization_results/.
/// Returns empty path if none found.
static std::string find_latest_opt_dir() {
    namespace fs = std::filesystem;
    if (!fs::is_directory(OPT_DIR)) return {};

    fs::path latest;
    for (auto const& entry : fs::directory_iterator(OPT_DIR)) {
        if (!entry.is_directory()) continue;
        if (latest.empty() || entry.path().filename() > latest.filename()) {
            latest = entry.path();
        }
    }
    return latest.string();
}

/// Read the best candidate's params from the most recent optimization.
/// Tries (in order):
///   1. optimization_results/<latest>/results_pareto.json (first entry)
///   2. optimization_results/.live_state (lowest cv)
/// Returns false if nothing found.
static bool read_best_from_latest_opti(std::map<std::string, double>& out_params) {
    namespace fs = std::filesystem;

    std::string const latest = find_latest_opt_dir();
    if (latest.empty()) {
        // Fallback: try .live_state at the root
        std::string const live_path = std::string(OPT_DIR) + "/.live_state";
        simdjson::padded_string j;
        if (simdjson::padded_string::load(live_path).get(j)) return false;
        simdjson::ondemand::parser p;
        simdjson::ondemand::document doc;
        if (p.iterate(j).get(doc)) return false;
        simdjson::ondemand::object root;
        simdjson::ondemand::array top_arr;
        if (doc.get_object().get(root) || root["top"].get_array().get(top_arr)) return false;
        double best_cv = 1e99;
        for (auto elem : top_arr) {
            simdjson::ondemand::object ro;
            if (elem.get_object().get(ro)) continue;
            double cv = 1e99;
            if (!ro["constraint_violation"].get_double().get(cv) && cv >= best_cv) continue;
            simdjson::ondemand::object params_obj;
            if (ro["params"].get_object().get(params_obj)) continue;
            best_cv = cv;
            out_params.clear();
            for (auto pf : params_obj) {
                std::string_view pk;
                if (pf.unescaped_key().get(pk)) continue;
                double pv;
                if (!pf.value().get_double().get(pv))
                    out_params[std::string(pk)] = pv;
            }
        }
        return !out_params.empty();
    }

    // Try results_pareto.json in the latest dir
    fs::path pareto_path;
    for (auto const& entry : fs::directory_iterator(latest)) {
        std::string name = entry.path().filename().string();
        if (name.find("_pareto.json") != std::string::npos && name.size() > 12) {
            pareto_path = entry.path();
            break;
        }
    }
    if (!pareto_path.empty()) {
        simdjson::padded_string json_data;
        if (simdjson::padded_string::load(pareto_path.string()).get(json_data)) return false;
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc;
        if (parser.iterate(json_data).get(doc)) return false;
        simdjson::ondemand::array arr;
        if (doc.get_array().get(arr)) return false;
        for (auto elem : arr) {
            simdjson::ondemand::object entry;
            if (elem.get_object().get(entry)) continue;
            simdjson::ondemand::object params_obj;
            if (entry["params"].get_object().get(params_obj)) continue;
            for (auto pf : params_obj) {
                std::string_view pk;
                if (pf.unescaped_key().get(pk)) continue;
                double pv;
                if (!pf.value().get_double().get(pv))
                    out_params[std::string(pk)] = pv;
            }
            return true;
        }
    }

    // Fallback: try .live_state in the latest dir, then root
    std::string live_path = latest + "/.live_state";
    simdjson::padded_string j;
    if (simdjson::padded_string::load(live_path).get(j)) {
        live_path = std::string(OPT_DIR) + "/.live_state";
        if (simdjson::padded_string::load(live_path).get(j)) return false;
    }
    simdjson::ondemand::parser p;
    simdjson::ondemand::document doc;
    if (p.iterate(j).get(doc)) return false;
    simdjson::ondemand::object root;
    simdjson::ondemand::array top_arr;
    if (doc.get_object().get(root) || root["top"].get_array().get(top_arr)) return false;
    double best_cv = 1e99;
    for (auto elem : top_arr) {
        simdjson::ondemand::object ro;
        if (elem.get_object().get(ro)) continue;
        double cv = 1e99;
        if (!ro["constraint_violation"].get_double().get(cv) && cv >= best_cv) continue;
        simdjson::ondemand::object params_obj;
        if (ro["params"].get_object().get(params_obj)) continue;
        best_cv = cv;
        out_params.clear();
        for (auto pf : params_obj) {
            std::string_view pk;
            if (pf.unescaped_key().get(pk)) continue;
            double pv;
            if (!pf.value().get_double().get(pv))
                out_params[std::string(pk)] = pv;
        }
    }
    return !out_params.empty();
}

// ============================================================================
// Backtest runners
// ============================================================================

/// Runs a full backtest with the given config and writes results to res_dir.
/// All log output goes to stdout (captured by the TUI when running in optimize
/// mode, or directly to terminal in backtest mode).
static void backtest_and_report(Config cfg, const std::string& res_dir) {
    std::printf("Loading symbol info...\n");
    std::vector<SymbolInfo> const symbols_info = fetch_symbol_info(
        std::string("data/cache"));
    std::printf("  Loaded %zu symbols\n", symbols_info.size());

    std::printf("Loading candles...\n");
    LoadedCandles const loaded = load_candles(cfg);
    size_t const n_sym = cfg.symbols.size();
    std::vector<LoadedCandles> const per_symbol = split_candles(loaded, n_sym);
    std::printf("  Total candles: %zu, per symbol: %zu, trading start: %zu\n",
                loaded.candles.size(), loaded.candles.size() / n_sym,
                loaded.trading_start_idx);

    std::printf("Running backtest...\n");
    BacktestResult const result = run_backtest(cfg, per_symbol, symbols_info, res_dir);
    std::printf("  Equity curve points: %zu\n", result.equity_curve.size());

    std::printf("Computing metrics...\n");
    Metrics const metrics = compute_metrics(result, cfg);

    std::string const json_path = res_dir + "/analysis.json";
    write_analysis_json(json_path, metrics, cfg, result.equity_curve);
    std::printf("  Wrote %s\n", json_path.c_str());

    std::printf("Generating charts...\n");
    Plotter plotter(cfg, result.equity_curve, metrics, res_dir);
    plotter.generate_all();

    if (!result.equity_curve.empty()) {
        auto const& first = result.equity_curve.front();
        auto const& last = result.equity_curve.back();
        std::printf("\nBacktest complete:\n");
        std::printf("  Initial equity: %.2f\n", first.equity);
        std::printf("  Final equity:   %.2f\n", last.equity);
        std::printf("  Return:         %+.4f%%\n",
                    (last.equity - first.equity) / first.equity * 100.0);
        std::printf("  Sharpe:         %.4f\n", metrics.sharpe_ratio_usd);
        std::printf("  Calmar:         %.4f\n", metrics.calmar_ratio_usd);
    }
}

// ============================================================================
// Optimizer runner (with integrated TUI)
// ============================================================================

static void run_optimize(Config const& cfg_in, bool backtest_best) {
    // Make a mutable copy so we can set warmup_candles = max_warmup
    Config cfg = cfg_in;

    // Optimization results ALWAYS go to optimization_results/ (separate from
    // backtests which go to backtests/).
    std::string const res_dir = create_results_dir(OPT_DIR);
    std::string const live_state = std::string(OPT_DIR) + "/.live_state";
    std::printf("Optimization results dir: %s\n", res_dir.c_str());
    std::printf("Live state: %s  (use '--tui' in another terminal to watch progress)\n",
                live_state.c_str());

    std::printf("Loading symbol info...\n");
    std::vector<SymbolInfo> const symbols_info = fetch_symbol_info(
        std::string("data/cache"));
    std::printf("  Loaded %zu symbols\n", symbols_info.size());

    int max_warmup = cfg.warmup_candles;
    for (const auto& [name, bound] : cfg.optimize.bounds) {
        if (name == "entry_ema_period" || name == "parkinson_volatility_span") {
            int const bval = static_cast<int>(bound[1]);
            if (bval > max_warmup) max_warmup = bval;
        }
    }
    // Set cfg.warmup_candles to max_warmup so that make_config_from_genes
    // uses the same warmup for all candidates, matching the data loading.
    cfg.warmup_candles = max_warmup;

    std::printf("Loading candles (warmup=%d)...\n", max_warmup);
    LoadedCandles const loaded = load_candles(cfg);
    size_t const n_sym = cfg.symbols.size();
    std::vector<LoadedCandles> const per_symbol = split_candles(loaded, n_sym);
    std::printf("  Total candles: %zu, per symbol: %zu, trading start: %zu\n",
                loaded.candles.size(), loaded.candles.size() / n_sym,
                loaded.trading_start_idx);

    std::printf("Running optimization...\n");

    // Progress is printed to stdout. A separate '--tui' command can be run in
    // another terminal to watch the live state (Pareto front table) via ncurses.
    auto opt_callback = [&](const RunResult& rr, size_t gen, size_t n_gen) {
        std::string obj_str;
        for (size_t i = 0; i < rr.objectives.size(); ++i) {
            if (i > 0) obj_str += " ";
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.4f", rr.objectives[i]);
            obj_str += buf;
        }
        std::printf("\r  Gen %zu / %zu objectives=[%s] cv=%.6f  ",
                    gen, n_gen, obj_str.c_str(), rr.constraint_violation);
        std::fflush(stdout);
    };

    std::string results_path = res_dir + "/results";
    OptimizerResult const opt_result = run_optimization(
        cfg, per_symbol, symbols_info, results_path, opt_callback, live_state);

    std::printf("\n");

    auto const& results = opt_result.all_results;

    if (results.empty()) {
        std::printf("  No valid results.\n");
        return;
    }

    std::printf("  Pareto front size: %zu\n", opt_result.pareto_front.size());

    // Log top 5 to stdout
    std::printf("  Top 5 results:\n");
    for (size_t i = 0; i < std::min(size_t{5}, results.size()); ++i) {
        auto const& r = results[i];
        std::string obj_str;
        for (size_t j = 0; j < r.objectives.size(); ++j) {
            if (j > 0) obj_str += " ";
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.4f", r.objectives[j]);
            obj_str += buf;
        }
        std::printf("    #%zu: cv=%.6f objectives=[%s]\n",
                    i + 1, r.constraint_violation, obj_str.c_str());
        for (const auto& [k, v] : r.params) {
            std::printf("      %s = %.4f\n", k.c_str(), v);
        }
    }

    // Backtest the best candidate if requested.
    // All backtests go directly to backtests/<timestamp>/ (no more best/ subdir).
    if (backtest_best && !results.empty() && results[0].constraint_violation < 1e-10) {
        std::printf("\nBacktesting best candidate...\n");
        Config best_cfg = cfg;
        apply_params_to_cfg(best_cfg, results[0].params);

        std::string best_dir = create_results_dir(BT_DIR);
        backtest_and_report(best_cfg, best_dir);
    }
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char** argv) {
    // ── Standalone --tui: watch live optimization progress ─────────────────
    if (argc >= 2 && std::strcmp(argv[1], "--tui") == 0) {
        std::string const live_state = std::string(OPT_DIR) + "/.live_state";
        std::printf("Watching live state: %s\n", live_state.c_str());
        std::printf("(run 'optimize <config>' in another terminal to start an optimization)\n");
        run_watch_tui(live_state);
        return 0;
    }

    // ── Standalone --backtest-best (no config needed) ──────────────────────
    if (argc >= 2 && std::strcmp(argv[1], "--backtest-best") == 0) {
        std::printf("PowerMDG v1.0\n");
        std::printf("  Mode: --backtest-best (no config, uses latest optimization)\n");

        std::map<std::string, double> best_params;
        if (!read_best_from_latest_opti(best_params)) {
            std::fprintf(stderr, "No optimization results found in %s/.\n", OPT_DIR);
            std::fprintf(stderr, "Run 'optimize <config>' first.\n");
            return 1;
        }

        std::string const latest = find_latest_opt_dir();
        std::printf("  Source: %s\n", latest.empty() ? (std::string(OPT_DIR) + "/.live_state").c_str() : latest.c_str());
        std::printf("  Best candidate params:\n");
        for (const auto& [k, v] : best_params) {
            std::printf("    %s = %.4f\n", k.c_str(), v);
        }

        // Build a Config from the best params. We need to load the original
        // config that was used for the optimization to get symbols, dates, etc.
        // Strategy: look for a config.json copy in the optimization_results dir
        // (copied there during the optimize run).
        Config cfg;
        namespace fs = std::filesystem;
        std::string config_path;
        // Try optimization_results/config.json first (copied by run_optimize)
        std::string candidate = std::string(OPT_DIR) + "/config.json";
        if (fs::exists(candidate)) config_path = candidate;
        // Fallback: try in the latest subdir
        if (config_path.empty() && !latest.empty()) {
            candidate = latest + "/config.json";
            if (fs::exists(candidate)) config_path = candidate;
        }
        if (config_path.empty()) {
            // Fallback: ask user to provide a config
            std::fprintf(stderr, "\nNo config.json found in %s/. The --backtest-best\n", OPT_DIR);
            std::fprintf(stderr, "mode needs the original config to know symbols, dates, etc.\n");
            std::fprintf(stderr, "Please re-run with: %s optimize <config.json> --backtest-best\n", argv[0]);
            return 1;
        }

        cfg = load_config(config_path, Mode::BACKTEST);
        apply_params_to_cfg(cfg, best_params);

        std::printf("\n  Symbols:  ");
        for (size_t i = 0; i < cfg.symbols.size(); ++i) {
            if (i > 0) std::printf(", ");
            std::printf("%s", cfg.symbols[i].c_str());
        }
        std::printf("\n  Balance:  %.2f USDT\n", cfg.initial_balance_usd);
        std::printf("  Exposure: %.2f\n", cfg.total_wallet_exposure);
        std::printf("  Warmup:   %d candles\n", cfg.warmup_candles);

        // All backtests go directly to backtests/<timestamp>/ (no more best/ subdir).
        std::string best_dir = create_results_dir(BT_DIR);
        std::printf("\nBacktest results dir: %s\n", best_dir.c_str());
        backtest_and_report(cfg, best_dir);
        return 0;
    }

    // ── Regular modes: backtest/optimize <config> ──────────────────────────
    if (argc < 3) {
        usage(argv[0]);
    }

    std::string const config_path(argv[2]);
    bool backtest_best = false;

    // Parse optional flags (only --backtest-best is supported as a flag here;
    // --tui is a standalone mode handled above)
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backtest-best") == 0) {
            backtest_best = true;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            usage(argv[0]);
        }
    }

    std::string_view const mode_arg(argv[1]);
    Mode mode;
    if (mode_arg == "backtest") {
        mode = Mode::BACKTEST;
    } else if (mode_arg == "optimize") {
        mode = Mode::OPTIMIZE;
    } else {
        usage(argv[0]);
    }

    Config const cfg = load_config(config_path, mode);

    std::printf("PowerMDG v1.0\n");
    std::printf("  Mode:     %s\n", mode_str(cfg.mode));
    std::printf("  Symbols:  ");
    for (size_t i = 0; i < cfg.symbols.size(); ++i) {
        if (i > 0) std::printf(", ");
        std::printf("%s", cfg.symbols[i].c_str());
    }
    std::printf("\n");
    std::printf("  Balance:  %.2f USDT\n", cfg.initial_balance_usd);
    std::printf("  Exposure: %.2f\n", cfg.total_wallet_exposure);
    std::printf("  Warmup:   %d candles\n", cfg.warmup_candles);

    if (mode == Mode::BACKTEST) {
        // Backtest results go to backtests/
        std::string const res_dir = create_results_dir(BT_DIR);
        std::printf("Backtest results dir: %s\n", res_dir.c_str());
        backtest_and_report(cfg, res_dir);
    } else {
        // Copy the config into the optimization results dir so --backtest-best
        // can later reconstruct the Config without the original file.
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(OPT_DIR, ec);
        // We'll copy after create_results_dir is called inside run_optimize,
        // so we pass the config_path and copy it there. For simplicity, copy
        // to optimization_results/config.json (overwritten each run).
        fs::copy_file(config_path, std::string(OPT_DIR) + "/config.json",
                      fs::copy_options::overwrite_existing, ec);

        run_optimize(cfg, backtest_best);
    }

    return 0;
}
