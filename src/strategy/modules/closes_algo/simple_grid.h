#ifndef POWERMDG_STRATEGY_MODULES_CLOSES_ALGO_SIMPLE_GRID_H
#define POWERMDG_STRATEGY_MODULES_CLOSES_ALGO_SIMPLE_GRID_H

#include "closes_algo.h"

namespace powermdg {

/// Simple grid closes algorithm: closes a fixed fraction of the position at
/// each grid level when price rises by close_grid_spacing_pct from avg_entry_price.
///
/// Behavior:
///   For k = 1..close_grid_count:
///     target_price = avg_entry_price × (1 + k × close_grid_spacing_pct)
///     if close >= target_price AND upnl >= k × close_grid_spacing_pct:
///       close_qty = total_qty / close_grid_count (rounded to step_size)
///       close_qty = min(close_qty, total_qty)
///       add {close_qty} to orders
///
/// Parameters (read from cfg.strategy):
///   - close_grid_spacing_pct: price rise between close levels
///   - close_grid_count: number of close levels (each closes 1/count of qty)
///
/// This module is a PURE FUNCTION: it does NOT mutate pos.
/// strategy.cpp executes the returned orders, detects full close, and resets
/// position state.
class SimpleGridClosesAlgo : public IClosesAlgo {
public:
    std::vector<CloseOrder> compute_closes(const ModuleContext& ctx) const override;
    std::string name() const override { return "simple_grid"; }
};

} // namespace powermdg

#endif // POWERMDG_STRATEGY_MODULES_CLOSES_ALGO_SIMPLE_GRID_H
