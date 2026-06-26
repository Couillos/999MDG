#ifndef MARTINGALE_OPTIMIZER_TYPES_H
#define MARTINGALE_OPTIMIZER_TYPES_H

#include "metrics/types.h"
#include <map>
#include <string>
#include <vector>

namespace martingale {

/// Result of a single parameter combination run during optimization.
struct RunResult {
    std::map<std::string, double> params;
    Metrics metrics;
    std::vector<double> objectives;
    double constraint_violation = 0.0;
    int generation = 0;
};

/// Result of a full optimization run with Pareto front and all evaluated results.
struct OptimizerResult {
    std::vector<RunResult> pareto_front;
    std::vector<RunResult> all_results;
};

} // namespace martingale

#endif // MARTINGALE_OPTIMIZER_TYPES_H
