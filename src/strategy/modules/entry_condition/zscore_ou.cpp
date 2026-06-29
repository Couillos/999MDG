#include "zscore_ou.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
namespace powermdg {
namespace {

// ── DEBUG counters ──
static std::atomic<size_t> debug_atr_calls{0};
static std::atomic<size_t> debug_atr_total_n{0};
static std::atomic<size_t> debug_median_atr_calls{0};
static std::atomic<size_t> debug_median_atr_inner_atr_calls{0};
static std::atomic<double> debug_atr_time_ms{0};
static std::atomic<double> debug_median_atr_time_ms{0};
static thread_local bool debug_tid_logged = false;

static void log_thread_once() {
    if (!debug_tid_logged) {
        debug_tid_logged = true;
        std::fprintf(stderr, "[DEBUG] [zscore_ou] thread=%zx\n",
                     std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
}

static void log_stats_if_needed() {
    static thread_local auto last_log = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - last_log).count() < 5.0) return;
    last_log = now;
    auto ac = debug_atr_calls.load();
    auto mc = debug_median_atr_calls.load();
    auto mi = debug_median_atr_inner_atr_calls.load();
    std::fprintf(stderr,
        "[DEBUG] [zscore_ou] calls: atr=%zu median_atr=%zu (atrs_computed=%zu)\n",
        ac, mc, mi);
    std::fprintf(stderr,
        "[DEBUG] [zscore_ou] tot_n: atr=%zu\n",
        debug_atr_total_n.load());
    std::fprintf(stderr,
        "[DEBUG] [zscore_ou] time_ms: atr=%.0f median_atr=%.0f\n",
        debug_atr_time_ms.load(), debug_median_atr_time_ms.load());
}

double compute_atr(std::span<const Candle> cs, size_t end, int period) {
    log_thread_once();
    auto t0 = std::chrono::steady_clock::now();
    if (cs.empty()||end<1) return 0.0;
    size_t st=(end>static_cast<size_t>(period))?end-period:0;
    double sum=0; size_t n=0;
    for (size_t i=st; i<=end&&i<cs.size(); ++i) {
        if(i==0) continue;
        double tr=std::max({cs[i].high-cs[i].low, std::abs(cs[i].high-cs[i-1].close), std::abs(cs[i].low-cs[i-1].close)});
        sum+=tr; ++n;
    }
    auto t1 = std::chrono::steady_clock::now();
    debug_atr_calls.fetch_add(1);
    debug_atr_total_n.fetch_add(n);
    debug_atr_time_ms.fetch_add(std::chrono::duration<double, std::milli>(t1 - t0).count());
    return n>0?sum/n:0.0;
}
double median_atr(std::span<const Candle> cs, size_t end, int period, int median_lookback) {
    log_thread_once();
    auto t0 = std::chrono::steady_clock::now();
    if (end<2) { return 0.0; }

    // ── Sliding-window ATR ─────────────────────────────────────────────
    // Instead of calling compute_atr() 30 times independently (O(30*period)),
    // precompute ALL needed True Range values once, then slide the window.
    size_t st = (end > static_cast<size_t>(median_lookback)) ? end - median_lookback : 0;
    size_t first_win_end = st + 1;            // first ATR computed at end=st+1
    size_t last_win_end  = std::min(end + 1, cs.size() - 1);

    if (first_win_end > last_win_end) return 0.0;

    // True-Range range: first_win_end - period + 1  … last_win_end
    size_t tr_beg = (first_win_end > static_cast<size_t>(period))
                        ? first_win_end - static_cast<size_t>(period) + 1
                        : 1;
    if (tr_beg < 1) tr_beg = 1;
    if (tr_beg >= last_win_end) return 0.0;

    size_t tr_len = last_win_end - tr_beg + 1;
    std::vector<double> tr_buf(tr_len);
    for (size_t i = tr_beg; i <= last_win_end; ++i) {
        tr_buf[i - tr_beg] = std::max({
            cs[i].high - cs[i].low,
            std::abs(cs[i].high - cs[i-1].close),
            std::abs(cs[i].low - cs[i-1].close)
        });
    }

    // First ATR at first_win_end
    double sum = 0.0;
    size_t n = 0;
    size_t atr_start = first_win_end - static_cast<size_t>(period) + 1;
    if (atr_start < tr_beg) atr_start = tr_beg;
    for (size_t i = atr_start; i <= first_win_end; ++i) {
        sum += tr_buf[i - tr_beg];
        ++n;
    }

    std::vector<double> atrs;
    atrs.reserve(last_win_end - first_win_end + 1);
    atrs.push_back(n > 0 ? sum / static_cast<double>(n) : 0.0);

    // Slide
    for (size_t i = first_win_end + 1; i <= last_win_end; ++i) {
        // TR that falls out of the window
        size_t out_idx = i - static_cast<size_t>(period);
        if (out_idx >= tr_beg && out_idx <= i - 1 && out_idx < tr_beg + tr_len) {
            sum -= tr_buf[out_idx - tr_beg];
            --n;
        }
        // TR that enters
        size_t new_idx = i;
        if (new_idx >= tr_beg && new_idx < tr_beg + tr_len) {
            sum += tr_buf[new_idx - tr_beg];
            ++n;
        }
        atrs.push_back(n > 0 ? sum / static_cast<double>(n) : 0.0);
    }

    auto t1 = std::chrono::steady_clock::now();
    debug_median_atr_calls.fetch_add(1);
    debug_median_atr_inner_atr_calls.fetch_add(atrs.size());
    debug_median_atr_time_ms.fetch_add(std::chrono::duration<double, std::milli>(t1 - t0).count());

    if (atrs.empty()) return 0.0;
    std::sort(atrs.begin(), atrs.end());
    return atrs[atrs.size() / 2];
}
} // anonymous namespace

bool ZscoreOuEntryCondition::should_enter(const ModuleContext& ctx) const {
    log_thread_once();
    log_stats_if_needed();
    if (ctx.candle_series.empty()) return false;
    double const vwap = ctx.vwap;
    double const stdev = ctx.stdev;
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
