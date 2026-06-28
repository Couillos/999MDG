#include "stop_loss.h"
#include <cmath>
#include <cstdlib>

namespace powermdg {

bool check_stop_loss(const Config& strat, const Candle& candle, Position& pos) {
    if (std::abs(pos.total_qty) < 1e-12) {
        return false;
    }

    double const weighted_upnl = (candle.close - pos.avg_entry_price)
                               / pos.avg_entry_price;

    if (weighted_upnl > strat.strategy.sl_upnl_pct) {
        return false;
    }

    // Fully close the position
    double const fee = std::abs(pos.total_qty * candle.close)
                     * strat.strategy.maker_fee_pct;
    pos.realized_pnl += pos.total_qty * (candle.close - pos.avg_entry_price) - fee;
    pos.traded_qty += pos.total_qty;
    pos.total_qty = 0.0;

    // Reset entry state for the next position.
    // NOTE: entry_timestamp_ms is NOT reset here — strategy.cpp's position
    // tracking needs it to record the duration. strategy.cpp resets it.
    // Fixes BUG 5: previously only entry_levels was reset, leaving
    // unstuck_levels stale for the next position.
    pos.entry_levels = 0;
    pos.unstuck_levels = 0;
    pos.entry_tick = 0;

    return true;
}

} // namespace powermdg
