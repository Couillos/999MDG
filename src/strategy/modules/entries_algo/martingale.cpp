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
        // Double-down logic
        double const price_drop_pct = (ctx.pos.avg_entry_price - ctx.candle.close)
                                    / ctx.pos.avg_entry_price;
        if (price_drop_pct <= 0.0) {
            return orders;
        }

        int const levels_filled = static_cast<int>(
            std::floor(price_drop_pct / ctx.cfg.strategy.entry_grid_spacing_pct));

        if (levels_filled < ctx.pos.entry_levels) {
            return orders;
        }

        for (int lvl = ctx.pos.entry_levels; lvl <= levels_filled; ++lvl) {
            double const mult = std::pow(ctx.cfg.strategy.double_down_factor,
                                         static_cast<double>(lvl));
            double const entry_size_usd = slot_capital
                                        * ctx.cfg.strategy.initial_qty_pct * mult;
            double const qty = round_step(entry_size_usd / ctx.candle.close,
                                          ctx.info.step_size);

            if (qty < ctx.info.min_qty) {
                break;
            }
            orders.push_back({qty});
        }
    }

    return orders;
}

} // namespace powermdg
