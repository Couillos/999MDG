#include "z_stop.h"
#include <cmath>
namespace powermdg {
namespace {
/// Compute VWAP over last N candles from candle_series.
double compute_vwap(std::span<const Candle> candles, size_t end_idx, size_t lookback) {
    double pv = 0.0, vv = 0.0;
    size_t start = (end_idx > lookback) ? end_idx - lookback : 0;
    for (size_t i = start; i <= end_idx && i < candles.size(); ++i) {
        double const tp = (candles[i].high + candles[i].low + candles[i].close) / 3.0;
        pv += tp * candles[i].volume;
        vv += candles[i].volume;
    }
    return vv > 0.0 ? pv / vv : 0.0;
}
/// Compute rolling stdev of close over last N candles.
double compute_stdev(std::span<const Candle> candles, size_t end_idx, size_t lookback) {
    if (end_idx < 2) return 0.0;
    size_t start = (end_idx > lookback) ? end_idx - lookback : 0;
    size_t n = 0;
    double sum = 0.0, sq = 0.0;
    for (size_t i = start; i <= end_idx && i < candles.size(); ++i) {
        sum += candles[i].close;
        sq += candles[i].close * candles[i].close;
        ++n;
    }
    if (n < 2) return 0.0;
    double const mean = sum / n;
    double const var = sq / n - mean * mean;
    return std::sqrt(std::max(0.0, var));
}
} // anonymous namespace

std::vector<CloseOrder> ZStop::compute_loss_exits(const ModuleContext& ctx) const {
    std::vector<CloseOrder> orders;
    if (std::abs(ctx.pos.total_qty) < 1e-12) return orders;
    if (ctx.candle_series.empty()) return orders;
    int const lookback = ctx.cfg.strategy.zscore_vwap_lookback;
    if (lookback < 2) return orders;
    double const vwap = compute_vwap(ctx.candle_series, ctx.candle_series_idx, static_cast<size_t>(lookback));
    double const stdev = compute_stdev(ctx.candle_series, ctx.candle_series_idx, static_cast<size_t>(lookback));
    if (vwap <= 0.0 || stdev <= 0.0) return orders;
    double const z = (ctx.candle.close - vwap) / stdev;
    // Exit when |Z| exceeds threshold (OU broken, regime trend)
    if (std::abs(z) > ctx.cfg.strategy.z_stop_threshold) {
        orders.push_back({ctx.pos.total_qty});
    }
    return orders;
}
} // namespace powermdg
