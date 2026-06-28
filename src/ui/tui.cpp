#include "tui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ncurses.h>
#include <simdjson.h>
#include <sstream>
#include <thread>
#include <chrono>

namespace martingale {

/// Generate a unique short abbreviation for a parameter name.
std::string param_abbrev(const std::string& name) {
    auto known = [&]() -> std::string {
        if (name == "entry_ema_period") return "ema";
        if (name == "entry_ema_distance_pct") return "ema_dis";
        if (name == "entry_grid_spacing_pct") return "gspc";
        if (name == "initial_qty_pct") return "iqty";
        if (name == "double_down_factor") return "ddf";
        if (name == "close_grid_spacing_pct") return "cgspc";
        if (name == "close_grid_count") return "cgcnt";
        if (name == "sl_upnl_pct") return "sl";
        if (name == "n_positions") return "npos";
        if (name == "parkinson_volatility_span") return "pvol";
        if (name == "maker_fee_pct") return "mfee";
        if (name == "time_based_unstuck_pct") return "tup";
        if (name == "time_based_unstuck_age") return "tua";
        if (name == "total_wallet_exposure") return "twle";
        return "";
    };
    std::string k = known();
    if (!k.empty()) return k;
    auto strip = [&](const std::string& suf) {
        if (name.size() > suf.size() && name.substr(name.size() - suf.size()) == suf)
            return name.substr(0, name.size() - suf.size());
        return name;
    };
    std::string s = strip("_usd");
    s = strip("_pct");
    return s.size() > 8 ? s.substr(0, 8) : s;
}

std::string format_metric(const Metrics& m, const std::string& name) {
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
    if (name == "adg_smoothed") return get(m.adg_smoothed);
    if (name == "adg_usd") return get(m.adg_usd);
    if (name == "calmar_ratio_usd") return get(m.calmar_ratio_usd);
    if (name == "drawdown_worst") return get(m.drawdown_worst);
    if (name == "drawdown_worst_mean_1pct") return get(m.drawdown_worst_mean_1pct);
    if (name == "equity_balance_diff_neg_max_usd") return get(m.equity_balance_diff_neg_max_usd);
    if (name == "equity_balance_diff_neg_mean_usd") return get(m.equity_balance_diff_neg_mean_usd);
    if (name == "expected_shortfall_1pct_usd") return get(m.expected_shortfall_1pct_usd);
    if (name == "gain") return get(m.gain);
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
    if (name == "sterling_ratio") return get(m.sterling_ratio);
    if (name == "volume_pct_per_day_avg") return get(m.volume_pct_per_day_avg);
    return get(0.0);
}

void set_metric_value(Metrics& m, const std::string& name, double val) {
    if (name == "adg_smoothed") { m.adg_smoothed = val; return; }
    if (name == "adg_usd") { m.adg_usd = val; return; }
    if (name == "adg_per_exponential_fit_error_usd") { m.adg_per_exponential_fit_error_usd = val; return; }
    if (name == "adg_per_exposure_long_usd") { m.adg_per_exposure_long_usd = val; return; }
    if (name == "adg_per_exposure_short_usd") { m.adg_per_exposure_short_usd = val; return; }
    if (name == "calmar_ratio_usd") { m.calmar_ratio_usd = val; return; }
    if (name == "drawdown_worst") { m.drawdown_worst = val; return; }
    if (name == "drawdown_worst_mean_1pct") { m.drawdown_worst_mean_1pct = val; return; }
    if (name == "entry_initial_balance_pct_long") { m.entry_initial_balance_pct_long = val; return; }
    if (name == "entry_initial_balance_pct_short") { m.entry_initial_balance_pct_short = val; return; }
    if (name == "equity_balance_diff_neg_max_usd") { m.equity_balance_diff_neg_max_usd = val; return; }
    if (name == "equity_balance_diff_neg_mean_usd") { m.equity_balance_diff_neg_mean_usd = val; return; }
    if (name == "equity_balance_diff_pos_max_usd") { m.equity_balance_diff_pos_max_usd = val; return; }
    if (name == "equity_balance_diff_pos_mean_usd") { m.equity_balance_diff_pos_mean_usd = val; return; }
    if (name == "equity_choppiness_usd") { m.equity_choppiness_usd = val; return; }
    if (name == "equity_jerkiness_usd") { m.equity_jerkiness_usd = val; return; }
    if (name == "expected_shortfall_1pct_usd") { m.expected_shortfall_1pct_usd = val; return; }
    if (name == "exponential_fit_error_usd") { m.exponential_fit_error_usd = val; return; }
    if (name == "gain") { m.gain = val; return; }
    if (name == "gain_usd") { m.gain_usd = val; return; }
    if (name == "gain_per_exposure_long_usd") { m.gain_per_exposure_long_usd = val; return; }
    if (name == "gain_per_exposure_short_usd") { m.gain_per_exposure_short_usd = val; return; }
    if (name == "loss_profit_ratio") { m.loss_profit_ratio = val; return; }
    if (name == "loss_profit_ratio_long") { m.loss_profit_ratio_long = val; return; }
    if (name == "loss_profit_ratio_short") { m.loss_profit_ratio_short = val; return; }
    if (name == "mdg_usd") { m.mdg_usd = val; return; }
    if (name == "mdg_per_exponential_fit_error_usd") { m.mdg_per_exponential_fit_error_usd = val; return; }
    if (name == "mdg_per_exposure_long_usd") { m.mdg_per_exposure_long_usd = val; return; }
    if (name == "mdg_per_exposure_short_usd") { m.mdg_per_exposure_short_usd = val; return; }
    if (name == "omega_ratio_usd") { m.omega_ratio_usd = val; return; }
    if (name == "peak_recovery_hours_equity_usd") { m.peak_recovery_hours_equity_usd = val; return; }
    if (name == "position_held_hours_max") { m.position_held_hours_max = val; return; }
    if (name == "position_held_hours_mean") { m.position_held_hours_mean = val; return; }
    if (name == "position_held_hours_median") { m.position_held_hours_median = val; return; }
    if (name == "position_unchanged_hours_max") { m.position_unchanged_hours_max = val; return; }
    if (name == "positions_held_per_day") { m.positions_held_per_day = val; return; }
    if (name == "sharpe_ratio_usd") { m.sharpe_ratio_usd = val; return; }
    if (name == "sortino_ratio_usd") { m.sortino_ratio_usd = val; return; }
    if (name == "sterling_ratio") { m.sterling_ratio = val; return; }
    if (name == "volume_pct_per_day_avg") { m.volume_pct_per_day_avg = val; return; }
}

// ---------------------------------------------------------------------------
// OptimizerTUI (used inline by optimize mode — but NOT for ncurses display.
// We keep the class for API compatibility but optimize mode no longer uses it.)
// ---------------------------------------------------------------------------

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
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_CYAN, COLOR_BLACK);
        init_pair(2, COLOR_GREEN, COLOR_BLACK);
        init_pair(3, COLOR_RED, COLOR_BLACK);
    }
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

    // Filter only Pareto front (rank == 1), sort by crowding distance descending
    sorted.erase(std::remove_if(sorted.begin(), sorted.end(),
        [](const RunResult& r) { return r.rank != 1; }), sorted.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const RunResult& a, const RunResult& b) {
            return a.crowding_distance > b.crowding_distance;
        });

    size_t const completed = completed_.load();
    int const cols = COLS > 0 ? COLS : 80;
    int const max_rows = LINES - 4;

    erase();

    // Title
    attron(A_BOLD | COLOR_PAIR(1));
    mvprintw(0, 0, "Martingale Optimizer  |  Gen: %zu/%zu  |  Pareto Front (%zu cand.)",
             completed, total_combos_, sorted.size());
    attroff(A_BOLD | COLOR_PAIR(1));

    if (completed == 0 || sorted.empty()) {
        mvprintw(2, 0, "Waiting for results...");
        refresh();
        return;
    }

    auto fmt_val = [](double v) -> std::string {
        char buf[16];
        if (v >= 10000.0 || v <= -10000.0) {
            std::snprintf(buf, sizeof(buf), "%.1f", v);
        } else if (v >= 100.0 || v <= -100.0) {
            std::snprintf(buf, sizeof(buf), "%.2f", v);
        } else if (v >= 1.0 || v <= -1.0) {
            std::snprintf(buf, sizeof(buf), "%.4f", v);
        } else {
            std::snprintf(buf, sizeof(buf), "%.6f", v);
        }
        return std::string(buf);
    };

    int cur_row = 2;
    int const rank_w = 4;

    for (size_t ci = 0; ci < sorted.size() && cur_row < max_rows - 1; ++ci) {
        const auto& r = sorted[ci];

        std::vector<std::string> parts;
        for (const auto& sm : scoring_) {
            parts.push_back(param_abbrev(sm.metric) + ":" +
                            metric_value(r.metrics, sm.metric));
        }
        for (const auto& [name, _] : limits_) {
            parts.push_back(param_abbrev(name) + ":" +
                            metric_value(r.metrics, name));
        }
        parts.push_back("cv:" + fmt_val(r.constraint_violation));

        int const indent = rank_w + 1;
        int avail = cols - indent - 1;

        attron(COLOR_PAIR(2));
        mvprintw(cur_row, 0, "%-*d", rank_w, static_cast<int>(ci + 1));
        attroff(COLOR_PAIR(2));
        int used = indent;
        int idx = 0;
        for (; idx < (int)parts.size(); ++idx) {
            int need = (int)parts[idx].size() + 1;
            if (used + need >= avail) break;
            mvprintw(cur_row, used, "%s ", parts[idx].c_str());
            used += need;
        }
        if (idx < (int)parts.size() && cur_row < max_rows - 2) {
            ++cur_row;
            mvprintw(cur_row, 0, "%*c", indent, ' ');
            used = indent;
            for (; idx < (int)parts.size(); ++idx) {
                int need = (int)parts[idx].size() + 1;
                if (used + need >= avail) break;
                mvprintw(cur_row, used, "%s ", parts[idx].c_str());
                used += need;
            }
        }
        ++cur_row;
    }

    // Footer
    if (done_) {
        mvprintw(LINES - 1, 0, " Optimization complete! Press any key to continue.");
    } else {
        mvprintw(LINES - 1, 0, " Press 'q' to abort optimization.");
    }

    refresh();
}

std::string OptimizerTUI::metric_value(const Metrics& m, const std::string& name) const {
    return format_metric(m, name);
}

// ---------------------------------------------------------------------------
// run_watch_tui — standalone --tui mode: watches a live state JSON file
// written by a running optimization and displays the Pareto front.
// ---------------------------------------------------------------------------

void run_watch_tui(const std::string& state_path) {
    initscr();
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_CYAN, COLOR_BLACK);
        init_pair(2, COLOR_GREEN, COLOR_BLACK);
        init_pair(3, COLOR_RED, COLOR_BLACK);
    }
    cbreak();
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    timeout(500);

    bool done = false;
    bool first = true;

    while (!done) {
        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;

        // Read and parse state file
        simdjson::padded_string json_data;
        auto load_err = simdjson::padded_string::load(state_path).get(json_data);
        if (load_err) {
            if (first) {
                erase();
                mvprintw(0, 0, "Waiting for optimization to start...");
                mvprintw(2, 0, "State file: %s", state_path.c_str());
                refresh();
                first = false;
            } else {
                erase();
                mvprintw(0, 0, "Optimization process ended.");
                refresh();
                std::this_thread::sleep_for(std::chrono::seconds(2));
                break;
            }
            continue;
        }
        first = false;

        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc;
        auto parse_err = parser.iterate(json_data).get(doc);
        if (parse_err) continue;

        simdjson::ondemand::object root;
        if (doc.get_object().get(root)) continue;

        uint64_t completed_u, total_u;
        if (root["completed"].get_uint64().get(completed_u)) continue;
        if (root["total"].get_uint64().get(total_u)) continue;
        size_t completed = static_cast<size_t>(completed_u);
        size_t total = static_cast<size_t>(total_u);

        bool is_done = false;
        bool done_val;
        if (!root["done"].get_bool().get(done_val)) is_done = done_val;

        // Parse scoring
        std::vector<ScoringMetric> scoring;
        simdjson::ondemand::array scoring_arr;
        if (!root["scoring"].get_array().get(scoring_arr)) {
            for (auto elem : scoring_arr) {
                simdjson::ondemand::object so;
                if (elem.get_object().get(so)) continue;
                ScoringMetric sm{};
                std::string_view sv;
                if (!so["metric"].get_string().get(sv)) sm.metric = std::string(sv);
                double w;
                sm.weight = so["weight"].get_double().get(w) ? 1.0 : w;
                std::string_view gsv;
                if (!so["goal"].get_string().get(gsv)) sm.goal = std::string(gsv);
                sm.engine_sign = (sm.goal == "max") ? -1.0 : 1.0;
                scoring.push_back(sm);
            }
        }

        // Parse limits
        std::map<std::string, Limit> limits;
        simdjson::ondemand::object limits_obj;
        if (!root["limits"].get_object().get(limits_obj)) {
            for (auto field : limits_obj) {
                std::string_view key;
                if (field.unescaped_key().get(key)) continue;
                simdjson::ondemand::object lim_obj;
                if (field.value().get_object().get(lim_obj)) continue;
                Limit lim{};
                double v;
                lim.has_min = !lim_obj["min"].get_double().get(v); if (lim.has_min) lim.min = v;
                lim.has_max = !lim_obj["max"].get_double().get(v); if (lim.has_max) lim.max = v;
                limits[std::string(key)] = lim;
            }
        }

        // Parse top results
        std::vector<RunResult> top;
        simdjson::ondemand::array top_arr;
        if (!root["top"].get_array().get(top_arr)) {
            for (auto elem : top_arr) {
                simdjson::ondemand::object ro;
                if (elem.get_object().get(ro)) continue;
                RunResult rr{};
                int64_t rv;
                if (!ro["rank"].get_int64().get(rv)) rr.rank = static_cast<int>(rv);
                double cv;
                if (!ro["constraint_violation"].get_double().get(cv)) rr.constraint_violation = cv;
                simdjson::ondemand::array obj_arr;
                if (!ro["objectives"].get_array().get(obj_arr)) {
                    for (auto oe : obj_arr) {
                        double ov;
                        if (!oe.get_double().get(ov)) rr.objectives.push_back(ov);
                    }
                }

                simdjson::ondemand::object params_obj;
                if (!ro["params"].get_object().get(params_obj)) {
                    for (auto pf : params_obj) {
                        std::string_view pk;
                        if (pf.unescaped_key().get(pk)) continue;
                        double pv;
                        if (!pf.value().get_double().get(pv))
                            rr.params[std::string(pk)] = pv;
                    }
                }

                simdjson::ondemand::object metrics_obj;
                if (!ro["metrics"].get_object().get(metrics_obj)) {
                    for (auto mf : metrics_obj) {
                        std::string_view mk;
                        if (mf.unescaped_key().get(mk)) continue;
                        double mv;
                        if (!mf.value().get_double().get(mv))
                            set_metric_value(rr.metrics, std::string(mk), mv);
                    }
                }

                top.push_back(rr);
            }
        }

        // Render
        erase();

        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(0, 0, "Martingale Optimizer  |  Gen: %zu/%zu  |  %s",
                 completed, total, is_done ? "COMPLETED" : "Watching live state");
        attroff(A_BOLD | COLOR_PAIR(1));

        if (top.empty()) {
            mvprintw(2, 0, "Waiting for results...");
            refresh();
            continue;
        }

        // Filter only Pareto front (rank == 1), sort by crowding distance descending
        top.erase(std::remove_if(top.begin(), top.end(),
            [](const RunResult& r) { return r.rank != 1; }), top.end());
        std::sort(top.begin(), top.end(),
            [](const RunResult& a, const RunResult& b) {
                return a.crowding_distance > b.crowding_distance;
            });

        int const cols = COLS > 0 ? COLS : 80;
        int const max_rows = LINES - 4;
        int cur_row = 2;
        int const rank_w = 4;

        auto fmt_val = [](double v) -> std::string {
            char buf[16];
            if (v >= 10000.0 || v <= -10000.0) {
                std::snprintf(buf, sizeof(buf), "%.1f", v);
            } else if (v >= 100.0 || v <= -100.0) {
                std::snprintf(buf, sizeof(buf), "%.2f", v);
            } else if (v >= 1.0 || v <= -1.0) {
                std::snprintf(buf, sizeof(buf), "%.4f", v);
            } else {
                std::snprintf(buf, sizeof(buf), "%.6f", v);
            }
            return std::string(buf);
        };

        for (size_t ci = 0; ci < top.size() && cur_row < max_rows - 1; ++ci) {
            const auto& r = top[ci];

            std::vector<std::string> parts;
            for (const auto& sm : scoring) {
                parts.push_back(param_abbrev(sm.metric) + ":" +
                                format_metric(r.metrics, sm.metric));
            }
            for (const auto& [name, _] : limits) {
                parts.push_back(param_abbrev(name) + ":" +
                                format_metric(r.metrics, name));
            }
            parts.push_back("cv:" + fmt_val(r.constraint_violation));

            int const indent = rank_w + 1;
            int avail = cols - indent - 1;

            attron(COLOR_PAIR(2));
            mvprintw(cur_row, 0, "%-*d", rank_w, static_cast<int>(ci + 1));
            attroff(COLOR_PAIR(2));
            int used = indent;
            int idx = 0;
            for (; idx < (int)parts.size(); ++idx) {
                int need = (int)parts[idx].size() + 1;
                if (used + need >= avail) break;
                mvprintw(cur_row, used, "%s ", parts[idx].c_str());
                used += need;
            }
            if (idx < (int)parts.size() && cur_row < max_rows - 2) {
                ++cur_row;
                mvprintw(cur_row, 0, "%*c", indent, ' ');
                used = indent;
                for (; idx < (int)parts.size(); ++idx) {
                    int need = (int)parts[idx].size() + 1;
                    if (used + need >= avail) break;
                    mvprintw(cur_row, used, "%s ", parts[idx].c_str());
                    used += need;
                }
            }
            ++cur_row;
        }

        mvprintw(LINES - 1, 0, " %s Press 'q' to quit.",
                 is_done ? "Optimization complete. Results preserved." : "Watching live state.");
        refresh();
    }

    endwin();
    std::setvbuf(stdout, nullptr, _IOLBF, 1024);
}

} // namespace martingale
