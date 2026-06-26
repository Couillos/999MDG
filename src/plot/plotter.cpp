#include "plotter.h"
#include "colors.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>

namespace martingale {

Plotter::Plotter(const Config& cfg,
                 const std::vector<EquityPoint>& equity_curve,
                 const std::string& results_dir)
    : cfg_(cfg), curve_(equity_curve), dir_(results_dir) {}

void Plotter::cmd(FILE* gp, const char* fmt, ...) const {
    va_list args;
    va_start(args, fmt);
    std::vfprintf(gp, fmt, args);
    va_end(args);
}

std::string Plotter::title() const {
    std::string t;
    for (size_t i = 0; i < cfg_.symbols.size(); ++i) {
        if (i > 0) t += ", ";
        t += cfg_.symbols[i];
    }
    t += " | " + cfg_.timeframe + " | " + cfg_.date_from + " → " + cfg_.date_to;
    return t;
}

/// Returns the time range bounds in seconds (gnuplot epoch).
void Plotter::time_range(double& t0, double& t1) const {
    t0 = static_cast<double>(curve_.front().timestamp) / 1000.0;
    t1 = static_cast<double>(curve_.back().timestamp) / 1000.0;
}

/// Writes common gnuplot preamble (dark theme, time axis, format).
void Plotter::chart_preamble(FILE* gp, const char* png_name) const {
    double t0, t1;
    time_range(t0, t1);
    std::string const out = dir_ + "/" + png_name;

    cmd(gp, "set terminal pngcairo size 1600,900 enhanced\n");
    cmd(gp, "set output '%s'\n", out.c_str());
    cmd(gp, "set datafile separator ','\n");
    cmd(gp, "set object 1 rectangle from screen 0,0 to screen 1,1 behind fillcolor rgb '%s' fillstyle solid\n", plot::BG);
    cmd(gp, "set border lc rgb '%s'\n", plot::BORDER);
    cmd(gp, "set grid lc rgb '%s'\n", plot::GRID);
    cmd(gp, "set key textcolor rgb '%s'\n", plot::TEXT);
    cmd(gp, "set xlabel textcolor rgb '%s' \"Date\"\n", plot::TEXT);
    cmd(gp, "set ylabel textcolor rgb '%s' \"USD\"\n", plot::TEXT);
    cmd(gp, "set xdata time\n");
    cmd(gp, "set timefmt '%%s'\n");
    cmd(gp, "set format x '%%Y-%%m'\n");
    cmd(gp, "set xrange [%.0f:%.0f] noreverse nowriteback\n", t0, t1);
    cmd(gp, "set tics textcolor rgb '%s'\n", plot::TEXT);
}

void Plotter::equity_chart() {
    FILE* gp = popen("gnuplot -p 2>/dev/null", "w");
    if (!gp) {
        std::fprintf(stderr, "Warning: gnuplot not available, skipping equity_chart.png\n");
        return;
    }

    std::string const csv = dir_ + "/data/equity_curve.csv";
    chart_preamble(gp, "equity_chart.png");
    cmd(gp, "set title textcolor rgb '%s' font ',14' \"Equity & Balance — %s\"\n", plot::TEXT, title().c_str());
    cmd(gp, "plot '%s' every ::1 using ($1/1000):2 with lines lw 2 lc rgb '%s' title 'Equity', ", csv.c_str(), plot::EQUITY);
    cmd(gp, "'%s' every ::1 using ($1/1000):3 with lines dt 2 lw 2 lc rgb '%s' title 'Balance'\n", csv.c_str(), plot::BALANCE);

    int const ret = pclose(gp);
    if (ret == 0) {
        std::printf("  Wrote %s\n", (dir_ + "/equity_chart.png").c_str());
    } else {
        std::fprintf(stderr, "Warning: gnuplot exited with code %d for equity_chart\n", ret);
    }
}

void Plotter::exposure_chart() {
    FILE* gp = popen("gnuplot -p 2>/dev/null", "w");
    if (!gp) {
        std::fprintf(stderr, "Warning: gnuplot not available, skipping exposure_chart.png\n");
        return;
    }

    std::string const csv = dir_ + "/data/exposure.csv";
    double const max_exposure = cfg_.initial_balance_usd * cfg_.total_wallet_exposure;
    chart_preamble(gp, "exposure_chart.png");
    cmd(gp, "set ylabel textcolor rgb '%s' \"Exposure (%% of max)\"\n", plot::TEXT);
    cmd(gp, "set title textcolor rgb '%s' font ',14' \"Capital Exposure — %s\"\n", plot::TEXT, title().c_str());
    cmd(gp, "plot '%s' every ::1 using ($1/1000):($2 / %.2f * 100) with lines lw 2 lc rgb '%s' title 'Exposure %%', ", csv.c_str(), max_exposure, plot::EXPOSURE);
    cmd(gp, "100 with lines dt 3 lw 1 lc rgb '%s' title 'Max Exposure'\n", plot::BALANCE);

    int const ret = pclose(gp);
    if (ret == 0) {
        std::printf("  Wrote %s\n", (dir_ + "/exposure_chart.png").c_str());
    } else {
        std::fprintf(stderr, "Warning: gnuplot exited with code %d for exposure_chart\n", ret);
    }
}

void Plotter::pnl_per_symbol_chart() {
    FILE* gp = popen("gnuplot -p 2>/dev/null", "w");
    if (!gp) {
        std::fprintf(stderr, "Warning: gnuplot not available, skipping pnl_per_symbol.png\n");
        return;
    }

    std::string const csv = dir_ + "/data/pnl_symbol.csv";
    chart_preamble(gp, "pnl_per_symbol.png");
    cmd(gp, "set title textcolor rgb '%s' font ',14' \"Cumulative PnL per Symbol — %s\"\n", plot::TEXT, title().c_str());
    cmd(gp, "set style data lines\n");

    // Build plot command with one column per symbol
    // CSV format: timestamp,symbol1,symbol2,...
    // using 1:2 for first symbol, 1:3 for second, etc.
    std::string plot_cmd = "plot ";
    for (size_t i = 0; i < cfg_.symbols.size(); ++i) {
        if (i > 0) plot_cmd += ", ";
        plot_cmd += "'" + csv + "' every ::1 using ($1/1000):" + std::to_string(i + 2);
        plot_cmd += " lw 2 " + plot::symbol_lc(i);
        plot_cmd += " title '" + cfg_.symbols[i] + "'";
    }
    cmd(gp, "%s\n", plot_cmd.c_str());

    int const ret = pclose(gp);
    if (ret == 0) {
        std::printf("  Wrote %s\n", (dir_ + "/pnl_per_symbol.png").c_str());
    } else {
        std::fprintf(stderr, "Warning: gnuplot exited with code %d for pnl_per_symbol\n", ret);
    }
}

void Plotter::generate_all() {
    if (curve_.empty()) {
        std::printf("  Skipping charts (empty equity curve)\n");
        return;
    }
    equity_chart();
    exposure_chart();
    pnl_per_symbol_chart();
}

} // namespace martingale
