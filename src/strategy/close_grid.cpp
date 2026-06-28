#include "close_grid.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace powermdg {
namespace {

double round_step(double val, double step) {
    return std::round(val / step) * step;
}

/// Resets per-position entry state so the next entry starts fresh.
/// NOTE: entry_timestamp_ms is NOT reset here — strategy.cpp's position
/// tracking needs it to detect the open→closed transition and record
/// the position duration. strategy.cpp resets it after recording.
void reset_position_state(Position& pos) {
    pos.entry_levels = 0;
    pos.unstuck_levels = 0;
    pos.entry_tick = 0;
}

} // anonymous namespace

void process_closes(const Config& strat, const SymbolInfo& info,
                    const Candle& candle, Position& pos,
                    int& total_positions) {
    // BUG 11 FIX: use epsilon comparison instead of == 0.0.
    if (std::abs(pos.total_qty) < 1e-12) {
        return;
    }

    for (int k = 1; k <= strat.strategy.close_grid_count; ++k) {
        if (std::abs(pos.total_qty) < 1e-12) {
            break;  // position fully closed, stop iterating
        }

        double const target_price = pos.avg_entry_price
            * (1.0 + static_cast<double>(k) * strat.strategy.close_grid_spacing_pct);

        if (candle.close < target_price) {
            continue;
        }

        // UPnL condition: upnl >= k * close_grid_spacing_pct
        double const upnl = (candle.close - pos.avg_entry_price) / pos.avg_entry_price;
        if (upnl < static_cast<double>(k) * strat.strategy.close_grid_spacing_pct) {
            continue;
        }

        double close_qty = round_step(pos.total_qty
            / static_cast<double>(strat.strategy.close_grid_count), info.step_size);
        close_qty = std::min(close_qty, pos.total_qty);

        double const fee = std::abs(close_qty * candle.close)
                         * strat.strategy.maker_fee_pct;
        pos.realized_pnl += close_qty * (candle.close - pos.avg_entry_price) - fee;
        pos.total_qty -= close_qty;
        pos.traded_qty += close_qty;
    }

    // Guard against floating-point residuals
    if (std::abs(pos.total_qty) < 1e-12) {
        pos.total_qty = 0.0;
    }

    // If position fully closed via grid, reset state and decrement total_positions.
    if (pos.total_qty <= 0.0) {
        reset_position_state(pos);
        if (total_positions > 0) {
            total_positions -= 1;
        }
    }
}

} // namespace powermdg
