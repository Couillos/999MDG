#include "mean_revert_tp.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
namespace powermdg {
namespace { double round_step(double v, double s) { return std::round(v/s)*s; } }
std::vector<CloseOrder> MeanRevertTpClosesAlgo::compute_closes(const ModuleContext& ctx) const {
    std::vector<CloseOrder> orders;
    if (std::abs(ctx.pos.total_qty) < 1e-12) return orders;
    if (ctx.pos.avg_entry_price <= 0.0 || ctx.ema <= 0.0) return orders;
    double const upnl = (ctx.candle.close - ctx.pos.avg_entry_price) / ctx.pos.avg_entry_price;
    if (upnl < ctx.cfg.strategy.tp_min_upnl_pct) return orders;
    double remaining = ctx.pos.total_qty;
    double const overshoot = ctx.ema * (1.0 + ctx.cfg.strategy.overshoot_pct);
    if (ctx.candle.close >= overshoot) {
        if (remaining > 1e-12) { orders.push_back({remaining}); remaining = 0; }
    }
    if (remaining > 1e-12 && ctx.candle.close >= ctx.ema) {
        double q = round_step(remaining * ctx.cfg.strategy.revert_close_frac, ctx.info.step_size);
        q = std::min(q, remaining);
        if (q > 1e-12) orders.push_back({q});
    }
    return orders;
}
} // namespace powermdg
