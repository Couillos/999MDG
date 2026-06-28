#ifndef POWERMDG_STRATEGY_ENTRY_GRID_H
#define POWERMDG_STRATEGY_ENTRY_GRID_H

#include "config/types.h"
#include "data/candle.h"
#include "data/symbol_info.h"
#include "strategy/types.h"

namespace powermdg {

/// Processes entry conditions and opens / doubles-down positions.
void process_entries(const Config& strat, const SymbolInfo& info,
                     const Candle& candle, double total_balance,
                     int& total_positions, Position& pos,
                     bool is_active, double ema,
                     int64_t current_tick);

} // namespace powermdg

#endif // POWERMDG_STRATEGY_ENTRY_GRID_H
