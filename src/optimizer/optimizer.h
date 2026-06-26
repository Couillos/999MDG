#ifndef MARTINGALE_OPTIMIZER_OPTIMIZER_H
#define MARTINGALE_OPTIMIZER_OPTIMIZER_H

#include "config/types.h"
#include "data/candle_manager.h"
#include "data/symbol_info.h"
#include "optimizer/types.h"
#include <vector>

namespace martingale {

/// Generates all parameter combinations from bounds, runs backtests in parallel,
/// scores results, and returns the top 100 sorted by score descending.
std::vector<RunResult> run_optimization(
    const Config& cfg,
    const std::vector<LoadedCandles>& per_symbol_candles,
    const std::vector<SymbolInfo>& symbols_info
);

} // namespace martingale

#endif // MARTINGALE_OPTIMIZER_OPTIMIZER_H
