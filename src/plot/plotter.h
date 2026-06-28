#ifndef POWERMDG_PLOT_PLOTTER_H
#define POWERMDG_PLOT_PLOTTER_H

#include "config/types.h"
#include "metrics/types.h"
#include "strategy/types.h"
#include <string>
#include <vector>

namespace powermdg {

/// Generates PNG charts from backtest results using gnuplot.
/// All charts use a dark theme (colors defined in colors.h).
class Plotter {
public:
    Plotter(const Config& cfg,
            const std::vector<EquityPoint>& equity_curve,
            const Metrics& metrics,
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
    Metrics metrics_;
    std::string dir_;

    /// Writes a command string to a gnuplot pipe.
    void cmd(FILE* gp, const char* fmt, ...) const;

    /// Returns the first and last timestamp in seconds.
    void time_range(double& t0, double& t1) const;

    /// Writes the standard preamble (dark theme, time axis, xrange).
    void chart_preamble(FILE* gp, const char* png_name) const;

    /// Builds the chart title string from config.
    std::string title() const;
};

} // namespace powermdg

#endif // POWERMDG_PLOT_PLOTTER_H
