#ifndef MARTINGALE_STRATEGY_PARKINSON_VOLATILITY_H
#define MARTINGALE_STRATEGY_PARKINSON_VOLATILITY_H

#include "data/candle.h"
#include <cstddef>
#include <span>

namespace martingale {

/// Computes Parkinson volatility over a sliding window of candles.
double compute_parkinson_volatility(std::span<const Candle> candles,
                                    size_t current_idx, int span);

} // namespace martingale

#endif // MARTINGALE_STRATEGY_PARKINSON_VOLATILITY_H
