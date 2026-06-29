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

    // Pre-calculate the full close ladder using recursive compounding,
    // mirroring the martingale entry pattern.
    double const spacing = ctx.cfg.strategy.close_grid_spacing_pct;
    int const count = ctx.cfg.strategy.close_grid_count;
    double const close = ctx.candle.close;

    struct Level { double trigger; double qty; };
    std::vector<Level> ladder;

    double const avg_entry = ctx.pos.avg_entry_price;
    double sim_qty = ctx.pos.total_qty;
    double prev_trigger = avg_entry;

    for (int k = 1; k <= count; ++k) {
        double const trigger = prev_trigger * (1.0 + spacing);
        double raw_qty;
        if (k == count) {
            raw_qty = sim_qty;
        } else {
            raw_qty = sim_qty * (1.0 / static_cast<double>(count));
        }
        double qty = round_step(raw_qty, ctx.info.step_size);
        qty = std::min(qty, sim_qty);
        if (qty < ctx.info.min_qty) break;
        ladder.push_back({trigger, qty});
        sim_qty -= qty;
        prev_trigger = trigger;
    }

    for (auto const& level : ladder) {
        if (close >= level.trigger) {
            orders.push_back({level.qty});
        } else {
            break;
        }
    }

    return orders;
}

} // namespace powermdg
