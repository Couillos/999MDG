#include "strategy.h"
#include "strategy/close_grid.h"
#include "strategy/entry_grid.h"
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

namespace martingale {
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

    // Per-symbol state
    std::vector<Position> positions(n, Position{});
    std::vector<double> ema_values(n, 0.0);
    double const alpha = 2.0 / (static_cast<double>(cfg.strategy.entry_ema_period) + 1.0);

    // Initialize EMAs with the first candle close of each symbol
    for (size_t s = 0; s < n; ++s) {
        ema_values[s] = per_symbol_candles[s].candles[0].close;
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
    // Fix for audit issue S1: guard against ts >= nc which would underflow
    // the reserve() argument and throw std::length_error under -fno-exceptions.
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

        // Update iterative EMA
        for (size_t s = 0; s < n; ++s) {
            ema_values[s] = next_ema(alpha, current_candles[s]->close, ema_values[s]);
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

        // Step a: process_closes for all symbols with open positions
        for (size_t s = 0; s < n; ++s) {
            if (positions[s].total_qty > 1e-12) {
                process_closes(cfg, symbols_info[s], *current_candles[s],
                               positions[s], total_positions);
            }
        }

        // Step b: check_stop_loss for ALL symbols
        for (size_t s = 0; s < n; ++s) {
            if (check_stop_loss(cfg, *current_candles[s], positions[s])) {
                if (total_positions > 0) {
                    total_positions -= 1;
                }
            }
        }

        // Step c: process_entries for active symbols
        for (size_t s = 0; s < n; ++s) {
            if (is_active[s]) {
                process_entries(cfg, symbols_info[s], *current_candles[s],
                                balance, total_positions, positions[s],
                                true, ema_values[s],
                                static_cast<int64_t>(i));
            }
        }

        // Step d: time-based unstuck for all symbols with open positions
        for (size_t s = 0; s < n; ++s) {
            if (positions[s].total_qty > 1e-12) {
                bool const closed = check_time_based_unstuck(cfg, *current_candles[s],
                                                              positions[s], i);
                if (closed && positions[s].total_qty < 1e-12 && total_positions > 0) {
                    total_positions -= 1;
                }
            }
        }

        // Step e: enforce exposure limit (auto-reduce if > 1% over limit)
        // PassivBot-style: when wallet_exposure_ratio > 1.01, crop position
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

        // Track position entry/exit for position_held_hours (like PassivBot's fill-based approach).
        // Uses prev_entry_ts (saved at start of tick) to correctly handle close→reopen
        // within the same tick.
        for (size_t s = 0; s < n; ++s) {
            bool const now_open = positions[s].total_qty > 1e-12;
            int64_t const old_ts = prev_entry_ts[s];
            int64_t const new_ts = positions[s].entry_timestamp_ms;

            if (old_ts != 0) {
                // Position was open at start of tick.
                if (!now_open) {
                    // Closed during this tick (close_grid / stop_loss / unstuck / crop).
                    double const hrs = static_cast<double>(
                        current_candles[s]->timestamp - old_ts) / 3600000.0;
                    result.position_durations_hours.push_back(hrs);
                    positions[s].entry_timestamp_ms = 0;
                } else if (new_ts != old_ts) {
                    // Closed AND reopened this tick (entry_grid set a new timestamp).
                    double const hrs = static_cast<double>(
                        current_candles[s]->timestamp - old_ts) / 3600000.0;
                    result.position_durations_hours.push_back(hrs);
                    // new_ts is already set by entry_grid — keep it.
                }
                // else: still open, unchanged.
            }
            // else: was closed at start of tick. If now_open, entry_grid already
            // set entry_timestamp_ms. Nothing to record.
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

} // namespace martingale
