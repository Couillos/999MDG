#ifndef POWERMDG_STRATEGY_CLOSE_GRID_H
#define POWERMDG_STRATEGY_CLOSE_GRID_H

#include "config/types.h"
#include "data/candle.h"
#include "data/symbol_info.h"
#include "strategy/types.h"

namespace powermdg {

/// Processes take-profit grid closes for an open position.
void process_closes(const Config& strat, const SymbolInfo& info,
                    const Candle& candle, Position& pos,
                    int& total_positions);

} // namespace powermdg

#endif // POWERMDG_STRATEGY_CLOSE_GRID_H
