#ifndef POWERMDG_STRATEGY_MODULES_MODULE_CONTEXT_H
#define POWERMDG_STRATEGY_MODULES_MODULE_CONTEXT_H

#include "config/types.h"
#include "data/candle.h"
#include "data/symbol_info.h"
#include "strategy/types.h"
#include <cstdint>

namespace powermdg {

/// An entry order returned by an entries_algo module.
/// strategy.cpp is responsible for executing it (mutating Position, applying
/// fees, updating entry_levels, etc.).
struct EntryOrder {
    double qty;  // quantity to enter (>0)
};

/// A close order returned by a closes_algo module.
/// strategy.cpp is responsible for executing it (mutating Position, computing
/// realized PnL, applying fees, etc.).
struct CloseOrder {
    double qty;  // quantity to close (>0)
};

/// Shared context passed to every strategy module (entry_condition, entries_algo,
/// closes_algo). Contains everything a module might need to make a decision.
///
/// IMPORTANT: `pos` is a CONST reference — modules MUST NOT mutate it.
/// Modules are pure decision functions: they read the context and return
/// orders. strategy.cpp executes the orders.
///
/// Fields:
///   - cfg: full config (for total_wallet_exposure, n_positions, maker_fee_pct, etc.)
///   - info: symbol info (step_size, min_qty, min_notional)
///   - candle: current candle
///   - pos: current position state (READ-ONLY)
///   - total_balance: current account balance (realized PnL included)
///   - is_active: whether this symbol is in the active set (top n_positions by vol)
///   - current_tick: index of the current candle in the series
///   - ema: current EMA value for this symbol (only used by entry_condition)
struct ModuleContext {
    const Config& cfg;
    const SymbolInfo& info;
    const Candle& candle;
    const Position& pos;
    double total_balance;
    bool is_active;
    int64_t current_tick;
    double ema;
};

} // namespace powermdg

#endif // POWERMDG_STRATEGY_MODULES_MODULE_CONTEXT_H
