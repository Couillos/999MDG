#ifndef POWERMDG_CONFIG_LOADER_H
#define POWERMDG_CONFIG_LOADER_H

#include "types.h"
#include <string>

namespace powermdg {

/// Convert a timeframe string ("1h", "4h", "15m", etc.) to minutes.
/// Returns 0 on invalid input.
int timeframe_to_minutes(const std::string& tf);

/// Convert a timeframe string to its millisecond equivalent.
int64_t timeframe_to_ms(const std::string& tf);

/// Returns the candle ratio: how many `base` candles fit in one `tf` candle.
/// e.g. tf="4h", base="1h" → 4.  tf="15m", base="1h" → 0.25.
double candle_ratio(const std::string& tf, const std::string& base);

/// Returns the set of parameter names that support a timeframe prefix.
bool supports_timeframe(const std::string& name);

/// Loads and validates a JSON config file, returns a fully populated Config struct.
Config load_config(const std::string& path, Mode mode);

}  // namespace powermdg

#endif  // POWERMDG_CONFIG_LOADER_H
