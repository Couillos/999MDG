#include "ema_dist_pct.h"

namespace powermdg {

bool EmaDistPctEntryCondition::should_enter(const ModuleContext& ctx) const {
    // Entry condition: close must be BELOW EMA threshold (dip-buy for long grid).
    // A long martingale/grid must enter on a dip, not at a top: buying above the
    // EMA creates a guaranteed path to ruin because the grid then averages down
    // from an already-elevated price.
    // strategy.cpp already checked is_active, n_positions, and wallet exposure
    // before calling this function — so we only check the EMA signal.
    double const threshold = ctx.ema * (1.0 - ctx.cfg.strategy.entry_ema_distance_pct);
    return ctx.candle.close < threshold;
}

} // namespace powermdg
