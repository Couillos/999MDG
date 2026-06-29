#ifndef POWERMDG_OPTIMIZER_OPTIMIZER_H
#define POWERMDG_OPTIMIZER_OPTIMIZER_H

#include "config/types.h"
#include "data/candle_manager.h"
#include "data/symbol_info.h"
#include "optimizer/types.h"
#include "optimizer/nsga2.h"
#include <functional>
#include <vector>

namespace powermdg {

/// Callback invoked after each generation is evaluated.
/// Receives the best RunResult of the generation, current generation index, and total generations.
using OptimizerCallback = std::function<void(const RunResult&, size_t, size_t)>;

// ---------------------------------------------------------------------------
// Test/internal helpers (exposed for unit tests)
// ---------------------------------------------------------------------------

/// Apply a single named parameter value to a Config struct.
/// Used by the optimizer for each gene, and exposed here for unit testing.
void apply_param_to_cfg(Config& cfg, const std::string& name, double value);

/// Compute engine-space objectives for every individual in `population`.
/// NSGA-II minimises all objectives; engine_sign flips "max" goals.
///
/// DEFECT B (weights in NSGA-II): Pareto dominance is per-axis scale-invariant,
/// so multiplying an objective by weight does NOT change which individuals
/// dominate which others, and crowding distance is re-normalised per axis by
/// the sort — weights therefore have ~no real effect on the evolutionary
/// selection process.  Weights DO influence the FINAL best-candidate selection:
/// run_optimization() uses a weighted-sum scalar over normalised per-objective
/// values to rank the feasible Pareto front and pick the single best (#1)
/// candidate, so a higher weight genuinely pulls the selection toward that
/// metric.  The weight stored in each objective here is the engine_sign only;
/// the per-weight scaling happens in the final scorer, not here.
void compute_objectives_for_population(
    std::vector<Individual>& population,
    const std::vector<ScoringMetric>& scoring);

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
