#include "strategy.h"
#include "strategy/modules/closes_algo/closes_algo.h"
#include "strategy/modules/entry_condition/entry_condition.h"
#include "strategy/modules/entries_algo/entries_algo.h"
#include "strategy/parkinson_volatility.h"
#include "strategy/modules/loss_modules/loss_module.h"
#include "data/candle_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <string>
#include <thread>
#include <vector>
#include <set>
#include <map>

// ── DEBUG: strategy-level counters ──
static std::atomic<size_t> debug_strat_candle_count{0};
static std::atomic<size_t> debug_strat_position_candle_count{0};
static std::atomic<size_t> debug_strat_entry_condition_calls{0};
static std::atomic<size_t> debug_strat_entry_calls{0};
static std::atomic<size_t> debug_strat_close_calls{0};
static std::atomic<size_t> debug_strat_loss_calls{0};
static std::atomic<double> debug_strat_close_time_ms{0};
static std::atomic<double> debug_strat_loss_time_ms{0};
static std::atomic<double> debug_strat_entry_time_ms{0};
static std::atomic<double> debug_strat_pv_time_ms{0};

static void log_strat_stats() {
    static thread_local auto last_log = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - last_log).count() < 5.0) return;
    last_log = now;
    std::fprintf(stderr,
        "[DEBUG] [strategy] candles=%zu pos_candles=%zu entry_cond=%zu closes=%zu(calls=%zu time=%.0fms) loss=%zu(calls=%zu time=%.0fms) entries=%zu(calls=%zu time=%.0fms) pv_time=%.0fms\n",
        debug_strat_candle_count.load(), debug_strat_position_candle_count.load(),
        debug_strat_entry_condition_calls.load(),
        debug_strat_close_calls.load(), debug_strat_close_calls.load(), debug_strat_close_time_ms.load(),
        debug_strat_loss_calls.load(), debug_strat_loss_calls.load(), debug_strat_loss_time_ms.load(),
        debug_strat_entry_calls.load(), debug_strat_entry_calls.load(), debug_strat_entry_time_ms.load(),
        debug_strat_pv_time_ms.load());
}

namespace powermdg {
namespace {

/// Computes the next EMA value iteratively.
double next_ema(double alpha, double close, double prev_ema) {
    return alpha * close + (1.0 - alpha) * prev_ema;
}

/// VWAP over last N candles (lookback window).
double compute_vwap(std::span<const Candle> cs, size_t end, size_t lb) {
    double pv = 0.0, vv = 0.0;
    size_t st = (end > lb) ? end - lb : 0;
    for (size_t i = st; i <= end && i < cs.size(); ++i) {
        double tp = (cs[i].high + cs[i].low + cs[i].close) / 3.0;
        pv += tp * cs[i].volume;
        vv += cs[i].volume;
    }
    return vv > 0.0 ? pv / vv : 0.0;
}

/// Population stdev of close over last N candles.
double compute_stdev(std::span<const Candle> cs, size_t end, size_t lb) {
    if (end < 2) return 0.0;
    size_t st = (end > lb) ? end - lb : 0;
    size_t n = 0;
    double s = 0.0, sq = 0.0;
    for (size_t i = st; i <= end && i < cs.size(); ++i) {
        s += cs[i].close;
        sq += cs[i].close * cs[i].close;
        ++n;
    }
    if (n < 2) return 0.0;
    double m = s / n;
    double v = sq / n - m * m;
    return std::sqrt(std::max(0.0, v));
}

/// Computes current exposure (sum of position values) for all symbols.
double total_exposure(const std::vector<Position>& positions,
                      const std::vector<const Candle*>& candles) {
    double sum = 0.0;
    for (size_t i = 0; i < positions.size(); ++i) {
        sum += positions[i].total_qty * candles[i]->close;
    }

    return sum;
}

/// Computes current equity (balance + unrealized PnL).
double total_equity(double balance, const std::vector<Position>& positions,
                    const std::vector<const Candle*>& candles) {
    double unrealized = 0.0;
    for (size_t i = 0; i < positions.size(); ++i) {
        if (positions[i].total_qty > 0.0) {
            unrealized += positions[i].total_qty
                        * (candles[i]->close - positions[i].avg_entry_price);
        }
    }
    return balance + unrealized;
}

/// Resets per-position entry state so the next entry starts fresh.
/// NOTE: entry_timestamp_ms is NOT reset here — the position tracking code
/// needs it to detect the open→closed transition and record the duration.
/// avg_entry_price is NOT reset (matching original close_grid.cpp behavior).
void reset_position_state(Position& pos) {
    pos.entry_levels = 0;
    pos.unstuck_levels = 0;
    pos.entry_tick = 0;
    pos.entry_side = 0;
    pos.original_qty = 0.0;
    pos.tp1_fired = false;
}

/// Execute a close order: update pos (realized PnL, fees, qty, traded_qty).
/// Does NOT reset position state on full close — caller handles that.
void execute_close(Position& pos, const CloseOrder& co, const Candle& candle,
                   const Config& cfg) {
    double const fee = std::abs(co.qty * candle.close) * cfg.strategy.maker_fee_pct;
    pos.realized_pnl += co.qty * (candle.close - pos.avg_entry_price) - fee;
    pos.total_qty -= co.qty;
    pos.traded_qty += co.qty;
}

/// Execute a first entry: open a new position from zero.
void execute_first_entry(Position& pos, const EntryOrder& eo, const Candle& candle,
                         const Config& cfg, int64_t current_tick) {
    double const fee = std::abs(eo.qty * candle.close) * cfg.strategy.maker_fee_pct;
    pos.avg_entry_price = candle.close;
    pos.total_qty = eo.qty;
    pos.entry_levels = 1;
    pos.entry_tick = current_tick;
    pos.entry_timestamp_ms = candle.timestamp;
    pos.unstuck_levels = 0;
    pos.realized_pnl -= fee;
    // H3: record the side and initial qty for modules that need them
    pos.entry_side = 1;          // long position
    pos.original_qty = eo.qty;
}

/// Execute a double-down entry: add to an existing position.
void execute_double_down(Position& pos, const EntryOrder& eo, const Candle& candle,
                         const Config& cfg) {
    double const fee = std::abs(eo.qty * candle.close) * cfg.strategy.maker_fee_pct;
    pos.realized_pnl -= fee;
    double const total_cost = pos.avg_entry_price * pos.total_qty
                            + candle.close * eo.qty;
    pos.total_qty += eo.qty;
    pos.avg_entry_price = total_cost / pos.total_qty;
    pos.entry_levels += 1;
}

} // anonymous namespace


/// Build the tf_data map for a given symbol at a given tick.
/// Uses the LAST CLOSED HTF candle (not the forming one).
std::map<std::string, TfCandles> build_tf_data(
    size_t s, int64_t cur_ts,
    const std::map<std::string, std::vector<LoadedCandles>>& mtf_candles,
    bool need_mtf,
    std::map<std::string, size_t>& cursor)
{
    std::map<std::string, TfCandles> tfd;
    if (!need_mtf) return tfd;
    for (auto const& [tf, vlc] : mtf_candles) {
        if (s >= vlc.size()) continue;
        auto const& candles = vlc[s].candles;
        if (candles.empty()) continue;
        size_t& idx = cursor[tf];
        while (idx + 1 < candles.size() && candles[idx + 1].timestamp <= cur_ts) {
            ++idx;
        }
        size_t last_closed = (idx > 0) ? idx - 1 : 0;
        tfd[tf] = {tf, std::span<const Candle>(candles), last_closed};
    }
    return tfd;
}

// ── Multi-timeframe helper ──────────────────────────────────────────────────

std::map<std::string, std::vector<LoadedCandles>> load_all_mtf_candles(const Config& cfg) {
    std::set<std::string> needed_tfs;
    needed_tfs.insert(cfg.timeframe);
    for (auto const& [param_name, tf] : cfg.strategy.indicator_timeframes) {
        if (!tf.empty()) needed_tfs.insert(tf);
    }

    std::map<std::string, std::vector<LoadedCandles>> mtf_data;
    if (needed_tfs.size() <= 1) return mtf_data;

    std::printf("Multi-timeframe mode: timeframes = [");
    bool first = true;
    for (auto const& tf : needed_tfs) {
        if (!first) std::printf(", ");
        std::printf("%s", tf.c_str());
        first = false;
    }
    std::printf("]\n");

    for (auto const& tf : needed_tfs) {
        if (tf == cfg.timeframe) continue;
        Config mtf_cfg = cfg;
        mtf_cfg.timeframe = tf;
        mtf_data[tf] = {};
        for (size_t s = 0; s < cfg.symbols.size(); ++s) {
            LoadedCandles lc = load_candles(mtf_cfg);
            mtf_data[tf].push_back(std::move(lc));
        }
    }
    return mtf_data;
}

// ── Backtest entry point ────────────────────────────────────────────────────

BacktestResult run_backtest(const Config& cfg,
                            const std::vector<LoadedCandles>& per_symbol_candles,
                            const std::vector<SymbolInfo>& symbols_info,
                            const std::string& output_dir,
                            const std::map<std::string, std::vector<LoadedCandles>>* mtf_candles) {
    size_t const n = cfg.symbols.size();
    size_t const nc = per_symbol_candles[0].candles.size();

    if (n == 0 || nc == 0) {
        return {};
    }

    // Compute trading start from warmup candles (allows optimizer to vary warmup)
    size_t const ts = static_cast<size_t>(cfg.warmup_candles);

    // Instantiate strategy modules from config (one set for the whole backtest)
    auto entry_condition = create_entry_condition(cfg.strategy.entry_condition_type);
    auto entries_algo = create_entries_algo(cfg.strategy.entries_algo_type);
    auto closes_algo = create_closes_algo(cfg.strategy.closes_algo_type);
    auto loss_modules = powermdg::create_loss_modules(cfg.strategy.loss_algo_types);
    if (cfg.strategy.loss_algo_types.empty()) {
        // Default: use legacy stop_loss + unstuck
        loss_modules = powermdg::create_loss_modules({"legacy_stop_loss", "legacy_unstuck"});
    }
    if (!entry_condition || !entries_algo || !closes_algo) {
        std::fprintf(stderr, "Failed to create strategy modules\n");
        return {};
    }

    // Aggregate data needs — only compute indicators that at least one active module requires
    DataNeed needs = entry_condition->data_needs() | entries_algo->data_needs() | closes_algo->data_needs();
    for (auto const& lm : loss_modules) needs = needs | lm->data_needs();
    bool const need_mtf = powermdg::needs(needs, DataNeed::MultiTimeframe);

    // Multi-timeframe: use pre-loaded data when provided, else load now
    std::map<std::string, std::vector<LoadedCandles>> local_mtf;
    if (mtf_candles) {
        // Pre-loaded — nothing to do here
    } else if (need_mtf && !cfg.strategy.indicator_timeframes.empty()) {
        local_mtf = load_all_mtf_candles(cfg);
        mtf_candles = &local_mtf;
    }
    static const std::map<std::string, std::vector<LoadedCandles>> EMPTY_MTF;
    const auto& mtf_ref = mtf_candles ? *mtf_candles : EMPTY_MTF;
    bool const need_ema = powermdg::needs(needs, DataNeed::Ema) || powermdg::needs(needs, DataNeed::RollingStdev);
    bool const need_stdev = powermdg::needs(needs, DataNeed::RollingStdev);
    bool const need_candles = powermdg::needs(needs, DataNeed::CandleSeries);

    // Per-symbol state
    std::vector<Position> positions(n, Position{});
    std::vector<double> ema_values;
    std::vector<double> ema_sq_values;
    std::vector<double> stdev_values;
    double alpha = 0.0;
    if (need_ema) {
        ema_values.resize(n, 0.0);
        alpha = 2.0 / (static_cast<double>(cfg.strategy.entry_ema_period) + 1.0);
        for (size_t s = 0; s < n; ++s) ema_values[s] = per_symbol_candles[s].candles[0].close;
    }
    if (need_stdev) {
        ema_sq_values.resize(n, 0.0);
        stdev_values.resize(n, 0.0);
        for (size_t s = 0; s < n; ++s) ema_sq_values[s] = per_symbol_candles[s].candles[0].close * per_symbol_candles[s].candles[0].close;
    }

    // CSV output (written during the loop if output_dir is set)
    FILE* eq_csv = nullptr;
    FILE* ex_csv = nullptr;
    FILE* pnl_csv = nullptr;
    if (!output_dir.empty()) {
        std::string const data_dir = output_dir + "/data";
        std::error_code ec;
        std::filesystem::create_directories(data_dir, ec);
        eq_csv = std::fopen((data_dir + "/equity_curve.csv").c_str(), "w");
        if (eq_csv) {
            std::fprintf(eq_csv, "timestamp,equity,balance\n");
        }
        ex_csv = std::fopen((data_dir + "/exposure.csv").c_str(), "w");
        if (ex_csv) {
            std::fprintf(ex_csv, "timestamp,exposure_usd\n");
        }
        pnl_csv = std::fopen((data_dir + "/pnl_symbol.csv").c_str(), "w");
        if (pnl_csv) {
            std::fprintf(pnl_csv, "timestamp");
            for (size_t s = 0; s < n; ++s) {
                std::fprintf(pnl_csv, ",%s", cfg.symbols[s].c_str());
            }
            std::fprintf(pnl_csv, "\n");
        }
    }

    BacktestResult result;
    size_t const reserve_count = (ts < nc) ? (nc - ts) : 0;
    result.equity_curve.reserve(reserve_count);
    int total_positions = 0;
    double balance = cfg.initial_balance_usd;
    bool bankrupt = false;  // liquidation floor flag

    // Volatility ranking helpers
    std::vector<size_t> sym_order(n);
    std::iota(sym_order.begin(), sym_order.end(), size_t{0});
    std::vector<double> parkinson_vol(n, 0.0);

    // MTF cursor: per-symbol per-timeframe index that advances monotonically
    std::vector<std::map<std::string, size_t>> mtf_cursors(n);

    for (size_t i = 0; i < nc; ++i) {
        debug_strat_candle_count.fetch_add(1);

        // Gather candle pointers for this tick
        std::vector<const Candle*> current_candles(n);
        for (size_t s = 0; s < n; ++s) {
            current_candles[s] = &per_symbol_candles[s].candles[i];
        }

        // Save entry timestamp at the START of the tick so we can detect
        // close→reopen transitions within the same tick.
        std::vector<int64_t> prev_entry_ts(n, 0);
        for (size_t s = 0; s < n; ++s) {
            prev_entry_ts[s] = positions[s].entry_timestamp_ms;
        }

        // Update iterative EMA and EMA of close² (only if modules need them)
        for (size_t s = 0; s < n; ++s) {
            double const close = current_candles[s]->close;
            if (need_ema) ema_values[s] = next_ema(alpha, close, ema_values[s]);
            if (need_stdev) {
                ema_sq_values[s] = next_ema(alpha, close * close, ema_sq_values[s]);
                stdev_values[s] = std::sqrt(std::max(0.0, ema_sq_values[s] - ema_values[s] * ema_values[s]));
            }
        }

        // Skip warmup period
        if (i < ts) {
            continue;
        }

        // Compute Parkinson volatility for each symbol (with optional MTF)
        {
            auto t0 = std::chrono::steady_clock::now();
            auto pv_tf_it = cfg.strategy.indicator_timeframes.find("parkinson_volatility_span");
            if (pv_tf_it != cfg.strategy.indicator_timeframes.end() && need_mtf) {
                auto const& tf_name = pv_tf_it->second;
                auto mtf_it = mtf_ref.find(tf_name);
                if (mtf_it != mtf_ref.end()) {
                    for (size_t s = 0; s < n; ++s) {
                        auto const& mtf_arr = mtf_it->second[s].candles;
                        if (mtf_arr.empty()) {
                            parkinson_vol[s] = 0.0;
                            continue;
                        }
                        int64_t cur_ts = current_candles[s]->timestamp;
                        size_t& mtf_idx = mtf_cursors[s][tf_name];
                        while (mtf_idx + 1 < mtf_arr.size() && mtf_arr[mtf_idx + 1].timestamp <= cur_ts) {
                            ++mtf_idx;
                        }
                        parkinson_vol[s] = compute_parkinson_volatility(
                            mtf_arr, mtf_idx, cfg.strategy.parkinson_volatility_span);
                    }
                } else {
                    for (size_t s = 0; s < n; ++s)
                        parkinson_vol[s] = compute_parkinson_volatility(
                            per_symbol_candles[s].candles, i, cfg.strategy.parkinson_volatility_span);
                }
            } else {
                for (size_t s = 0; s < n; ++s)
                    parkinson_vol[s] = compute_parkinson_volatility(
                        per_symbol_candles[s].candles, i, cfg.strategy.parkinson_volatility_span);
            }
            auto t1 = std::chrono::steady_clock::now();
            debug_strat_pv_time_ms.fetch_add(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }

        // Sort symbols by volatility descending; stable for deterministic order
        std::sort(sym_order.begin(), sym_order.end(),
            [&](size_t a, size_t b) {
                return parkinson_vol[a] > parkinson_vol[b];
            });

        // Build active set (top n_positions by vol)
        std::vector<bool> is_active(n, false);
        for (int a = 0; a < cfg.strategy.n_positions && a < static_cast<int>(n); ++a) {
            is_active[sym_order[static_cast<size_t>(a)]] = true;
        }

        // Compute VWAP and stdev per symbol (cached for modules that need candle_series)
        std::vector<double> vwap_values(n, 0.0);
        std::vector<double> stdev_pop_values(n, 0.0);
        if (need_candles) {
            size_t const vwap_lb = static_cast<size_t>(cfg.strategy.zscore_vwap_lookback);
            for (size_t s = 0; s < n; ++s) {
                auto const& cs = per_symbol_candles[s].candles;
                vwap_values[s] = compute_vwap(std::span<const Candle>(cs), i, vwap_lb);
                stdev_pop_values[s] = compute_stdev(std::span<const Candle>(cs), i, vwap_lb);
            }
        }

        // ── Step a: CLOSES ─────────────────────────────────────────────────
        // Ask closes_algo for close orders, then execute them.
        // Step a contains ONLY take-profit closes (stop-loss/unstuck are in Step b),
        // so "closes_algo scaled out this tick" == "a TP fired". We set tp1_fired
        // whenever any close order executed (qty > 0), regardless of price vs
        // avg_entry. This unblocks graduated_tp TP2/TP3 (which can fire while
        // close < avg_entry on a z-score revert) and lets time_stop disengage.
        {
            auto t0 = std::chrono::steady_clock::now();
        for (size_t s = 0; s < n; ++s) {
            if (positions[s].total_qty > 1e-12) {
                double const rstd = need_stdev ? stdev_values[s] : 0.0;
                ModuleContext ctx{cfg, symbols_info[s], *current_candles[s],
                                  positions[s], balance, static_cast<int64_t>(i),
                                  need_ema ? ema_values[s] : 0.0, rstd,
                                  vwap_values[s], stdev_pop_values[s],
                                  need_candles ? std::span<const Candle>(per_symbol_candles[s].candles) : std::span<const Candle>{},
                                  i,
                                   build_tf_data(s, current_candles[s]->timestamp, mtf_ref, need_mtf, mtf_cursors[s])};
                auto close_orders = closes_algo->compute_closes(ctx);
                bool any_tp_close = false;
                for (auto const& co : close_orders) {
                    if (co.qty > 1e-12) {
                        any_tp_close = true;
                    }
                    execute_close(positions[s], co, *current_candles[s], cfg);
                }
                // DEFECT 3 FIX: set tp1_fired whenever closes_algo executed any close
                // this tick — regardless of price vs avg_entry. Step a is TP-only, so
                // "executed a close" means "TP fired". This enables graduated_tp TP2/TP3
                // even when TP1 fired while close < avg_entry (z-score revert in-loss).
                if (any_tp_close) {
                    positions[s].tp1_fired = true;
                }
                // Guard against floating-point residuals
                if (std::abs(positions[s].total_qty) < 1e-12) {
                    positions[s].total_qty = 0.0;
                    reset_position_state(positions[s]);
                    if (total_positions > 0) {
                        total_positions -= 1;
                    }
                }
            }
        }
            auto t1 = std::chrono::steady_clock::now();
            debug_strat_close_calls.fetch_add(1);
            debug_strat_close_time_ms.fetch_add(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }

        // ── Step b: LOSS MODULES (replaces stop_loss + unstuck) ───────────
        {
            auto t0 = std::chrono::steady_clock::now();
        for (size_t s = 0; s < n; ++s) {
            if (positions[s].total_qty > 1e-12) {
                double const rstd_l = need_stdev ? stdev_values[s] : 0.0;
                ModuleContext lctx{cfg, symbols_info[s], *current_candles[s],
                                  positions[s], balance, static_cast<int64_t>(i),
                                  need_ema ? ema_values[s] : 0.0, rstd_l,
                                  vwap_values[s], stdev_pop_values[s],
                                  need_candles ? std::span<const Candle>(per_symbol_candles[s].candles) : std::span<const Candle>{},
                                  i,
                                   build_tf_data(s, current_candles[s]->timestamp, mtf_ref, need_mtf, mtf_cursors[s])};
                for (auto const& lm : loss_modules) {
                    // DEFECT 1 FIX: track qty before each loss module so we can
                    // detect a partial de-risk (unstuck tranche) vs a full close.
                    double const qty_before_lm = positions[s].total_qty;
                    auto loss_orders = lm->compute_loss_exits(lctx);
                    for (auto const& co : loss_orders) {
                        execute_close(positions[s], co, *current_candles[s], cfg);
                    }
                    // If the position is still open (partial close, not a full stop),
                    // and qty strictly decreased, this was an unstuck tranche.
                    // Increment unstuck_levels so legacy_unstuck's gate
                    // (expected <= unstuck_levels) prevents it re-firing every candle.
                    double const qty_after_lm = positions[s].total_qty;
                    if (qty_after_lm > 1e-12 && qty_after_lm < qty_before_lm - 1e-12) {
                        positions[s].unstuck_levels += 1;
                    }
                    if (std::abs(positions[s].total_qty) < 1e-12) break;
                }
                if (std::abs(positions[s].total_qty) < 1e-12) {
                    positions[s].total_qty = 0.0;
                    reset_position_state(positions[s]);
                    if (total_positions > 0) total_positions -= 1;
                }
            }
        }
            auto t1 = std::chrono::steady_clock::now();
            debug_strat_loss_calls.fetch_add(1);
            debug_strat_loss_time_ms.fetch_add(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }

        // ── Step c: ENTRIES ────────────────────────────────────────────────
        // C2: entry_condition->should_enter() is ONLY checked for the first
        // entry (no open position).  Double-downs are governed entirely by the
        // entries_algo grid spacing — blocking them with an EMA condition would
        // prevent the martingale from averaging down on the very dip it was
        // designed to capture.
        // Liquidation floor: skip all new entries if we are bankrupt.
        {
            auto t0 = std::chrono::steady_clock::now();
            // Count position candles (where modules do more work)
            for (size_t s = 0; s < n; ++s) {
                if (positions[s].total_qty > 1e-12) {
                    debug_strat_position_candle_count.fetch_add(1);
                    break;
                }
            }
        if (!bankrupt) {
        for (size_t s = 0; s < n; ++s) {
            if (!is_active[s]) continue;

            double const wallet_exposure_limit = cfg.total_wallet_exposure
                / static_cast<double>(cfg.strategy.n_positions);

            // Build context once (used for both entry_condition and entries_algo)
            double const rstd2 = need_stdev ? stdev_values[s] : 0.0;
            ModuleContext ctx{cfg, symbols_info[s], *current_candles[s],
                              positions[s], balance, static_cast<int64_t>(i),
                              need_ema ? ema_values[s] : 0.0, rstd2,
                              vwap_values[s], stdev_pop_values[s],
                              need_candles ? std::span<const Candle>(per_symbol_candles[s].candles) : std::span<const Candle>{},
                              i,
                               build_tf_data(s, current_candles[s]->timestamp, mtf_ref, need_mtf, mtf_cursors[s])};

            if (positions[s].total_qty < 1e-12) {
                // ── First entry ──
                // Gate on entry_condition only here (C2 fix)
                debug_strat_entry_condition_calls.fetch_add(1);
                if (!entry_condition->should_enter(ctx)) continue;
                if (total_positions >= cfg.strategy.n_positions) continue;

                auto entry_orders = entries_algo->compute_entries(ctx);
                if (!entry_orders.empty()) {
                    execute_first_entry(positions[s], entry_orders[0],
                                        *current_candles[s], cfg,
                                        static_cast<int64_t>(i));
                    total_positions += 1;
                }
            } else {
                // ── Double-down ──
                // No entry_condition check: governed solely by grid spacing (C2 fix)
                double const current_we = balance > 0.0
                    ? std::abs(positions[s].total_qty * current_candles[s]->close) / balance
                    : 0.0;
                if (current_we >= wallet_exposure_limit * 0.999) continue;

                auto entry_orders = entries_algo->compute_entries(ctx);
                for (auto const& eo : entry_orders) {
                    execute_double_down(positions[s], eo, *current_candles[s], cfg);
                }
            }
        }
        } // end if (!bankrupt)
            auto t1 = std::chrono::steady_clock::now();
            debug_strat_entry_calls.fetch_add(1);
            debug_strat_entry_time_ms.fetch_add(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }

        // (Step d: TIME-BASED UNSTUCK removed — now handled by loss modules in Step b)

        // ── Step e: enforce exposure limit (auto-reduce if > 1% over limit) ──
        {
            double const we_limit = cfg.total_wallet_exposure
                                  / static_cast<double>(cfg.strategy.n_positions);
            for (size_t s = 0; s < n; ++s) {
                if (positions[s].total_qty > 1e-12 && balance > 0.0) {
                    double const we = std::abs(positions[s].total_qty
                                             * current_candles[s]->close) / balance;
                    if (we > we_limit * 1.01) {
                        double const ideal_qty = we_limit * 1.01 * balance
                                               / current_candles[s]->close;
                        double const reduce_qty = positions[s].total_qty - ideal_qty;
                        if (reduce_qty > 1e-12) {
                            double const fee = std::abs(reduce_qty * current_candles[s]->close)
                                             * cfg.strategy.maker_fee_pct;
                            positions[s].realized_pnl += reduce_qty
                                * (current_candles[s]->close - positions[s].avg_entry_price) - fee;
                            positions[s].total_qty -= reduce_qty;
                            positions[s].traded_qty += reduce_qty;
                        }
                    }
                }
            }
        }

        // Recompute balance from realized PnL
        balance = cfg.initial_balance_usd;
        for (size_t s = 0; s < n; ++s) {
            balance += positions[s].realized_pnl;
        }

        // ── Liquidation floor: equity must never go negative ──────────────
        // Compute tentative equity to check for blow-up.
        {
            double const tentative_equity = total_equity(balance, positions, current_candles);
            if (!bankrupt && tentative_equity <= 0.0) {
                // Force-liquidate all open positions at current close price.
                for (size_t s = 0; s < n; ++s) {
                    if (positions[s].total_qty > 1e-12) {
                        double const fee = std::abs(positions[s].total_qty
                                                  * current_candles[s]->close)
                                         * cfg.strategy.maker_fee_pct;
                        positions[s].realized_pnl +=
                            positions[s].total_qty
                            * (current_candles[s]->close - positions[s].avg_entry_price)
                            - fee;
                        positions[s].traded_qty += positions[s].total_qty;
                        positions[s].total_qty = 0.0;
                        reset_position_state(positions[s]);
                        if (total_positions > 0) total_positions -= 1;
                    }
                }
                // Clamp balance/realized_pnl so equity records as 0
                balance = 0.0;
                for (size_t s = 0; s < n; ++s) {
                    positions[s].realized_pnl = 0.0;
                }
                bankrupt = true;
            }
        }

        // ── Track position entry/exit for position_held_hours ──────────────
        for (size_t s = 0; s < n; ++s) {
            bool const now_open = positions[s].total_qty > 1e-12;
            int64_t const old_ts = prev_entry_ts[s];
            int64_t const new_ts = positions[s].entry_timestamp_ms;

            if (old_ts != 0) {
                if (!now_open) {
                    double const hrs = static_cast<double>(
                        current_candles[s]->timestamp - old_ts) / 3600000.0;
                    result.position_durations_hours.push_back(hrs);
                    positions[s].entry_timestamp_ms = 0;
                } else if (new_ts != old_ts) {
                    double const hrs = static_cast<double>(
                        current_candles[s]->timestamp - old_ts) / 3600000.0;
                    result.position_durations_hours.push_back(hrs);
                }
            }
        }

        // Periodic debug log
        if (i % 50000 == 0 && i > 0) {
            log_strat_stats();
            std::fprintf(stderr, "[DEBUG] [strategy] candle %zu/%zu (%.0f%%) pos_count=%d balance=%.2f\n",
                         i, nc, 100.0 * i / nc, total_positions, balance);
        }

        // Record equity point (floored at 0 when bankrupt)
        double const exposure = bankrupt ? 0.0 : total_exposure(positions, current_candles);
        double const equity = bankrupt ? 0.0 : total_equity(balance, positions, current_candles);

        EquityPoint ep{};
        ep.timestamp = current_candles[0]->timestamp;
        ep.equity = equity;
        ep.balance = bankrupt ? 0.0 : balance;
        ep.exposure_usd = exposure;

        for (size_t s = 0; s < n; ++s) {
            ep.symbol_pnl.emplace_back(cfg.symbols[s],
                                       positions[s].realized_pnl);
        }

        result.equity_curve.push_back(ep);

        // Write CSV rows during the loop
        if (eq_csv) {
            std::fprintf(eq_csv, "%lld,%.8f,%.8f\n",
                         static_cast<long long>(ep.timestamp), ep.equity, ep.balance);
        }
        if (ex_csv) {
            std::fprintf(ex_csv, "%lld,%.8f\n",
                         static_cast<long long>(ep.timestamp), ep.exposure_usd);
        }
        if (pnl_csv) {
            std::fprintf(pnl_csv, "%lld", static_cast<long long>(ep.timestamp));
            for (size_t s = 0; s < n; ++s) {
                std::fprintf(pnl_csv, ",%.8f", ep.symbol_pnl[s].second);
            }
            std::fprintf(pnl_csv, "\n");
        }
    }

    if (eq_csv) std::fclose(eq_csv);
    if (ex_csv) std::fclose(ex_csv);
    if (pnl_csv) std::fclose(pnl_csv);

    // Handle still-open positions (like PassivBot counts them too)
    int64_t const last_ts = result.equity_curve.empty()
        ? 0 : result.equity_curve.back().timestamp;
    for (size_t s = 0; s < n; ++s) {
        if (positions[s].entry_timestamp_ms != 0) {
            double hrs = static_cast<double>(
                last_ts - positions[s].entry_timestamp_ms) / 3600000.0;
            result.position_durations_hours.push_back(hrs);
        }
    }

    // ── DEBUG: summary per backtest run ──
    std::fprintf(stderr,
        "[DEBUG] [strategy] BACKTEST DONE: total_candles=%zu position_candles=%zu equity_points=%zu\n"
        "[DEBUG] [strategy]   entry_cond_calls=%zu close_calls=%zu loss_calls=%zu entry_calls=%zu\n"
        "[DEBUG] [strategy]   time_ms: pv=%.0f closes=%.0f loss=%.0f entries=%.0f\n",
        nc, debug_strat_position_candle_count.load(),
        result.equity_curve.size(),
        debug_strat_entry_condition_calls.load(),
        debug_strat_close_calls.load(), debug_strat_loss_calls.load(), debug_strat_entry_calls.load(),
        debug_strat_pv_time_ms.load(), debug_strat_close_time_ms.load(),
        debug_strat_loss_time_ms.load(), debug_strat_entry_time_ms.load());

    result.final_positions = std::move(positions);
    return result;
}

} // namespace powermdg
