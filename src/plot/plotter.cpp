#include "plotter.h"
#include "colors.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <numeric>
#include <string>

namespace martingale {

namespace {

/// Ensure SIGPIPE is ignored so that an early gnuplot exit does not kill
/// the whole backtest process. Fix for audit issue Plot-4.
struct SigpipeIgnore {
    SigpipeIgnore() {
        // SIG_IGN is portable; do not use signal() with multi-threaded programs.
        std::signal(SIGPIPE, SIG_IGN);
    }
};

SigpipeIgnore g_sigpipe_ignore{};

std::string fmt_metric(double v) {
    char buf[32];
    if (v >= 1000.0 || v <= -1000.0) {
        std::snprintf(buf, sizeof(buf), "%.2f", v);
    } else if (v >= 1.0 || v <= -1.0) {
        std::snprintf(buf, sizeof(buf), "%.4f", v);
    } else if (v >= 0.01 || v <= -0.01) {
        std::snprintf(buf, sizeof(buf), "%.6f", v);
    } else {
        std::snprintf(buf, sizeof(buf), "%.8f", v);
    }
    return std::string(buf);
}

/// Formats a double using %.6g (6 significant digits, like Python's `:.6g`).
/// This matches the format used in plot_balance_equity_jk2.py's metrics panel.
std::string fmt_g6(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return std::string(buf);
}

/// Builds the metrics panel text exactly as in plot_balance_equity_jk2.py's
/// create_metrics_panel(): 22 metrics, one per line, format "key: value".
/// Order matches the Python script.
std::string build_metrics_panel_text(const Metrics& m, const std::vector<EquityPoint>& curve) {
    std::string cumret;
    if (curve.size() >= 2 && curve.front().equity > 0.0) {
        double const initial = curve.front().equity;
        double const final_eq = curve.back().equity;
        double const cr = (final_eq / initial - 1.0) * 100.0;
        cumret = fmt_g6(cr) + "%";
    } else {
        cumret = "-";
    }

    auto label = [](const std::string& raw) {
        std::string out = raw;
        for (auto& c : out) if (c == '_') c = ' ';
        return out;
    };

    std::string text;
    text.reserve(1024);
    text += "adg: " + fmt_g6(m.adg_usd) + "\n";
    text += "mdg: " + fmt_g6(m.mdg_usd) + "\n";
    text += "gain: " + fmt_g6(m.gain) + "\n";
    text += label("cumulative_return") + ": " + cumret + "\n";
    text += label("drawdown_worst") + ": " + fmt_g6(m.drawdown_worst) + "\n";
    text += label("drawdown_worst_mean_1pct") + ": " + fmt_g6(m.drawdown_worst_mean_1pct) + "\n";
    text += label("loss_profit_ratio") + ": " + fmt_g6(m.loss_profit_ratio_long) + "\n";
    text += label("sortino_ratio") + ": " + fmt_g6(m.sortino_ratio_usd) + "\n";
    text += label("calmar_ratio") + ": " + fmt_g6(m.calmar_ratio_usd) + "\n";
    text += label("sterling_ratio") + ": " + fmt_g6(m.sterling_ratio) + "\n";
    text += label("sharpe_ratio") + ": " + fmt_g6(m.sharpe_ratio_usd) + "\n";
    text += label("omega_ratio") + ": " + fmt_g6(m.omega_ratio_usd) + "\n";
    text += label("equity_balance_diff_neg_max") + ": " + fmt_g6(m.equity_balance_diff_neg_max_usd) + "\n";
    text += label("equity_balance_diff_neg_mean") + ": " + fmt_g6(m.equity_balance_diff_neg_mean_usd) + "\n";
    text += label("equity_balance_diff_pos_max") + ": " + fmt_g6(m.equity_balance_diff_pos_max_usd) + "\n";
    text += label("equity_balance_diff_pos_mean") + ": " + fmt_g6(m.equity_balance_diff_pos_mean_usd) + "\n";
    text += label("position_held_hours_max") + ": " + fmt_g6(m.position_held_hours_max) + "\n";
    text += label("position_held_hours_mean") + ": " + fmt_g6(m.position_held_hours_mean) + "\n";
    text += label("position_held_hours_median") + ": " + fmt_g6(m.position_held_hours_median) + "\n";
    text += label("position_unchanged_hours_max") + ": " + fmt_g6(m.position_unchanged_hours_max) + "\n";
    text += label("positions_held_per_day") + ": " + fmt_g6(m.positions_held_per_day) + "\n";
    text += label("peak_recovery_hours") + ": " + fmt_g6(m.peak_recovery_hours_equity_usd);
    return text;
}
} // anonymous namespace

Plotter::Plotter(const Config& cfg,
                 const std::vector<EquityPoint>& equity_curve,
                 const Metrics& metrics,
                 const std::string& results_dir)
    : cfg_(cfg), curve_(equity_curve), metrics_(metrics), dir_(results_dir) {}

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

    // Metrics panel — reproduces plot_balance_equity_jk2.py's create_metrics_panel():
    // 22 metrics, one per line, format "key: value" with %.6g for floats.
    // Placed at top-left, monospace font, semi-transparent dark blue background.
    std::string const panel_text = build_metrics_panel_text(metrics_, curve_);

    // gnuplot uses \n (literal backslash-n) for newlines inside quoted strings.
    // build_metrics_panel_text returns real newlines; convert them to \\n for gnuplot.
    // build_metrics_panel_text replaces underscores with spaces so the "enhanced"
    // terminal mode never sees a raw _ (which would start subscript).
    std::string gnuplot_text;
    gnuplot_text.reserve(panel_text.size() * 2);
    for (char c : panel_text) {
        if (c == '\n') {
            gnuplot_text += "\\n";
        } else if (c == '"') {
            gnuplot_text += "\\\"";
        } else if (c == '_') {
            // double-escape so gnuplot's double-quote string parser
            // turns \\_ into \_ before enhanced text sees it
            gnuplot_text += "\\\\_";
        } else {
            gnuplot_text += c;
        }
    }

    // Style: JK2-inspired dark blue panel with light blue border.
    // font ',11' = 11pt; textcolor = plot::TEXT (light/white).
    cmd(gp, "set style textbox 1 lw 1 fc rgb '#0a1a2a' border rgb '#2a5a8a'\n");
    cmd(gp, "set label 1 at graph 0.015, graph 0.978 \"%s\" front tc rgb '%s' font ',11' boxed\n",
        gnuplot_text.c_str(), plot::TEXT);

    cmd(gp, "plot '%s' every ::1 using ($1/1000):2 with lines lw 2 lc rgb '%s' title 'Equity', ", csv.c_str(), plot::EQUITY);
    cmd(gp, "'%s' every ::1 using ($1/1000):3 with lines lw 2 lc rgb '%s' title 'Balance'\n", csv.c_str(), plot::BALANCE);

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
