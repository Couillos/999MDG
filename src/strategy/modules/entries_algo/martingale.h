#ifndef POWERMDG_STRATEGY_MODULES_ENTRIES_ALGO_MARTINGALE_H
#define POWERMDG_STRATEGY_MODULES_ENTRIES_ALGO_MARTINGALE_H

#include "entries_algo.h"

namespace powermdg {

/// Martingale entries algorithm: grid entries with exponential double-down.
///
/// Behavior:
///   - First entry (pos.total_qty == 0):
///       qty = slot_capital × initial_qty_pct / price
///     where slot_capital = total_balance × total_wallet_exposure / n_positions
///     Returns [{qty}] if qty >= min_qty, else [].
///
///   - Double-down (pos.total_qty > 0):
///       price_drop_pct = (avg_entry_price - close) / avg_entry_price
///       levels_filled = floor(price_drop_pct / entry_grid_spacing_pct)
///     For each level from pos.entry_levels to levels_filled:
///       mult = double_down_factor^level
///       qty = slot_capital × initial_qty_pct × mult / price
///       if qty >= min_qty: add {qty} to orders, else break.
///     Returns the list of orders.
///
/// Parameters (read from cfg.strategy):
///   - initial_qty_pct: fraction of slot capital for the first entry
///   - entry_grid_spacing_pct: price drop between double-down levels
///   - double_down_factor: multiplier per level (1.0 = constant, 0.5 = halving)
///   - n_positions: for slot capital computation
///
/// This module is a PURE FUNCTION: it does NOT mutate pos.
/// strategy.cpp executes the returned orders and updates pos.entry_levels.
class MartingaleEntriesAlgo : public IEntriesAlgo {
public:
    std::vector<EntryOrder> compute_entries(const ModuleContext& ctx) const override;
    std::string name() const override { return "martingale"; }
};

} // namespace powermdg

#endif // POWERMDG_STRATEGY_MODULES_ENTRIES_ALGO_MARTINGALE_H
