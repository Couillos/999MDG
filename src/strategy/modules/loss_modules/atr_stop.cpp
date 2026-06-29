#include "atr_stop.h"
#include <algorithm>
#include <cmath>
namespace powermdg {
namespace {
/// Compute ATR(14) from HTF candles.
double compute_atr(std::span<const Candle> candles, size_t end_idx, int period) {
    if (candles.empty() || end_idx < 1) return 0.0;
    size_t start = (end_idx > static_cast<size_t>(period)) ? end_idx - period : 0;
    double sum_tr = 0.0;
    size_t n = 0;
    for (size_t i = start; i <= end_idx && i < candles.size(); ++i) {
        if (i == 0) continue;
        double const tr = std::max({
            candles[i].high - candles[i].low,
            std::abs(candles[i].high - candles[i-1].close),
            std::abs(candles[i].low - candles[i-1].close)
        });
        sum_tr += tr;
        ++n;
    }
    return n > 0 ? sum_tr / n : 0.0;
}
} // anonymous namespace

std::vector<CloseOrder> AtrStop::compute_loss_exits(const ModuleContext& ctx) const {
    std::vector<CloseOrder> orders;
    if (std::abs(ctx.pos.total_qty) < 1e-12) return orders;
    if (ctx.pos.avg_entry_price <= 0.0) return orders;
    if (ctx.tf_data.empty()) return orders;
    // Find the timeframe for atr_period
    auto it = ctx.cfg.strategy.indicator_timeframes.find("atr_period");
    if (it == ctx.cfg.strategy.indicator_timeframes.end()) return orders;
    auto tf_it = ctx.tf_data.find(it->second);
    if (tf_it == ctx.tf_data.end()) return orders;
    auto const& htf = tf_it->second;
    double const atr = compute_atr(htf.candles, htf.current_idx, ctx.cfg.strategy.atr_period);
    if (atr <= 0.0) return orders;
    double const stop_distance = ctx.cfg.strategy.atr_stop_mult * atr;
    // For long: exit if close < entry - stop_distance
    // For short: exit if close > entry + stop_distance
    double const adverse = (ctx.pos.entry_side >= 0)
        ? ctx.pos.avg_entry_price - ctx.candle.close
        : ctx.candle.close - ctx.pos.avg_entry_price;
    if (adverse >= stop_distance) {
        orders.push_back({ctx.pos.total_qty});
    }
    return orders;
}
} // namespace powermdg
