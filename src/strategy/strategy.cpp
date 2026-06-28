#include "strategy.h"
#include "strategy/modules/closes_algo/closes_algo.h"
#include "strategy/modules/entry_condition/entry_condition.h"
#include "strategy/modules/entries_algo/entries_algo.h"
#include "strategy/parkinson_volatility.h"
#include "strategy/stop_loss.h"
#include "strategy/unstuck.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

namespace powermdg {
namespace {

/// Computes the next EMA value iteratively.
double next_ema(double alpha, double close, double prev_ema) {
    return alpha * close + (1.0 - alpha) * prev_ema;
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

BacktestResult run_backtest(const Config& cfg,
                            const std::vector<LoadedCandles>& per_symbol_candles,
                            const std::vector<SymbolInfo>& symbols_info,
                            const std::string& output_dir) {
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
    if (!entry_condition || !entries_algo || !closes_algo) {
        std::fprintf(stderr, "Failed to create strategy modules\n");
        return {};
    }

    // Aggregate data needs — only compute indicators that at least one active module requires
    DataNeed const needs = entry_condition->data_needs() | entries_algo->data_needs() | closes_algo->data_needs();
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

    // Volatility ranking helpers
    std::vector<size_t> sym_order(n);
    std::iota(sym_order.begin(), sym_order.end(), size_t{0});
    std::vector<double> parkinson_vol(n, 0.0);

    for (size_t i = 0; i < nc; ++i) {
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

        // Compute Parkinson volatility for each symbol
        for (size_t s = 0; s < n; ++s) {
            parkinson_vol[s] = compute_parkinson_volatility(
                per_symbol_candles[s].candles, i, cfg.strategy.parkinson_volatility_span);
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

        // ── Step a: CLOSES ─────────────────────────────────────────────────
        // Ask closes_algo for close orders, then execute them.
        for (size_t s = 0; s < n; ++s) {
            if (positions[s].total_qty > 1e-12) {
                double const rstd = need_stdev ? stdev_values[s] : 0.0;
                ModuleContext ctx{cfg, symbols_info[s], *current_candles[s],
                                  positions[s], balance, static_cast<int64_t>(i),
                                  need_ema ? ema_values[s] : 0.0, rstd,
                                  need_candles ? std::span<const Candle>(per_symbol_candles[s].candles) : std::span<const Candle>{}};
                auto close_orders = closes_algo->compute_closes(ctx);
                for (auto const& co : close_orders) {
                    execute_close(positions[s], co, *current_candles[s], cfg);
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

        // ── Step b: STOP LOSS ──────────────────────────────────────────────
        for (size_t s = 0; s < n; ++s) {
            if (check_stop_loss(cfg, *current_candles[s], positions[s])) {
                if (total_positions > 0) {
                    total_positions -= 1;
                }
            }
        }

        // ── Step c: ENTRIES ────────────────────────────────────────────────
        // In the original code, process_entries checked the EMA threshold for
        // BOTH first entry AND double-down (the check was before the branch).
        // We preserve this: entry_condition is checked for both.
        for (size_t s = 0; s < n; ++s) {
            if (!is_active[s]) continue;

            double const wallet_exposure_limit = cfg.total_wallet_exposure
                / static_cast<double>(cfg.strategy.n_positions);

            // Build context once (used for both entry_condition and entries_algo)
            double const rstd2 = need_stdev ? stdev_values[s] : 0.0;
            ModuleContext ctx{cfg, symbols_info[s], *current_candles[s],
                              positions[s], balance, static_cast<int64_t>(i),
                              need_ema ? ema_values[s] : 0.0, rstd2,
                              need_candles ? std::span<const Candle>(per_symbol_candles[s].candles) : std::span<const Candle>{}};

            // Check entry condition for BOTH first entry and double-down
            // (matches original behavior: EMA check was before the branch)
            if (!entry_condition->should_enter(ctx)) continue;

            if (positions[s].total_qty < 1e-12) {
                // ── First entry ──
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

        // ── Step d: TIME-BASED UNSTUCK ─────────────────────────────────────
        for (size_t s = 0; s < n; ++s) {
            if (positions[s].total_qty > 1e-12) {
                bool const closed = check_time_based_unstuck(cfg, *current_candles[s],
                                                              positions[s], i);
                if (closed && positions[s].total_qty < 1e-12 && total_positions > 0) {
                    total_positions -= 1;
                }
            }
        }

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

        // Record equity point
        double const exposure = total_exposure(positions, current_candles);
        double const equity = total_equity(balance, positions, current_candles);

        EquityPoint ep{};
        ep.timestamp = current_candles[0]->timestamp;
        ep.equity = equity;
        ep.balance = balance;
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

    result.final_positions = std::move(positions);
    return result;
}

} // namespace powermdg
