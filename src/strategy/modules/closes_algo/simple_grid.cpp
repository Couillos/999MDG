#include "simple_grid.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace powermdg {
namespace {

double round_step(double val, double step) {
    return std::round(val / step) * step;
}

} // anonymous namespace

std::vector<CloseOrder> SimpleGridClosesAlgo::compute_closes(const ModuleContext& ctx) const {
    std::vector<CloseOrder> orders;

    if (std::abs(ctx.pos.total_qty) < 1e-12) {
        return orders;
    }

    // Track remaining quantity locally — each close reduces it so the next
    // close sees the updated quantity. This matches the original close_grid.cpp
    // behavior where pos.total_qty was mutated in-place during the loop.
    double remaining_qty = ctx.pos.total_qty;

    for (int k = 1; k <= ctx.cfg.strategy.close_grid_count; ++k) {
        if (std::abs(remaining_qty) < 1e-12) {
            break;  // position fully closed, stop iterating
        }

        double const target_price = ctx.pos.avg_entry_price
            * (1.0 + static_cast<double>(k) * ctx.cfg.strategy.close_grid_spacing_pct);

        if (ctx.candle.close < target_price) {
            continue;
        }

        // UPnL condition: upnl >= k * close_grid_spacing_pct
        double const upnl = (ctx.candle.close - ctx.pos.avg_entry_price) / ctx.pos.avg_entry_price;
        if (upnl < static_cast<double>(k) * ctx.cfg.strategy.close_grid_spacing_pct) {
            continue;
        }

        double close_qty;
        if (k == ctx.cfg.strategy.close_grid_count) {
            // Last grid level: sweep the entire remaining qty so no dust is left.
            // round_step can under-close due to rounding, leaving a permanent residual
            // that the engine's 1e-12 guard will never flush.
            close_qty = remaining_qty;
        } else {
            close_qty = round_step(remaining_qty
                / static_cast<double>(ctx.cfg.strategy.close_grid_count), ctx.info.step_size);
            close_qty = std::min(close_qty, remaining_qty);
        }

        if (close_qty > 1e-12) {
            orders.push_back({close_qty});
            remaining_qty -= close_qty;  // decrement local copy
        }
    }

    return orders;
}

} // namespace powermdg
