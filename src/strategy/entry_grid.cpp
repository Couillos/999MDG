#include "entry_grid.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace martingale {
namespace {

/// Rounds a value down to the nearest step.
double round_step(double val, double step) {
    return std::round(val / step) * step;
}

} // anonymous namespace

void process_entries(const Config& strat, const SymbolInfo& info,
                     const Candle& candle, double total_balance,
                     int& total_positions, Position& pos,
                     bool is_active, double ema) {
    // Skip if not active and no existing position
    if (!is_active && std::abs(pos.total_qty) < 1e-12) {
        return;
    }

    // Entry condition: close must be above EMA threshold
    double const threshold = ema * (1.0 + strat.strategy.entry_ema_distance_pct);
    if (candle.close <= threshold) {
        return;
    }

    double const slot_capital = (total_balance * strat.total_wallet_exposure)
                              / static_cast<double>(strat.strategy.n_positions);

    if (std::abs(pos.total_qty) < 1e-12) {
        // First entry into this symbol
        if (total_positions >= strat.strategy.n_positions) {
            return;
        }

        double const entry_size_usd = slot_capital * strat.strategy.initial_qty_pct;
        double const qty = round_step(entry_size_usd / candle.close, info.step_size);

        if (qty < info.min_qty) {
            return;
        }

        pos.avg_entry_price = candle.close;
        pos.total_qty = qty;
        pos.entry_levels = 1;
        // Apply maker fee: fee = abs(order_value) * maker_fee_pct
        double const entry_fee = std::abs(qty * candle.close) * strat.strategy.maker_fee_pct;
        pos.realized_pnl -= entry_fee;
        total_positions += 1;
    } else {
        // Double-down logic
        double const price_drop_pct = (pos.avg_entry_price - candle.close)
                                    / pos.avg_entry_price;
        if (price_drop_pct <= 0.0) {
            return;
        }

        int const levels_filled = static_cast<int>(
            std::floor(price_drop_pct / strat.strategy.entry_grid_spacing_pct));

        if (levels_filled < pos.entry_levels) {
            return;
        }

        for (int lvl = pos.entry_levels; lvl <= levels_filled; ++lvl) {
            double const mult = std::pow(strat.strategy.double_down_factor,
                                         static_cast<double>(lvl));
            double const entry_size_usd = slot_capital
                                        * strat.strategy.initial_qty_pct * mult;
            double const qty = round_step(entry_size_usd / candle.close,
                                          info.step_size);

            if (qty < info.min_qty) {
                break;
            }

            // Apply maker fee: fee = abs(order_value) * maker_fee_pct
            double const entry_fee = std::abs(qty * candle.close) * strat.strategy.maker_fee_pct;
            pos.realized_pnl -= entry_fee;

            // Update weighted average entry price
            double const total_cost = pos.avg_entry_price * pos.total_qty
                                    + candle.close * qty;
            pos.total_qty += qty;
            pos.avg_entry_price = total_cost / pos.total_qty;
            pos.entry_levels = lvl + 1;
        }
    }
}

} // namespace martingale
