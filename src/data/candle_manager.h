#ifndef MARTINGALE_DATA_CANDLE_MANAGER_H
#define MARTINGALE_DATA_CANDLE_MANAGER_H

#include "candle.h"
#include "config/types.h"
#include <cstddef>
#include <string>
#include <vector>

namespace martingale {

/// Result of load_candles with candles and the trading-start index.
struct LoadedCandles {
    std::vector<Candle> candles;
    size_t trading_start_idx = 0;
};

/// Downloads missing daily 1m data, aggregates to the target timeframe,
/// caches the result, and returns the candles for all symbols concatenated.
LoadedCandles load_candles(const Config& cfg);

}  // namespace martingale

#endif  // MARTINGALE_DATA_CANDLE_MANAGER_H
