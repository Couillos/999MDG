#ifndef POWERMDG_STRATEGY_MODULES_ENTRY_CONDITION_EMA_DIST_PCT_H
#define POWERMDG_STRATEGY_MODULES_ENTRY_CONDITION_EMA_DIST_PCT_H

#include "entry_condition.h"

namespace powermdg {

/// Entry condition: enter when close < EMA * (1 - entry_ema_distance_pct).
///
/// This is the dip-buy entry used by PassivBot-style long grid/martingale bots.
/// The position is only opened when the price has dipped below the EMA by a
/// configurable percentage, acting as a mean-reversion filter.
/// Entering on dips (not tops) is essential: a long grid that averages down
/// must start from a low point, otherwise grid levels are filled as the price
/// falls from an already-elevated entry — a classic path to ruin.
///
/// Parameters (read from cfg.strategy):
///   - entry_ema_distance_pct: minimum distance below EMA (e.g. 0.01 = 1% dip)
///
/// Context fields used:
///   - candle.close: current close price
///   - ema: current EMA value
///
/// This module is a PURE FUNCTION: it does NOT mutate pos and does NOT check
/// wallet exposure or n_positions (strategy.cpp handles those).
class EmaDistPctEntryCondition : public IEntryCondition {
public:
    bool should_enter(const ModuleContext& ctx) const override;
    std::string name() const override { return "ema_dist_pct"; }
    DataNeed data_needs() const override { return DataNeed::Ema; }
};

} // namespace powermdg

#endif // POWERMDG_STRATEGY_MODULES_ENTRY_CONDITION_EMA_DIST_PCT_H
