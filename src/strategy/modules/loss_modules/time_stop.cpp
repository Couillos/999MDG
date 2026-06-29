#include "time_stop.h"
#include <cmath>
namespace powermdg {
std::vector<CloseOrder> TimeStop::compute_loss_exits(const ModuleContext& ctx) const {
    std::vector<CloseOrder> orders;
    if (std::abs(ctx.pos.total_qty) < 1e-12) return orders;
    if (ctx.pos.entry_timestamp_ms == 0) return orders;
    // Only trigger if TP1 hasn't fired yet
    if (ctx.pos.tp1_fired) return orders;
    double const max_hours = ctx.cfg.strategy.time_stop_hours;
    if (max_hours <= 0.0) return orders;
    int64_t const held_ms = ctx.candle.timestamp - ctx.pos.entry_timestamp_ms;
    double const held_hours = static_cast<double>(held_ms) / 3600000.0;
    if (held_hours >= max_hours) {
        orders.push_back({ctx.pos.total_qty});
    }
    return orders;
}
} // namespace powermdg
