#ifndef POWERMDG_STRATEGY_MODULES_MODULE_CONTEXT_H
#define POWERMDG_STRATEGY_MODULES_MODULE_CONTEXT_H
#include "config/types.h"
#include "data/candle.h"
#include "data/symbol_info.h"
#include "strategy/types.h"
#include <cstdint>
#include <map>
#include <span>
#include <string>
namespace powermdg {

struct EntryOrder { double qty; };
struct CloseOrder { double qty; };

enum class DataNeed : uint32_t {
    None=0, Ema=1<<0, RollingStdev=1<<1, CandleSeries=1<<2, MultiTimeframe=1<<3,
};
inline DataNeed operator|(DataNeed a, DataNeed b) {
    return static_cast<DataNeed>(static_cast<uint32_t>(a)|static_cast<uint32_t>(b));
}
inline bool needs(DataNeed f, DataNeed b) {
    return (static_cast<uint32_t>(f)&static_cast<uint32_t>(b))!=0;
}

/// Candle data for a specific timeframe, passed to modules.
struct TfCandles {
    std::string timeframe;
    std::span<const Candle> candles;
    size_t current_idx = 0;
};

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
    size_t candle_series_idx;
    // Multi-timeframe: map of timeframe → TfCandles
    // Modules look up by timeframe: ctx.tf_data.at("1h")
    std::map<std::string, TfCandles> tf_data;
};

} // namespace powermdg
#endif
