#include "parkinson_volatility.h"
#include <cmath>
#include <cstddef>

namespace martingale {

double compute_parkinson_volatility(std::span<const Candle> candles,
                                    size_t current_idx, int span) {
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

    return std::sqrt(sum / denom);
}

} // namespace martingale
