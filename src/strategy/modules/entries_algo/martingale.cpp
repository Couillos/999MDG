#include "martingale.h"
#include <cmath>
#include <cstdlib>

namespace powermdg {
namespace {

/// Rounds a value to the nearest step.
double round_step(double val, double step) {
    return std::round(val / step) * step;
}

} // anonymous namespace

std::vector<EntryOrder> MartingaleEntriesAlgo::compute_entries(const ModuleContext& ctx) const {
    std::vector<EntryOrder> orders;

    double const slot_capital = (ctx.total_balance * ctx.cfg.total_wallet_exposure)
                              / static_cast<double>(ctx.cfg.strategy.n_positions);

    if (std::abs(ctx.pos.total_qty) < 1e-12) {
        // First entry
        double const entry_size_usd = slot_capital * ctx.cfg.strategy.initial_qty_pct;
        double const qty = round_step(entry_size_usd / ctx.candle.close, ctx.info.step_size);

        if (qty >= ctx.info.min_qty) {
            orders.push_back({qty});
        }
    } else {
        // Double-down logic: pre-calculate the full martingale ladder
        // using recursive theoretical position averaging.
        double const spacing = ctx.cfg.strategy.entry_grid_spacing_pct;
        double const factor = ctx.cfg.strategy.double_down_factor;
        double const close = ctx.candle.close;

        struct Level { double trigger; double qty; };
        std::vector<Level> ladder;

        double sim_avg = ctx.pos.avg_entry_price;
        double sim_qty = ctx.pos.total_qty;

        for (int i = 0; i < 100; ++i) {
            double const trigger = sim_avg * (1.0 - spacing);
            double const raw_qty = sim_qty * factor;
            double const qty = round_step(raw_qty, ctx.info.step_size);
            if (qty < ctx.info.min_qty) break;
            ladder.push_back({trigger, qty});

            sim_qty += qty;
            sim_avg = (sim_avg * (sim_qty - qty) + trigger * qty) / sim_qty;
        }

        int const start = (std::max)(ctx.pos.entry_levels - 1, 0);
        for (int i = start; i < static_cast<int>(ladder.size()); ++i) {
            if (close <= ladder[i].trigger) {
                orders.push_back({ladder[i].qty});
            } else {
                break;
            }
        }
    }

    return orders;
}

} // namespace powermdg
