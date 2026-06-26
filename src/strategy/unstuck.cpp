#include "unstuck.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace martingale {

bool check_time_based_unstuck(const Config& strat, const Candle& candle,
                               Position& pos, size_t current_tick) {
    if (pos.total_qty < 1e-12) {
        return false;
    }

    double const pct = strat.strategy.time_based_unstuck_pct;
    double const threshold = strat.strategy.time_based_unstuck_threshold;
    int const age = strat.strategy.time_based_unstuck_age;

    if (pct <= 0.0 || age <= 0 || pos.entry_tick == 0) {
        return false;
    }

    int64_t const held = static_cast<int64_t>(current_tick) - pos.entry_tick;
    if (held < age) {
        return false;
    }

    double const upnl = (candle.close - pos.avg_entry_price) / pos.avg_entry_price;
    if (upnl <= threshold) {
        return false;
    }

    int const expected = static_cast<int>(held / age);
    if (expected <= pos.unstuck_levels) {
        return false;
    }

    double total_closed = 0.0;
    for (int lvl = pos.unstuck_levels; lvl < expected; ++lvl) {
        double const close_qty = pos.total_qty * pct;
        if (close_qty < 1e-12) {
            break;
        }

        double const fee = std::abs(close_qty * candle.close)
                         * strat.strategy.maker_fee_pct;
        pos.realized_pnl += close_qty * (candle.close - pos.avg_entry_price) - fee;
        pos.total_qty -= close_qty;
        pos.traded_qty += close_qty;
        total_closed += close_qty;
        pos.unstuck_levels = lvl + 1;

        if (pos.total_qty < 1e-12) {
            break;
        }
    }

    if (std::abs(pos.total_qty) < 1e-12) {
        pos.total_qty = 0.0;
    }

    return total_closed > 0.0;
}

} // namespace martingale
