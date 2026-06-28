#ifndef POWERMDG_STRATEGY_MODULES_MODULE_CONTEXT_H
#define POWERMDG_STRATEGY_MODULES_MODULE_CONTEXT_H

#include "config/types.h"
#include "data/candle.h"
#include "data/symbol_info.h"
#include "strategy/types.h"
#include <cstdint>
#include <span>

namespace powermdg {

struct EntryOrder { double qty; };
struct CloseOrder { double qty; };

enum class DataNeed : uint32_t {
    None          = 0,
    Ema           = 1 << 0,
    RollingStdev  = 1 << 1,
    CandleSeries  = 1 << 2,
};

inline DataNeed operator|(DataNeed a, DataNeed b) {
    return static_cast<DataNeed>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool needs(DataNeed flags, DataNeed bit) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(bit)) != 0;
}

struct ModuleContext {
    const Config& cfg;
    const SymbolInfo& info;
    const Candle& candle;
    const Position& pos;
    double total_balance;
    int64_t current_tick;
    double ema;
    double rolling_stdev;
    std::span<const Candle> candle_series;
};

} // namespace powermdg
#endif // POWERMDG_STRATEGY_MODULES_MODULE_CONTEXT_H
