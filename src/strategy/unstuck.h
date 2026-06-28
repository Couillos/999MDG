#ifndef POWERMDG_STRATEGY_UNSTUCK_H
#define POWERMDG_STRATEGY_UNSTUCK_H

#include "config/types.h"
#include "data/candle.h"
#include "data/symbol_info.h"
#include "strategy/types.h"

namespace powermdg {

/// Checks time-based unstuck conditions and partially closes the position
/// if it has been held longer than unstuck_age hours. Each "level" closes a
/// fixed exposure tranche = unstuck_pct * initial_balance / current_price.
/// Returns true if any qty was closed.
bool check_time_based_unstuck(const Config& strat, const Candle& candle,
                               Position& pos, size_t current_tick);

} // namespace powermdg

#endif // POWERMDG_STRATEGY_UNSTUCK_H
