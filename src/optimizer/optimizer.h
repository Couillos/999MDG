#ifndef POWERMDG_OPTIMIZER_OPTIMIZER_H
#define POWERMDG_OPTIMIZER_OPTIMIZER_H

#include "config/types.h"
#include "data/candle_manager.h"
#include "data/symbol_info.h"
#include "optimizer/types.h"
#include <functional>
#include <vector>

namespace powermdg {

/// Callback invoked after each generation is evaluated.
/// Receives the best RunResult of the generation, current generation index, and total generations.
using OptimizerCallback = std::function<void(const RunResult&, size_t, size_t)>;

/// Runs NSGA-II multi-objective optimization using genetic algorithm.
/// @param cfg           Full config (mode, strategy defaults, optimize bounds, GA params)
/// @param per_symbol_candles  Candle data per symbol
/// @param symbols_info  Symbol metadata
/// @param results_path  If non-empty, writes all results as zstd-compressed JSON
/// @param callback      Optional progress callback invoked after each generation
/// @param live_state_path  If non-empty, writes live state JSON here after each generation
OptimizerResult run_optimization(
    const Config& cfg,
    const std::vector<LoadedCandles>& per_symbol_candles,
    const std::vector<SymbolInfo>& symbols_info,
    const std::string& results_path = "",
    OptimizerCallback callback = nullptr,
    const std::string& live_state_path = ""
);

} // namespace powermdg

#endif // POWERMDG_OPTIMIZER_OPTIMIZER_H
