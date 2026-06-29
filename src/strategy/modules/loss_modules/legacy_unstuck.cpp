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
    // Tranche proportional to current position notional relative to current balance.
    // Using initial_balance_usd (a fixed constant) means the tranche never shrinks
    // as the account decays — causing the module to dump the entire position at once
    // instead of de-risking progressively. Scaling by total_balance (current equity)
    // makes the tranche proportional to account size: as balance shrinks, so does
    // the tranche, preventing a catastrophic single-candle full dump.
    double const ref_notional = (ctx.total_balance > 1e-12)
        ? ctx.total_balance
        : ctx.pos.total_qty * ctx.candle.close;
    double const tranche = pct * ref_notional;
    double const close_qty = std::min(tranche / ctx.candle.close, ctx.pos.total_qty);
    if (close_qty > 1e-12) {
        orders.push_back({close_qty});
    }
    return orders;
}
} // namespace powermdg
