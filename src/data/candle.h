#ifndef MARTINGALE_DATA_CANDLE_H
#define MARTINGALE_DATA_CANDLE_H

#include <cstdint>

namespace martingale {

/// OHLCV candle aligned to 64 bytes for cache-line efficiency.
struct alignas(64) Candle {
    int64_t timestamp;   // Unix ms
    double open, high, low, close, volume;
};

}  // namespace martingale

#endif  // MARTINGALE_DATA_CANDLE_H
