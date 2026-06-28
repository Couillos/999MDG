#include "bb_reversion.h"
#include <cmath>
namespace powermdg {
bool BbReversionEntryCondition::should_enter(const ModuleContext& ctx) const {
    if (ctx.ema <= 0.0 || ctx.rolling_stdev <= 0.0) return false;
    double const lower = ctx.ema - ctx.cfg.strategy.bb_std_mult * ctx.rolling_stdev;
    double const upper = ctx.ema + ctx.cfg.strategy.bb_std_mult * ctx.rolling_stdev;
    double const bw = (upper - lower) / ctx.ema;
    if (bw < ctx.cfg.strategy.bb_min_bandwidth_pct) return false;
    return ctx.candle.close <= lower;
}
} // namespace powermdg
