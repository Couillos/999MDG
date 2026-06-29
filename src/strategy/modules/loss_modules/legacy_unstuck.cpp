#include "legacy_unstuck.h"
#include <cmath>
namespace powermdg {
std::vector<CloseOrder> LegacyUnstuck::compute_loss_exits(const ModuleContext& ctx) const {
    std::vector<CloseOrder> orders;
    if (std::abs(ctx.pos.total_qty) < 1e-12) return orders;
    if (ctx.pos.entry_timestamp_ms == 0) return orders;
    double const pct = ctx.cfg.strategy.time_based_unstuck_pct;
    int const age = ctx.cfg.strategy.time_based_unstuck_age;
    if (pct <= 0.0 || age <= 0) return orders;
    int64_t const held_ms = ctx.candle.timestamp - ctx.pos.entry_timestamp_ms;
    int64_t const age_ms = static_cast<int64_t>(age) * 3600000LL;
    if (held_ms < age_ms) return orders;
    int64_t const expected = held_ms / age_ms;
    if (expected <= static_cast<int64_t>(ctx.pos.unstuck_levels)) return orders;
    double const tranche = pct * ctx.cfg.initial_balance_usd;
    double const close_qty = std::min(tranche / ctx.candle.close, ctx.pos.total_qty);
    if (close_qty > 1e-12) {
        orders.push_back({close_qty});
    }
    return orders;
}
} // namespace powermdg
