#ifndef POWERMDG_STRATEGY_MODULES_ENTRIES_ALGO_ENTRIES_ALGO_H
#define POWERMDG_STRATEGY_MODULES_ENTRIES_ALGO_ENTRIES_ALGO_H

#include "strategy/modules/module_context.h"
#include <memory>
#include <string>
#include <vector>

namespace powermdg {

/// Interface for entries algorithms.
///
/// An entries algorithm is a PURE DECISION function: it reads the context and
/// returns a list of EntryOrder. It does NOT mutate Position and does NOT
/// execute any order. strategy.cpp executes the returned orders.
///
/// The module is called in two cases:
///   1. First entry (pos.total_qty == 0): returns the initial entry order.
///      strategy.cpp has already checked entry_condition, is_active, n_positions,
///      and wallet exposure before calling.
///   2. Double-down (pos.total_qty > 0): returns zero or more double-down orders.
///      strategy.cpp has already checked wallet exposure before calling.
///
/// The module reads pos.entry_levels to know which level to start at, and
/// computes the qty for each level. strategy.cpp increments pos.entry_levels
/// by the number of orders executed.
///
/// Implementations:
///   - martingale: grid entries with double-down factor
class IEntriesAlgo {
public:
    virtual ~IEntriesAlgo() = default;

    /// Returns a list of entry orders to execute. Empty = no entry.
    /// The module MUST NOT mutate pos — it only reads pos to decide.
    virtual std::vector<EntryOrder> compute_entries(const ModuleContext& ctx) const = 0;

    /// Returns the name of this entries algorithm (e.g. "martingale").
    virtual std::string name() const = 0;
    virtual DataNeed data_needs() const { return DataNeed::None; }
};

/// Factory: creates an entries algorithm by name.
std::unique_ptr<IEntriesAlgo> create_entries_algo(const std::string& name);

} // namespace powermdg

#endif // POWERMDG_STRATEGY_MODULES_ENTRIES_ALGO_ENTRIES_ALGO_H
