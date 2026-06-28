#include "unstuck.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace powermdg {

bool check_time_based_unstuck(const Config& strat, const Candle& candle,
                               Position& pos, size_t /*current_tick*/) {
    if (pos.total_qty < 1e-12) {
        return false;
    }

    double const pct = strat.strategy.time_based_unstuck_pct;
    int const age = strat.strategy.time_based_unstuck_age;

    // Use timestamp (ms) instead of bar index — entry_timestamp_ms is ms since
    // epoch, so age is correctly interpreted in hours regardless of timeframe.
    if (pct <= 0.0 || age <= 0 || pos.entry_timestamp_ms == 0) {
        return false;
    }

    int64_t const held_ms = candle.timestamp - pos.entry_timestamp_ms;
    int64_t const age_ms = static_cast<int64_t>(age) * 3600000LL;
    if (held_ms < age_ms) {
        return false;
    }

    // Number of unstuck tranches that should have fired by now.
    int64_t const expected = held_ms / age_ms;
    if (expected <= static_cast<int64_t>(pos.unstuck_levels)) {
        return false;
    }

    // Each level closes a fixed exposure tranche = pct of initial balance.
    double const exposure_tranche = pct * strat.initial_balance_usd;

    double total_closed = 0.0;
    for (int64_t lvl = static_cast<int64_t>(pos.unstuck_levels); lvl < expected; ++lvl) {
        double const close_qty = std::min(
            exposure_tranche / candle.close,
            pos.total_qty
        );

        if (close_qty < 1e-12) {
            if (pos.total_qty > 1e-12) {
                double const fee = std::abs(pos.total_qty * candle.close)
                                 * strat.strategy.maker_fee_pct;
                pos.realized_pnl += pos.total_qty * (candle.close - pos.avg_entry_price) - fee;
                pos.traded_qty += pos.total_qty;
                total_closed += pos.total_qty;
                pos.total_qty = 0.0;
                pos.unstuck_levels = static_cast<int>(lvl + 1);
            }
            break;
        }

        double const fee = std::abs(close_qty * candle.close)
                         * strat.strategy.maker_fee_pct;
        pos.realized_pnl += close_qty * (candle.close - pos.avg_entry_price) - fee;
        pos.total_qty -= close_qty;
        pos.traded_qty += close_qty;
        total_closed += close_qty;
        pos.unstuck_levels = static_cast<int>(lvl + 1);

        if (pos.total_qty < 1e-12) {
            pos.total_qty = 0.0;
            break;
        }
    }

    return total_closed > 0.0;
}

} // namespace powermdg
