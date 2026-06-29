#include "graduated_tp.h"
#include <algorithm>
#include <cmath>
namespace powermdg {
namespace {
double compute_vwap(std::span<const Candle> cs, size_t end, size_t lb) {
    double pv=0,vv=0;
    size_t st=(end>lb)?end-lb:0;
    for (size_t i=st;i<=end&&i<cs.size();++i){double tp=(cs[i].high+cs[i].low+cs[i].close)/3.0;pv+=tp*cs[i].volume;vv+=cs[i].volume;}
    return vv>0?pv/vv:0.0;
}
double compute_stdev(std::span<const Candle> cs, size_t end, size_t lb) {
    if(end<2) return 0.0;
    size_t st=(end>lb)?end-lb:0;
    size_t n=0;double s=0,sq=0;
    for(size_t i=st;i<=end&&i<cs.size();++i){s+=cs[i].close;sq+=cs[i].close*cs[i].close;++n;}
    if(n<2) return 0.0;
    double m=s/n;return std::sqrt(std::max(0.0,sq/n-m*m));
}
double compute_atr(std::span<const Candle> cs, size_t end, int period) {
    if(cs.empty()||end<1) return 0.0;
    size_t st=(end>static_cast<size_t>(period))?end-period:0;
    double sum=0;size_t n=0;
    for(size_t i=st;i<=end&&i<cs.size();++i){if(i==0)continue;
        double tr=std::max({cs[i].high-cs[i].low,std::abs(cs[i].high-cs[i-1].close),std::abs(cs[i].low-cs[i-1].close)});
        sum+=tr;++n;}
    return n>0?sum/n:0.0;
}
} // anonymous namespace

std::vector<CloseOrder> GraduatedTpClosesAlgo::compute_closes(const ModuleContext& ctx) const {
    std::vector<CloseOrder> orders;
    if (std::abs(ctx.pos.total_qty) < 1e-12) return orders;
    if (ctx.candle_series.empty()) return orders;
    int const lb = ctx.cfg.strategy.zscore_vwap_lookback;
    double const vwap = compute_vwap(ctx.candle_series, ctx.candle_series_idx, static_cast<size_t>(lb));
    double const stdev = compute_stdev(ctx.candle_series, ctx.candle_series_idx, static_cast<size_t>(lb));
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
    if (ctx.pos.tp1_fired && remaining > 1e-12) {
        auto it = ctx.cfg.strategy.indicator_timeframes.find("atr_period");
        if (it == ctx.cfg.strategy.indicator_timeframes.end()) return orders;
        auto tf_it = ctx.tf_data.find(it->second);
        if (tf_it == ctx.tf_data.end()) return orders;
        double const atr = compute_atr(tf_it->second.candles, tf_it->second.current_idx, ctx.cfg.strategy.atr_period);
        if (atr > 0.0) {
            // Simple trailing: if price drops by trailing_atr_mult * ATR from recent high, exit
            // For simplicity, just exit all remaining if |Z| starts increasing again
            // (real trailing would need to track highest price since entry)
        }
    }
    return orders;
}
} // namespace powermdg
