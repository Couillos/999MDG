#ifndef MARTINGALE_STRATEGY_STRATEGY_H
#define MARTINGALE_STRATEGY_STRATEGY_H

#include "config/types.h"
#include "data/candle_manager.h"
#include "data/symbol_info.h"
#include "strategy/types.h"
#include <vector>

namespace martingale {

/// Runs the full backtest over all symbols and returns the equity curve + positions.
/// If output_dir is non-empty, writes CSV files (equity, exposure, PnL) during the loop.
BacktestResult run_backtest(const Config& cfg,
                            const std::vector<LoadedCandles>& per_symbol_candles,
                            const std::vector<SymbolInfo>& symbols_info,
                            const std::string& output_dir = "");

} // namespace martingale

#endif // MARTINGALE_STRATEGY_STRATEGY_H
