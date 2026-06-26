#ifndef MARTINGALE_PLOT_PLOTTER_H
#define MARTINGALE_PLOT_PLOTTER_H

#include "config/types.h"
#include "strategy/types.h"
#include <string>
#include <vector>

namespace martingale {

/// Generates PNG charts from backtest results using gnuplot.
/// All charts use a dark theme (colors defined in colors.h).
class Plotter {
public:
    Plotter(const Config& cfg,
            const std::vector<EquityPoint>& equity_curve,
            const std::string& results_dir);

    /// Generates equity_chart.png: equity + balance curves.
    void equity_chart();

    /// Generates exposure_chart.png: capital exposure over time.
    void exposure_chart();

    /// Generates pnl_per_symbol.png: cumulative PnL per symbol.
    void pnl_per_symbol_chart();

    /// Generates all three charts at once.
    void generate_all();

private:
    const Config& cfg_;
    const std::vector<EquityPoint>& curve_;
    std::string dir_;

    /// Writes a command string to a gnuplot pipe.
    void cmd(FILE* gp, const char* fmt, ...) const;

    /// Builds the chart title string from config.
    std::string title() const;
};

} // namespace martingale

#endif // MARTINGALE_PLOT_PLOTTER_H
