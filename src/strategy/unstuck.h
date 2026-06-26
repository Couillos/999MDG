#ifndef MARTINGALE_STRATEGY_UNSTUCK_H
#define MARTINGALE_STRATEGY_UNSTUCK_H

#include "config/types.h"
#include "data/candle.h"
#include "data/symbol_info.h"
#include "strategy/types.h"

namespace martingale {

/// Checks time-based unstuck conditions and partially closes the position
/// if the position has been held longer than unstuck_age and UPnL exceeds
/// the threshold. Closes unstuck_pct fraction of the current qty.
/// Returns true if any qty was closed.
bool check_time_based_unstuck(const Config& strat, const Candle& candle,
                               Position& pos, size_t current_tick);

} // namespace martingale

#endif // MARTINGALE_STRATEGY_UNSTUCK_H
