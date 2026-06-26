#include "tui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ncurses.h>
#include <thread>

namespace martingale {

OptimizerTUI::OptimizerTUI(const std::vector<ScoringMetric>& scoring,
                           const std::map<std::string, Limit>& limits,
                           size_t total_combos)
    : scoring_(scoring), limits_(limits), total_combos_(total_combos) {
    results_.reserve(4096);
    std::thread t([this] { display_loop(); });
    t.detach();
}

OptimizerTUI::~OptimizerTUI() {
    if (!done_) {
        done_ = true;
        cv_.notify_all();
    }
}

void OptimizerTUI::push_result(const RunResult& result) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        results_.push_back(result);
    }
    completed_++;
    cv_.notify_one();
}

void OptimizerTUI::finish() {
    done_ = true;
    cv_.notify_all();
    // Give the display thread time to refresh
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void OptimizerTUI::display_loop() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    timeout(500);

    while (!done_) {
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            abort_ = true;
            break;
        }

        draw_ui();
    }

    // Final refresh
    if (!abort_) {
        draw_ui();
    }

    endwin();

    // Restore stdout line buffering for subsequent output
    std::setvbuf(stdout, nullptr, _IOLBF, 1024);
}

void OptimizerTUI::draw_ui() {
    std::vector<RunResult> sorted;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sorted = results_;
    }

    std::sort(sorted.begin(), sorted.end(),
        [](const RunResult& a, const RunResult& b) {
            return a.score > b.score;
        });

    size_t const n = std::min(size_t{25}, sorted.size());
    size_t const completed = completed_.load();

    erase();

    // Title
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(0, 0, "Martingale Optimizer  |  Combo: %zu/%zu  |  Top 25 Live",
             completed, total_combos_);
    attroff(A_BOLD | COLOR_PAIR(1));

    if (completed == 0) {
        mvprintw(2, 0, "Waiting for results...");
        refresh();
        return;
    }

    // Build column headers
    // Rank | Params | Score | V | scoring_metrics... | limit_metrics...
    int col = 0;
    int const rank_w = 5;
    int const param_w = 28;
    int const score_w = 12;
    int const valid_w = 4;
    int const metric_w = 12;

    int x = 0;
    mvprintw(2, x, "%-*s", rank_w, "Rank");
    x += rank_w;
    mvprintw(2, x, "%-*s", param_w, "Params");
    x += param_w;
    mvprintw(2, x, "%-*s", score_w, "Score");
    x += score_w;
    mvprintw(2, x, "%-*s", valid_w, "Ok");
    x += valid_w;

    for (const auto& sm : scoring_) {
        std::string label = sm.metric.substr(0, 11);
        mvprintw(2, x, "%-*s", metric_w, label.c_str());
        x += metric_w;
    }
    for (const auto& [name, _] : limits_) {
        std::string label = name.substr(0, 11);
        mvprintw(2, x, "%-*s", metric_w, label.c_str());
        x += metric_w;
    }

    // Separator
    mvprintw(3, 0, "%*c", x, ' ');
    for (int i = 0; i < x; ++i) {
        mvaddch(3, i, '-');
    }

    // Data rows (top 25)
    for (size_t i = 0; i < n; ++i) {
        int row = static_cast<int>(i) + 4;
        const auto& r = sorted[i];

        x = 0;
        mvprintw(row, x, "%-*zu", rank_w, i + 1);
        x += rank_w;

        // Params string
        std::string params_str;
        for (const auto& [k, v] : r.params) {
            if (!params_str.empty()) params_str += " ";
            params_str += k.substr(0, 8) + "=";
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.4f", v);
            params_str += buf;
        }
        if (params_str.size() > param_w) params_str = params_str.substr(0, param_w - 1);
        mvprintw(row, x, "%-*s", param_w, params_str.c_str());
        x += param_w;

        mvprintw(row, x, "%-*.4f", score_w, r.score);
        x += score_w;

        attron(r.valid ? COLOR_PAIR(2) : COLOR_PAIR(3));
        mvprintw(row, x, "%-*s", valid_w, r.valid ? "Y" : "N");
        attroff(r.valid ? COLOR_PAIR(2) : COLOR_PAIR(3));
        x += valid_w;

        for (const auto& sm : scoring_) {
            std::string val = metric_value(r.metrics, sm.metric);
            mvprintw(row, x, "%-*s", metric_w, val.c_str());
            x += metric_w;
        }
        for (const auto& [name, _] : limits_) {
            std::string val = metric_value(r.metrics, name);
            mvprintw(row, x, "%-*s", metric_w, val.c_str());
            x += metric_w;
        }
    }

    // Footer
    if (done_) {
        mvprintw(static_cast<int>(n) + 5, 0, " Optimization complete! Press any key to continue.");
    } else {
        mvprintw(static_cast<int>(n) + 5, 0, " Press 'q' to abort optimization.");
    }

    // Color pairs
    if (has_colors()) {
        // Already initialized
    }

    refresh();
}

std::string OptimizerTUI::metric_value(const Metrics& m, const std::string& name) const {
    char buf[16];
    auto get = [&](double v) {
        if (v >= 1000.0 || v <= -1000.0) {
            std::snprintf(buf, sizeof(buf), "%.1f", v);
        } else if (v >= 1.0 || v <= -1.0) {
            std::snprintf(buf, sizeof(buf), "%.4f", v);
        } else {
            std::snprintf(buf, sizeof(buf), "%.6f", v);
        }
        return std::string(buf);
    };
    if (name == "adg_usd") return get(m.adg_usd);
    if (name == "calmar_ratio_usd") return get(m.calmar_ratio_usd);
    if (name == "equity_balance_diff_neg_max_usd") return get(m.equity_balance_diff_neg_max_usd);
    if (name == "equity_balance_diff_neg_mean_usd") return get(m.equity_balance_diff_neg_mean_usd);
    if (name == "expected_shortfall_1pct_usd") return get(m.expected_shortfall_1pct_usd);
    if (name == "gain_usd") return get(m.gain_usd);
    if (name == "gain_per_exposure_long_usd") return get(m.gain_per_exposure_long_usd);
    if (name == "gain_per_exposure_short_usd") return get(m.gain_per_exposure_short_usd);
    if (name == "loss_profit_ratio") return get(m.loss_profit_ratio);
    if (name == "loss_profit_ratio_long") return get(m.loss_profit_ratio_long);
    if (name == "loss_profit_ratio_short") return get(m.loss_profit_ratio_short);
    if (name == "mdg_usd") return get(m.mdg_usd);
    if (name == "omega_ratio_usd") return get(m.omega_ratio_usd);
    if (name == "peak_recovery_hours_equity_usd") return get(m.peak_recovery_hours_equity_usd);
    if (name == "position_held_hours_max") return get(m.position_held_hours_max);
    if (name == "position_held_hours_mean") return get(m.position_held_hours_mean);
    if (name == "position_held_hours_median") return get(m.position_held_hours_median);
    if (name == "position_unchanged_hours_max") return get(m.position_unchanged_hours_max);
    if (name == "positions_held_per_day") return get(m.positions_held_per_day);
    if (name == "sharpe_ratio_usd") return get(m.sharpe_ratio_usd);
    if (name == "sortino_ratio_usd") return get(m.sortino_ratio_usd);
    if (name == "sterling_ratio_usd") return get(m.sterling_ratio_usd);
    if (name == "volume_pct_per_day_avg") return get(m.volume_pct_per_day_avg);
    return get(0.0);
}

} // namespace martingale
