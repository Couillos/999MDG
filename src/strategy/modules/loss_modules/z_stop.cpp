#include "z_stop.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
namespace powermdg {
namespace {

// ── DEBUG counters ──
static std::atomic<size_t> debug_zstop_calls{0};

static void log_zstop_stats() {
    static thread_local auto last_log = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - last_log).count() < 5.0) return;
    last_log = now;
    std::fprintf(stderr,
        "[DEBUG] [z_stop] entry=%zu\n",
        debug_zstop_calls.load());
}
} // anonymous namespace

std::vector<CloseOrder> ZStop::compute_loss_exits(const ModuleContext& ctx) const {
    debug_zstop_calls.fetch_add(1);
    log_zstop_stats();
    std::vector<CloseOrder> orders;
    if (std::abs(ctx.pos.total_qty) < 1e-12) return orders;
    if (ctx.candle_series.empty()) return orders;
    double const vwap = ctx.vwap;
    double const stdev = ctx.stdev;
    if (vwap <= 0.0 || stdev <= 0.0) return orders;
    double const z = (ctx.candle.close - vwap) / stdev;
    // Exit when |Z| exceeds threshold (OU broken, regime trend)
    if (std::abs(z) > ctx.cfg.strategy.z_stop_threshold) {
        orders.push_back({ctx.pos.total_qty});
    }
    return orders;
}
} // namespace powermdg
