#ifndef POWERMDG_STRATEGY_STRATEGY_H
#define POWERMDG_STRATEGY_STRATEGY_H

#include "config/types.h"
#include "data/candle_manager.h"
#include "data/symbol_info.h"
#include "strategy/types.h"
#include <map>
#include <string>
#include <vector>

namespace powermdg {

/// Pre-loads candle data for all additional timeframes (beyond the base
/// timeframe) declared in cfg.strategy.indicator_timeframes.
/// The returned map can be passed to run_backtest() to avoid redundant
/// loading during optimisation (where run_backtest is called many times).
std::map<std::string, std::vector<LoadedCandles>> load_all_mtf_candles(const Config& cfg);

/// Runs the full backtest over all symbols and returns the equity curve + positions.
/// If output_dir is non-empty, writes CSV files (equity, exposure, PnL) during the loop.
/// If mtf_candles is non-null, uses pre-loaded multi-timeframe data (avoids reloading).
BacktestResult run_backtest(const Config& cfg,
                            const std::vector<LoadedCandles>& per_symbol_candles,
                            const std::vector<SymbolInfo>& symbols_info,
                            const std::string& output_dir = "",
                            const std::map<std::string, std::vector<LoadedCandles>>* mtf_candles = nullptr);

} // namespace powermdg

#endif // POWERMDG_STRATEGY_STRATEGY_H
