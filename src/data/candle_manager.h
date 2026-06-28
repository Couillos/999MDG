#ifndef POWERMDG_DATA_CANDLE_MANAGER_H
#define POWERMDG_DATA_CANDLE_MANAGER_H

#include "candle.h"
#include "config/types.h"
#include <cstddef>
#include <string>
#include <vector>

namespace powermdg {

/// Result of load_candles with candles and the trading-start index.
struct LoadedCandles {
    std::vector<Candle> candles;
    size_t trading_start_idx = 0;
};

/// Downloads missing daily 1m data, aggregates to the target timeframe,
/// caches the result, and returns the candles for all symbols concatenated.
LoadedCandles load_candles(const Config& cfg);

}  // namespace powermdg

#endif  // POWERMDG_DATA_CANDLE_MANAGER_H
