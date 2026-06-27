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
    double const threshold_factor = strat.strategy.time_based_unstuck_threshold;
    int const age = strat.strategy.time_based_unstuck_age;

    if (pct <= 0.0 || age <= 0 || pos.entry_tick == 0) {
        return false;
    }

    int64_t const held = static_cast<int64_t>(current_tick) - pos.entry_tick;
    if (held < age) {
        return false;
    }

    // Wallet exposure = position notional / balance (like PassivBot's calc_wallet_exposure)
    double const wallet_exposure = std::abs(pos.total_qty * candle.close)
                                 / strat.initial_balance_usd;
    // Per-position wallet exposure limit (like PassivBot's wel per coin/side)
    double const n_pos = static_cast<double>(std::max(strat.strategy.n_positions, 1));
    double const wel = strat.total_wallet_exposure / n_pos;
    // Skip if wallet_exposure < wel * threshold_factor (like PassivBot's threshold check)
    if (threshold_factor > 0.0 && wallet_exposure < wel * threshold_factor) {
        return false;
    }

    int const expected = static_cast<int>(held / age);
    if (expected <= pos.unstuck_levels) {
        return false;
    }

    // Each level closes a fixed exposure tranche = pct of initial balance.
    // close_qty = (pct * initial_balance) / current_price
    // This is independent of remaining qty. If the tranche exceeds the
    // remaining position, the whole position is closed.
    double const exposure_tranche = pct * strat.initial_balance_usd;

    double total_closed = 0.0;
    for (int lvl = pos.unstuck_levels; lvl < expected; ++lvl) {
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
                pos.unstuck_levels = lvl + 1;
            }
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
            pos.total_qty = 0.0;
            break;
        }
    }

    return total_closed > 0.0;
}

} // namespace martingale
