#ifndef POWERMDG_STRATEGY_MODULES_ENTRY_CONDITION_ENTRY_CONDITION_H
#define POWERMDG_STRATEGY_MODULES_ENTRY_CONDITION_ENTRY_CONDITION_H

#include "strategy/modules/module_context.h"
#include <memory>
#include <string>

namespace powermdg {

/// Interface for entry conditions.
///
/// An entry condition is a PURE DECISION function: it reads the context and
/// returns true/false. It does NOT mutate Position and does NOT execute any
/// order. strategy.cpp calls should_enter() to decide whether to ask the
/// entries_algo for a first-entry order.
///
/// The entry condition is only consulted for the FIRST entry (when no position
/// is open). Double-down entries are decided solely by the entries_algo.
///
/// Implementations:
///   - ema_dist_pct: enter when close > EMA * (1 + entry_ema_distance_pct)
class IEntryCondition {
public:
    virtual ~IEntryCondition() = default;

    /// Returns true if a new position should be opened for this candle.
    /// Called only when pos.total_qty == 0 (no existing position).
    /// strategy.cpp also checks is_active, n_positions, and wallet exposure
    /// before calling this — so the module only needs to check its own signal.
    virtual bool should_enter(const ModuleContext& ctx) const = 0;

    /// Returns the name of this entry condition (e.g. "ema_dist_pct").
    virtual std::string name() const = 0;
};

/// Factory: creates an entry condition by name.
/// Returns nullptr if the name is unknown.
std::unique_ptr<IEntryCondition> create_entry_condition(const std::string& name);

} // namespace powermdg

#endif // POWERMDG_STRATEGY_MODULES_ENTRY_CONDITION_ENTRY_CONDITION_H
