#include "zscore_ou.h"
#include <algorithm>
#include <cmath>
namespace powermdg {
namespace {
double compute_vwap(std::span<const Candle> cs, size_t end, size_t lb) {
    double pv=0, vv=0;
    size_t st = (end>lb)?end-lb:0;
    for (size_t i=st; i<=end && i<cs.size(); ++i) {
        double tp=(cs[i].high+cs[i].low+cs[i].close)/3.0;
        pv+=tp*cs[i].volume; vv+=cs[i].volume;
    }
    return vv>0?pv/vv:0.0;
}
double compute_stdev(std::span<const Candle> cs, size_t end, size_t lb) {
    if (end<2) return 0.0;
    size_t st=(end>lb)?end-lb:0;
    size_t n=0; double s=0,sq=0;
    for (size_t i=st; i<=end && i<cs.size(); ++i) { s+=cs[i].close; sq+=cs[i].close*cs[i].close; ++n; }
    if (n<2) return 0.0;
    double m=s/n; double v=sq/n-m*m; return std::sqrt(std::max(0.0,v));
}
double compute_atr(std::span<const Candle> cs, size_t end, int period) {
    if (cs.empty()||end<1) return 0.0;
    size_t st=(end>static_cast<size_t>(period))?end-period:0;
    double sum=0; size_t n=0;
    for (size_t i=st; i<=end&&i<cs.size(); ++i) {
        if(i==0) continue;
        double tr=std::max({cs[i].high-cs[i].low, std::abs(cs[i].high-cs[i-1].close), std::abs(cs[i].low-cs[i-1].close)});
        sum+=tr; ++n;
    }
    return n>0?sum/n:0.0;
}
double median_atr(std::span<const Candle> cs, size_t end, int period, int median_lookback) {
    if (end<2) return 0.0;
    std::vector<double> atrs;
    size_t st=(end>static_cast<size_t>(median_lookback))?end-median_lookback:0;
    for (size_t i=st; i<=end&&i<cs.size(); ++i) {
        double a=compute_atr(cs, i+1, period);
        if(a>0) atrs.push_back(a);
    }
    if (atrs.empty()) return 0.0;
    std::sort(atrs.begin(), atrs.end());
    return atrs[atrs.size()/2];
}
} // anonymous namespace

bool ZscoreOuEntryCondition::should_enter(const ModuleContext& ctx) const {
    if (ctx.candle_series.empty()) return false;
    int const lb = ctx.cfg.strategy.zscore_vwap_lookback;
    if (lb < 2) return false;
    double const vwap = compute_vwap(ctx.candle_series, ctx.candle_series_idx, static_cast<size_t>(lb));
    double const stdev = compute_stdev(ctx.candle_series, ctx.candle_series_idx, static_cast<size_t>(lb));
    if (vwap<=0.0 || stdev<=0.0) return false;
    double const z = (ctx.candle.close - vwap) / stdev;
    // LONG signal: Z <= -threshold
    if (z > -ctx.cfg.strategy.zscore_entry_threshold) return false;
    // ATR regime filter: ATR(14,HTF) < mult * median ATR
    if (ctx.cfg.strategy.atr_filter_mult > 0.0) {
        auto it = ctx.cfg.strategy.indicator_timeframes.find("atr_period");
        if (it != ctx.cfg.strategy.indicator_timeframes.end()) {
            auto tf_it = ctx.tf_data.find(it->second);
            if (tf_it != ctx.tf_data.end()) {
                auto const& htf = tf_it->second;
                double const atr = compute_atr(htf.candles, htf.current_idx, ctx.cfg.strategy.atr_period);
                double const med = median_atr(htf.candles, htf.current_idx, ctx.cfg.strategy.atr_period, 30);
                if (med > 0.0 && atr > ctx.cfg.strategy.atr_filter_mult * med) return false;
            }
        }
    }
    return true;
}
} // namespace powermdg
