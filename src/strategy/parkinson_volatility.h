#ifndef POWERMDG_STRATEGY_PARKINSON_VOLATILITY_H
#define POWERMDG_STRATEGY_PARKINSON_VOLATILITY_H

#include "data/candle.h"
#include <cstddef>
#include <span>

namespace powermdg {

/// Computes Parkinson volatility over a sliding window of candles.
double compute_parkinson_volatility(std::span<const Candle> candles,
                                    size_t current_idx, int span);

} // namespace powermdg

#endif // POWERMDG_STRATEGY_PARKINSON_VOLATILITY_H
