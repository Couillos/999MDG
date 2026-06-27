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
#include <vector>

using namespace martingale;

[[noreturn]] static void usage(char const* prog) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s backtest <config.json>\n"
        "  %s optimize <config.json> [--tui] [--backtest-best]\n"
        "  %s --tui <config.json>\n"
        "  %s --backtest-best <config.json>\n",
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
    std::fprintf(f, "      \"time_based_unstuck_threshold\": %.4f,\n", cfg.strategy.time_based_unstuck_threshold);
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

static void run_backtest(Config const& cfg) {
    std::string const res_dir = create_results_dir("backtests");
    std::printf("Results dir: %s\n", res_dir.c_str());

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

/// Runs a full backtest with the given config and returns its results + metrics.
static void backtest_and_report(Config cfg, const std::string& res_dir) {
    std::printf("Loading symbol info...\n");
    std::vector<SymbolInfo> const symbols_info = fetch_symbol_info(
        std::string("data/cache"));
    std::printf("  Loaded %zu symbols\n", symbols_info.size());

    std::printf("Loading candles...\n");
    LoadedCandles const loaded = load_candles(cfg);
    size_t const n_sym = cfg.symbols.size();
    std::vector<LoadedCandles> const per_symbol = split_candles(loaded, n_sym);

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
        else if (k == "time_based_unstuck_threshold")
            cfg.strategy.time_based_unstuck_threshold = v;
        else if (k == "time_based_unstuck_age")
            cfg.strategy.time_based_unstuck_age = static_cast<int>(v);
        else if (k == "total_wallet_exposure")
            cfg.total_wallet_exposure = v;
    }
    int const a = cfg.strategy.entry_ema_period;
    int const b = cfg.strategy.parkinson_volatility_span;
    cfg.warmup_candles = (a > b) ? a : b;
}

/// Find the most recent pareto JSON in optimize_results/*/ and read the first entry's params.
/// Returns false if nothing found.
static bool read_pareto_best(const std::string& base_path,
                             std::map<std::string, double>& out_params) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(base_path)) return false;

    // Find most recent subdirectory by name (YYYY-MM-DD_HH-MM-SS)
    fs::path latest;
    for (auto const& entry : fs::directory_iterator(base_path)) {
        if (!entry.is_directory()) continue;
        if (latest.empty() || entry.path().filename() > latest.filename()) {
            latest = entry.path();
        }
    }
    if (latest.empty()) return false;

    // Look for _pareto.json
    fs::path pareto_path;
    for (auto const& entry : fs::directory_iterator(latest)) {
        std::string name = entry.path().filename().string();
        if (name.find("_pareto.json") != std::string::npos && name.size() > 12) {
            pareto_path = entry.path();
            break;
        }
    }
    if (pareto_path.empty()) return false;

    // Read JSON array, take first entry's params
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
    return false;
}

static void run_optimize(Config const& cfg, bool backtest_best, bool show_tui) {
    std::string const base = "optimize_results";
    std::string const res_dir = create_results_dir(base);
    std::string const live_state = "optimize_results/.live_state";
    std::printf("Results dir: %s\n", res_dir.c_str());

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
    Config load_cfg = cfg;
    load_cfg.warmup_candles = max_warmup;

    std::printf("Loading candles (warmup=%d)...\n", max_warmup);
    LoadedCandles const loaded = load_candles(load_cfg);
    size_t const n_sym = cfg.symbols.size();
    std::vector<LoadedCandles> const per_symbol = split_candles(loaded, n_sym);
    std::printf("  Total candles: %zu, per symbol: %zu, trading start: %zu\n",
                loaded.candles.size(), loaded.candles.size() / n_sym,
                loaded.trading_start_idx);

    std::printf("Running optimization...\n");

    // TUI or stdout progress
    std::unique_ptr<OptimizerTUI> tui;
    if (show_tui) {
        tui = std::make_unique<OptimizerTUI>(
            cfg.optimize.scoring, cfg.optimize.limits, 0);
    }

    auto opt_callback = [&](const RunResult& rr, size_t gen, size_t n_gen) {
        if (tui) {
            if (!tui->has_total() && n_gen > 0) tui->set_total(n_gen);
            tui->push_result(rr);
            if (tui->should_abort()) {
                std::printf("\n  Optimization aborted by user.\n");
            }
        } else {
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
        }
    };

    std::string results_path = res_dir + "/results";
    OptimizerResult const opt_result = run_optimization(
        cfg, per_symbol, symbols_info, results_path, opt_callback, live_state);

    if (tui) tui->finish();
    else std::printf("\n");

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

    // Backtest the best candidate if requested (inline)
    if (backtest_best && !results.empty() && results[0].constraint_violation < 1e-10) {
        std::printf("\nBacktesting best candidate...\n");
        Config best_cfg = cfg;
        apply_params_to_cfg(best_cfg, results[0].params);

        std::string best_dir = res_dir + "/best";
        std::error_code ec;
        std::filesystem::create_directories(best_dir, ec);
        backtest_and_report(best_cfg, best_dir);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        usage(argv[0]);
    }

    std::string const config_path(argv[2]);

    // Check for standalone --tui or --backtest-best mode
    if (std::strcmp(argv[1], "--tui") == 0) {
        Config const cfg = load_config(config_path, Mode::OPTIMIZE);
        std::printf("Watching live state: optimize_results/.live_state\n");
        run_watch_tui("optimize_results/.live_state");
        return 0;
    }

    if (std::strcmp(argv[1], "--backtest-best") == 0) {
        Config cfg = load_config(config_path, Mode::OPTIMIZE);
        std::map<std::string, double> best_params;
        // 1) Try most recent _pareto.json
        if (!read_pareto_best("optimize_results", best_params)) {
            // 2) Try .live_state — pick entry with LOWEST constraint_violation
            std::string const live_path = "optimize_results/.live_state";
            simdjson::padded_string j;
            if (!simdjson::padded_string::load(live_path).get(j)) {
                simdjson::ondemand::parser p;
                simdjson::ondemand::document doc;
                if (!p.iterate(j).get(doc)) {
                    simdjson::ondemand::object root;
                    simdjson::ondemand::array top_arr;
                    if (!doc.get_object().get(root) && !root["top"].get_array().get(top_arr)) {
                        double best_cv = 1e99;
                        for (auto elem : top_arr) {
                            simdjson::ondemand::object ro;
                            if (elem.get_object().get(ro)) continue;
                            double cv = 1e99;
                            if (!ro["constraint_violation"].get_double().get(cv) && cv > best_cv) continue;
                            simdjson::ondemand::object params_obj;
                            if (ro["params"].get_object().get(params_obj)) continue;
                            best_cv = cv;
                            best_params.clear();
                            for (auto pf : params_obj) {
                                std::string_view pk;
                                if (pf.unescaped_key().get(pk)) continue;
                                double pv;
                                if (!pf.value().get_double().get(pv))
                                    best_params[std::string(pk)] = pv;
                            }
                        }
                    }
                }
            }
        }

        if (best_params.empty()) {
            std::fprintf(stderr, "No optimization results yet — run 'optimize' first.\n");
            return 1;
        }

        std::printf("Backtesting best candidate from most recent optimization:\n");
        for (const auto& [k, v] : best_params) {
            std::printf("  %s = %.4f\n", k.c_str(), v);
        }

        apply_params_to_cfg(cfg, best_params);

        std::string best_dir = "optimize_results/best_from_live";
        std::error_code ec;
        std::filesystem::create_directories(best_dir, ec);
        backtest_and_report(cfg, best_dir);
        return 0;
    }

    // Regular modes: backtest or optimize
    std::string_view const mode_arg(argv[1]);
    bool backtest_best = false;
    bool show_tui = false;

    // Parse optional flags
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backtest-best") == 0) {
            backtest_best = true;
        } else if (std::strcmp(argv[i], "--tui") == 0) {
            show_tui = true;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            usage(argv[0]);
        }
    }

    Mode mode;
    if (mode_arg == "backtest") {
        mode = Mode::BACKTEST;
    } else if (mode_arg == "optimize") {
        mode = Mode::OPTIMIZE;
    } else {
        usage(argv[0]);
    }

    Config const cfg = load_config(config_path, mode);

    std::printf("Martingale v1.0\n");
    std::printf("  Mode:     %s\n", mode_str(cfg.mode));
    std::printf("  Symbols:  ");
    for (size_t i = 0; i < cfg.symbols.size(); ++i) {
        if (i > 0) {
            std::printf(", ");
        }
        std::printf("%s", cfg.symbols[i].c_str());
    }
    std::printf("\n");
    std::printf("  Balance:  %.2f USDT\n", cfg.initial_balance_usd);
    std::printf("  Exposure: %.2f\n", cfg.total_wallet_exposure);
    std::printf("  Warmup:   %d candles\n", cfg.warmup_candles);

    if (mode == Mode::BACKTEST) {
        run_backtest(cfg);
    } else {
        run_optimize(cfg, backtest_best, show_tui);
    }

    return 0;
}
