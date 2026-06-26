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
#include <string>
#include <string_view>
#include <vector>

using namespace martingale;

[[noreturn]] static void usage(char const* prog) {
    std::fprintf(stderr, "Usage: %s <backtest|optimize> <config.json> [--backtest-best]\n", prog);
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
    std::fprintf(f, "      \"maker_fee_pct\": %.6f\n", cfg.strategy.maker_fee_pct);
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
    std::fprintf(f, "    \"adg_usd\": %.10f,\n", m.adg_usd);
    std::fprintf(f, "    \"adg_per_exponential_fit_error_usd\": %.10f,\n", m.adg_per_exponential_fit_error_usd);
    std::fprintf(f, "    \"adg_per_exposure_long_usd\": %.10f,\n", m.adg_per_exposure_long_usd);
    std::fprintf(f, "    \"adg_per_exposure_short_usd\": %.10f,\n", m.adg_per_exposure_short_usd);
    std::fprintf(f, "    \"calmar_ratio_usd\": %.10f,\n", m.calmar_ratio_usd);
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
    std::fprintf(f, "    \"sterling_ratio_usd\": %.10f,\n", m.sterling_ratio_usd);
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
    std::string const res_dir = create_results_dir(cfg.output.dir);
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
    Metrics const metrics = compute_metrics(result.equity_curve, cfg);

    std::string const json_path = res_dir + "/analysis.json";
    write_analysis_json(json_path, metrics, cfg, result.equity_curve);
    std::printf("  Wrote %s\n", json_path.c_str());

    std::printf("Generating charts...\n");
    Plotter plotter(cfg, result.equity_curve, res_dir);
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
    Metrics const metrics = compute_metrics(result.equity_curve, cfg);

    std::string const json_path = res_dir + "/analysis.json";
    write_analysis_json(json_path, metrics, cfg, result.equity_curve);
    std::printf("  Wrote %s\n", json_path.c_str());

    std::printf("Generating charts...\n");
    Plotter plotter(cfg, result.equity_curve, res_dir);
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

static void run_optimize(Config const& cfg, bool backtest_best) {
    std::string const base = cfg.output.dir + "/optimize";
    std::string const res_dir = create_results_dir(base);
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

    // Create TUI for live display
    OptimizerTUI tui(cfg.optimize.scoring, cfg.optimize.limits,
                     max_warmup > 0 ? 100 : 0);

    auto tui_callback = [&](const RunResult& rr, size_t /*current*/, size_t /*total*/) {
        tui.push_result(rr);
        if (tui.should_abort()) {
            std::printf("\n  Optimization aborted by user.\n");
        }
    };

    std::string results_path = res_dir + "/results";
    std::vector<RunResult> const results = run_optimization(
        cfg, per_symbol, symbols_info, results_path, tui_callback);

    tui.finish();

    if (results.empty()) {
        std::printf("  No valid results.\n");
        return;
    }

    // Log top 5 to stdout
    std::printf("  Top 5 results:\n");
    for (size_t i = 0; i < std::min(size_t{5}, results.size()); ++i) {
        auto const& r = results[i];
        std::printf("    #%zu: score=%.4f valid=%d\n", i + 1, r.score, r.valid);
        for (const auto& [k, v] : r.params) {
            std::printf("      %s = %.4f\n", k.c_str(), v);
        }
    }

    // Backtest the best candidate if requested
    if (backtest_best && !results.empty() && results[0].valid) {
        std::printf("\nBacktesting best candidate...\n");
        Config best_cfg = cfg;
        for (const auto& [k, v] : results[0].params) {
            if (k == "entry_ema_period")
                best_cfg.strategy.entry_ema_period = static_cast<int>(v);
            else if (k == "entry_ema_distance_pct")
                best_cfg.strategy.entry_ema_distance_pct = v;
            else if (k == "entry_grid_spacing_pct")
                best_cfg.strategy.entry_grid_spacing_pct = v;
            else if (k == "initial_qty_pct")
                best_cfg.strategy.initial_qty_pct = v;
            else if (k == "double_down_factor")
                best_cfg.strategy.double_down_factor = v;
            else if (k == "close_grid_spacing_pct")
                best_cfg.strategy.close_grid_spacing_pct = v;
            else if (k == "close_grid_count")
                best_cfg.strategy.close_grid_count = static_cast<int>(v);
            else if (k == "sl_upnl_pct")
                best_cfg.strategy.sl_upnl_pct = v;
            else if (k == "n_positions")
                best_cfg.strategy.n_positions = static_cast<int>(v);
            else if (k == "parkinson_volatility_span")
                best_cfg.strategy.parkinson_volatility_span = static_cast<int>(v);
            else if (k == "maker_fee_pct")
                best_cfg.strategy.maker_fee_pct = v;
            else if (k == "total_wallet_exposure")
                best_cfg.total_wallet_exposure = v;
        }
        int const a = best_cfg.strategy.entry_ema_period;
        int const b = best_cfg.strategy.parkinson_volatility_span;
        best_cfg.warmup_candles = (a > b) ? a : b;

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

    std::string_view const mode_arg(argv[1]);
    std::string const config_path(argv[2]);
    bool backtest_best = false;

    // Parse optional flags
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backtest-best") == 0) {
            backtest_best = true;
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
        run_optimize(cfg, backtest_best);
    }

    return 0;
}
