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
    double score = 0.0;
    bool valid = false;
};

} // namespace martingale

#endif // MARTINGALE_OPTIMIZER_TYPES_H
