#include "ema_dist_pct.h"

namespace powermdg {

bool EmaDistPctEntryCondition::should_enter(const ModuleContext& ctx) const {
    // Entry condition: close must be above EMA threshold.
    // strategy.cpp already checked is_active, n_positions, and wallet exposure
    // before calling this function — so we only check the EMA signal.
    double const threshold = ctx.ema * (1.0 + ctx.cfg.strategy.entry_ema_distance_pct);
    return ctx.candle.close > threshold;
}

} // namespace powermdg
