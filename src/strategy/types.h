#ifndef POWERMDG_STRATEGY_TYPES_H
#define POWERMDG_STRATEGY_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace powermdg {

/// Current state of a single position (one symbol).
struct Position {
    double avg_entry_price = 0.0;
    double total_qty = 0.0;
    double traded_qty = 0.0;
    double realized_pnl = 0.0;
    int entry_levels = 0;
    int64_t entry_tick = 0;
    int64_t entry_timestamp_ms = 0;
    int unstuck_levels = 0;
};

/// A single snapshot of the equity curve.
struct EquityPoint {
    int64_t timestamp;
    double equity;
    double balance;
    double exposure_usd;
    std::vector<std::pair<std::string, double>> symbol_pnl;
};

/// Result of a full backtest run.
struct BacktestResult {
    std::vector<EquityPoint> equity_curve;
    std::vector<Position> final_positions;
    /// Duration in hours of each position that was opened and closed.
    /// Computed from position entry/exit timestamps during the backtest.
    /// Mirrors PassivBot's fill-based position_held_hours calculation.
    std::vector<double> position_durations_hours;
};

} // namespace powermdg

#endif // POWERMDG_STRATEGY_TYPES_H
