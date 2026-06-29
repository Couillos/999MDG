#include "parkinson_volatility.h"
#include "debug_log.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <thread>

namespace powermdg {

// ── DEBUG counters ──
static std::atomic<size_t> debug_pv_calls{0};
static std::atomic<size_t> debug_pv_total_span{0};
static std::atomic<double> debug_pv_time_ms{0};

static void log_pv_stats() {
    static thread_local auto last_log = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - last_log).count() < 5.0) return;
    last_log = now;
    size_t c = debug_pv_calls.load();
    size_t ts = debug_pv_total_span.load();
    double tm = debug_pv_time_ms.load();
    DEBUG_LOG(
        "[DEBUG] [parkinson_vol] calls=%zu avg_span=%.1f total_time=%.0fms\n",
        c, c > 0 ? static_cast<double>(ts) / c : 0.0, tm);
}

double compute_parkinson_volatility(std::span<const Candle> candles,
                                    size_t current_idx, int span) {
    auto t0 = std::chrono::steady_clock::now();
    if (current_idx + 1 < static_cast<size_t>(span)) {
        return 0.0;
    }

    size_t const start = current_idx - static_cast<size_t>(span) + 1;
    size_t const n = static_cast<size_t>(span);
    double sum = 0.0;

    for (size_t i = start; i <= current_idx; ++i) {
        double const ratio = candles[i].high / candles[i].low;
        if (ratio <= 0.0) {
            continue;
        }
        double const log_hl = std::log(ratio);
        sum += log_hl * log_hl;
    }

    double const denom = 4.0 * static_cast<double>(n) * std::log(2.0);
    if (denom <= 0.0) {
        return 0.0;
    }

    auto t1 = std::chrono::steady_clock::now();
    debug_pv_calls.fetch_add(1);
    debug_pv_total_span.fetch_add(static_cast<size_t>(span));
    debug_pv_time_ms.fetch_add(std::chrono::duration<double, std::milli>(t1 - t0).count());
    log_pv_stats();

    return std::sqrt(sum / denom);
}

} // namespace powermdg
