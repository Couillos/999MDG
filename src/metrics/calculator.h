#ifndef POWERMDG_METRICS_CALCULATOR_H
#define POWERMDG_METRICS_CALCULATOR_H

#include "config/types.h"
#include "metrics/types.h"
#include "strategy/types.h"
#include <vector>

namespace powermdg {

/// Computes all performance metrics from the backtest result.
Metrics compute_metrics(const BacktestResult& result,
                        const Config& cfg);

} // namespace powermdg

#endif // POWERMDG_METRICS_CALCULATOR_H
