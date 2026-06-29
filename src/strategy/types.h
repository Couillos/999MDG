#ifndef POWERMDG_STRATEGY_TYPES_H
#define POWERMDG_STRATEGY_TYPES_H
#include <cstdint>
#include <string>
#include <vector>
namespace powermdg {

struct Position {
    double avg_entry_price = 0.0;
    double total_qty = 0.0;
    double traded_qty = 0.0;
    double realized_pnl = 0.0;
    int entry_levels = 0;
    int64_t entry_tick = 0;
    int64_t entry_timestamp_ms = 0;
    int unstuck_levels = 0;
    // --- New fields for multi-module support ---
    double original_qty = 0.0;    // Initial entry qty (for graduated TP sizing)
    int entry_side = 0;           // 1=long, -1=short, 0=no position
    bool tp1_fired = false;       // Whether TP1 has triggered (for time-stop logic)
    int64_t entry_bar = 0;        // Bar index at entry (for time-stop)
};

struct EquityPoint {
    int64_t timestamp;
    double equity;
    double balance;
    double exposure_usd;
    std::vector<std::pair<std::string, double>> symbol_pnl;
};

struct BacktestResult {
    std::vector<EquityPoint> equity_curve;
    std::vector<Position> final_positions;
    std::vector<double> position_durations_hours;
};

} // namespace powermdg
#endif // POWERMDG_STRATEGY_TYPES_H
