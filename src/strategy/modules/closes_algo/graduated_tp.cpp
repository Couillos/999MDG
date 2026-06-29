#include "graduated_tp.h"
#include "debug_log.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
namespace powermdg {
namespace {

// ── DEBUG counters ──
static std::atomic<size_t> debug_gtp_entry_calls{0};
static std::atomic<size_t> debug_gtp_atr_calls{0};
static std::atomic<double> debug_gtp_atr_time_ms{0};

static void log_gtp_stats() {
    static thread_local auto last_log = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - last_log).count() < 5.0) return;
    last_log = now;
    DEBUG_LOG(
        "[DEBUG] [graduated_tp] entry=%zu atr=%zu | atr_time=%.0fms\n",
        debug_gtp_entry_calls.load(), debug_gtp_atr_calls.load(),
        debug_gtp_atr_time_ms.load());
}

double compute_atr(std::span<const Candle> cs, size_t end, int period) {
    debug_gtp_atr_calls.fetch_add(1);
    auto t0 = std::chrono::steady_clock::now();
    if(cs.empty()||end<1) return 0.0;
    size_t st=(end>static_cast<size_t>(period))?end-period:0;
    double sum=0;size_t n=0;
    for(size_t i=st;i<=end&&i<cs.size();++i){if(i==0)continue;
        double tr=std::max({cs[i].high-cs[i].low,std::abs(cs[i].high-cs[i-1].close),std::abs(cs[i].low-cs[i-1].close)});
        sum+=tr;++n;}
    auto t1 = std::chrono::steady_clock::now();
    debug_gtp_atr_time_ms.fetch_add(std::chrono::duration<double, std::milli>(t1 - t0).count());
    return n>0?sum/n:0.0;
}
} // anonymous namespace

std::vector<CloseOrder> GraduatedTpClosesAlgo::compute_closes(const ModuleContext& ctx) const {
    debug_gtp_entry_calls.fetch_add(1);
    log_gtp_stats();
    std::vector<CloseOrder> orders;
    if (std::abs(ctx.pos.total_qty) < 1e-12) return orders;
    if (ctx.candle_series.empty()) return orders;
    double const vwap = ctx.vwap;
    double const stdev = ctx.stdev;
    if (vwap<=0.0 || stdev<=0.0) return orders;
    double const z = std::abs((ctx.candle.close - vwap) / stdev);
    double remaining = ctx.pos.total_qty;
    // TP1: |Z| <= tp1_z_threshold → close tp1_frac
    if (!ctx.pos.tp1_fired && z <= ctx.cfg.strategy.tp1_z_threshold) {
        double q = remaining * ctx.cfg.strategy.tp1_frac;
        if (q > 1e-12) { orders.push_back({q}); remaining -= q; }
        // Note: tp1_fired is set by strategy.cpp after execution
    }
    // TP2: |Z| <= tp2_z_threshold → close tp2_frac of remaining
    if (ctx.pos.tp1_fired && z <= ctx.cfg.strategy.tp2_z_threshold && remaining > 1e-12) {
        double q = remaining * ctx.cfg.strategy.tp2_frac;
        if (q > 1e-12) { orders.push_back({q}); remaining -= q; }
    }
    // TP3: trailing ATR stop on remaining
    // Trigger: close is still sufficiently close to VWAP (|z| <= tp2_z_threshold),
    // OR close has reverted so far that it's within trailing_atr_mult * ATR of VWAP.
    // If ATR data is unavailable, fall back to closing at the TP2/trailing threshold
    // (|z| <= tp2_z_threshold) rather than leaving the residual forever open.
    if (ctx.pos.tp1_fired && remaining > 1e-12) {
        bool should_close_tp3 = false;

        // Try to get ATR data for trailing stop
        auto it = ctx.cfg.strategy.indicator_timeframes.find("atr_period");
        if (it != ctx.cfg.strategy.indicator_timeframes.end()) {
            auto tf_it = ctx.tf_data.find(it->second);
            if (tf_it != ctx.tf_data.end()) {
                double const atr = compute_atr(tf_it->second.candles, tf_it->second.current_idx, ctx.cfg.strategy.atr_period);
                if (atr > 0.0) {
                    // ATR-based trailing: close remaining if price is within
                    // trailing_atr_mult * ATR of VWAP (mean-reversion confirmed)
                    double const trail_dist = ctx.cfg.strategy.trailing_atr_mult * atr;
                    should_close_tp3 = std::abs(ctx.candle.close - vwap) <= trail_dist;
                } else {
                    // ATR computed as 0 (insufficient data) → fall back to z-score threshold
                    should_close_tp3 = (z <= ctx.cfg.strategy.tp2_z_threshold);
                }
            } else {
                // Timeframe not in tf_data → fall back to z-score threshold
                should_close_tp3 = (z <= ctx.cfg.strategy.tp2_z_threshold);
            }
        } else {
            // No atr_period timeframe configured → fall back to z-score threshold
            should_close_tp3 = (z <= ctx.cfg.strategy.tp2_z_threshold);
        }

        if (should_close_tp3 && remaining > 1e-12) {
            orders.push_back({remaining});
            remaining = 0.0;
        }
    }
    return orders;
}
} // namespace powermdg
