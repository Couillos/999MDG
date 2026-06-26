#ifndef MARTINGALE_STRATEGY_CLOSE_GRID_H
#define MARTINGALE_STRATEGY_CLOSE_GRID_H

#include "config/types.h"
#include "data/candle.h"
#include "data/symbol_info.h"
#include "strategy/types.h"

namespace martingale {

/// Processes take-profit grid closes for an open position.
void process_closes(const Config& strat, const SymbolInfo& info,
                    const Candle& candle, Position& pos,
                    int& total_positions);

} // namespace martingale

#endif // MARTINGALE_STRATEGY_CLOSE_GRID_H
