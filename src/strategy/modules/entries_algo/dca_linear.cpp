#include "dca_linear.h"
#include <cmath>
#include <cstdlib>
namespace powermdg {
namespace { double round_step(double v, double s) { return std::round(v/s)*s; } }
std::vector<EntryOrder> DcaLinearEntriesAlgo::compute_entries(const ModuleContext& ctx) const {
    std::vector<EntryOrder> orders;
    double const slot = (ctx.total_balance * ctx.cfg.total_wallet_exposure)
                       / static_cast<double>(ctx.cfg.strategy.n_positions);
    double const base = slot * ctx.cfg.strategy.initial_qty_pct;
    if (std::abs(ctx.pos.total_qty) < 1e-12) {
        double const q = round_step(base / ctx.candle.close, ctx.info.step_size);
        if (q >= ctx.info.min_qty) orders.push_back({q});
    } else {
        double const drop = (ctx.pos.avg_entry_price - ctx.candle.close) / ctx.pos.avg_entry_price;
        if (drop <= 0.0) return orders;
        int const filled = static_cast<int>(std::floor(drop / ctx.cfg.strategy.entry_grid_spacing_pct));
        if (filled < ctx.pos.entry_levels) return orders;
        for (int lvl = ctx.pos.entry_levels; lvl <= filled; ++lvl) {
            double const mult = 1.0 + ctx.cfg.strategy.linear_step * static_cast<double>(lvl);
            double const q = round_step(base * mult / ctx.candle.close, ctx.info.step_size);
            if (q < ctx.info.min_qty) break;
            orders.push_back({q});
        }
    }
    return orders;
}
} // namespace powermdg
