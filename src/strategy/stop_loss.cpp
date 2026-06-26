#include "stop_loss.h"
#include <cmath>
#include <cstdlib>

namespace martingale {

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
    pos.entry_levels = 0;

    return true;
}

} // namespace martingale
