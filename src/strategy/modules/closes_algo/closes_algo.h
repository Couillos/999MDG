#ifndef POWERMDG_STRATEGY_MODULES_CLOSES_ALGO_CLOSES_ALGO_H
#define POWERMDG_STRATEGY_MODULES_CLOSES_ALGO_CLOSES_ALGO_H

#include "strategy/modules/module_context.h"
#include <memory>
#include <string>
#include <vector>

namespace powermdg {

/// Interface for closes algorithms.
///
/// A closes algorithm is a PURE DECISION function: it reads the context and
/// returns a list of CloseOrder. It does NOT mutate Position and does NOT
/// execute any order. strategy.cpp executes the returned orders.
///
/// The module is called when pos.total_qty > 0 (an existing position).
/// strategy.cpp handles the full-close detection (total_qty → 0) and the
/// position state reset (entry_levels, unstuck_levels, entry_tick).
///
/// Implementations:
///   - simple_grid: closes 1/close_grid_count of the position at each grid level
class IClosesAlgo {
public:
    virtual ~IClosesAlgo() = default;

    /// Returns a list of close orders to execute. Empty = no close.
    /// The module MUST NOT mutate pos — it only reads pos to decide.
    virtual std::vector<CloseOrder> compute_closes(const ModuleContext& ctx) const = 0;

    /// Returns the name of this closes algorithm (e.g. "simple_grid").
    virtual std::string name() const = 0;
    virtual DataNeed data_needs() const { return DataNeed::None; }
};

/// Factory: creates a closes algorithm by name.
std::unique_ptr<IClosesAlgo> create_closes_algo(const std::string& name);

} // namespace powermdg

#endif // POWERMDG_STRATEGY_MODULES_CLOSES_ALGO_CLOSES_ALGO_H
