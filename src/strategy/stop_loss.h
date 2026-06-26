#ifndef MARTINGALE_STRATEGY_STOP_LOSS_H
#define MARTINGALE_STRATEGY_STOP_LOSS_H

#include "config/types.h"
#include "data/candle.h"
#include "strategy/types.h"

namespace martingale {

/// Checks and executes stop-loss. Returns true if the position was fully closed.
bool check_stop_loss(const Config& strat, const Candle& candle, Position& pos);

} // namespace martingale

#endif // MARTINGALE_STRATEGY_STOP_LOSS_H
