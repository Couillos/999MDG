#include "legacy_stop_loss.h"
#include <cmath>
namespace powermdg {
std::vector<CloseOrder> LegacyStopLoss::compute_loss_exits(const ModuleContext& ctx) const {
    std::vector<CloseOrder> orders;
    if (std::abs(ctx.pos.total_qty) < 1e-12) return orders;
    if (ctx.pos.avg_entry_price <= 0.0) return orders;
    double const upnl = (ctx.candle.close - ctx.pos.avg_entry_price) / ctx.pos.avg_entry_price;
    // sl_upnl_pct is negative (e.g. -0.1 = -10%). Exit when upnl <= sl_upnl_pct.
    if (upnl <= ctx.cfg.strategy.sl_upnl_pct) {
        orders.push_back({ctx.pos.total_qty});
    }
    return orders;
}
} // namespace powermdg
