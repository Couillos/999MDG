#ifndef MARTINGALE_OPTIMIZER_OPTIMIZER_H
#define MARTINGALE_OPTIMIZER_OPTIMIZER_H

#include "config/types.h"
#include "data/candle_manager.h"
#include "data/symbol_info.h"
#include "optimizer/types.h"
#include <functional>
#include <vector>

namespace martingale {

/// Callback invoked after each parameter combination is evaluated.
/// Receives the RunResult and current combo index + total combos.
using OptimizerCallback = std::function<void(const RunResult&, size_t, size_t)>;

/// Generates all parameter combinations from bounds, runs backtests (optionally with
/// a progress callback), writes all results compressed via zstd, and returns top N.
/// @param cfg           Full config (mode, strategy defaults, optimize bounds)
/// @param per_symbol_candles  Candle data per symbol
/// @param symbols_info  Symbol metadata
/// @param results_path  If non-empty, writes all results as zstd-compressed JSON
/// @param callback      Optional progress callback invoked after each combo
/// @param top_n         Number of top results to return (default 100)
/// @param live_state_path  If non-empty, writes live state JSON here every 50 combos
std::vector<RunResult> run_optimization(
    const Config& cfg,
    const std::vector<LoadedCandles>& per_symbol_candles,
    const std::vector<SymbolInfo>& symbols_info,
    const std::string& results_path = "",
    OptimizerCallback callback = nullptr,
    size_t top_n = 100,
    const std::string& live_state_path = ""
);

} // namespace martingale

#endif // MARTINGALE_OPTIMIZER_OPTIMIZER_H
