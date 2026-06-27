#ifndef MARTINGALE_METRICS_CALCULATOR_H
#define MARTINGALE_METRICS_CALCULATOR_H

#include "config/types.h"
#include "metrics/types.h"
#include "strategy/types.h"
#include <vector>

namespace martingale {

/// Computes all performance metrics from the backtest result.
Metrics compute_metrics(const BacktestResult& result,
                        const Config& cfg);

} // namespace martingale

#endif // MARTINGALE_METRICS_CALCULATOR_H
