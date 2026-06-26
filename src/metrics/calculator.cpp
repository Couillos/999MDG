#include "calculator.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace martingale {
namespace {

/// Extracts one equity value per day (last snapshot of each UTC day).
std::vector<double> daily_equity(const std::vector<EquityPoint>& curve) {
    if (curve.empty()) return {};
    std::vector<double> daily;
    int64_t prev_day = curve[0].timestamp - (curve[0].timestamp % 86400000);
    double last_eq = curve[0].equity;
    for (size_t i = 1; i < curve.size(); ++i) {
        int64_t const day = curve[i].timestamp - (curve[i].timestamp % 86400000);
        if (day != prev_day) {
            daily.push_back(last_eq);
            prev_day = day;
        }
        last_eq = curve[i].equity;
    }
    daily.push_back(last_eq);
    return daily;
}

/// Computes daily returns from daily equity values.
std::vector<double> daily_returns(const std::vector<double>& eq) {
    if (eq.size() < 2) return {};
    std::vector<double> r;
    r.reserve(eq.size() - 1);
    for (size_t i = 1; i < eq.size(); ++i) {
        r.push_back((eq[i] - eq[i - 1]) / eq[i - 1]);
    }
    return r;
}

/// Computes arithmetic mean of a vector.
double mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

/// Computes median of a vector (modifies order).
double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    size_t const n = v.size();
    if (n % 2 == 1) {
        auto mid = v.begin() + n / 2;
        std::nth_element(v.begin(), mid, v.end());
        return *mid;
    }
    auto mid1 = v.begin() + n / 2 - 1;
    auto mid2 = v.begin() + n / 2;
    std::nth_element(v.begin(), mid1, v.end());
    std::nth_element(v.begin(), mid2, v.end());
    return (*mid1 + *mid2) / 2.0;
}

/// Computes population standard deviation.
double stddev(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    double const m = mean(v);
    double sq = 0.0;
    for (auto x : v) { double d = x - m; sq += d * d; }
    return std::sqrt(sq / static_cast<double>(v.size()));
}

/// Computes downside deviation (only negative deviations from mean).
double downside_dev(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    double const m = mean(v);
    double sq = 0.0; size_t cnt = 0;
    for (auto x : v) { double d = x - m; if (d < 0.0) { sq += d * d; ++cnt; } }
    return cnt > 0 ? std::sqrt(sq / static_cast<double>(v.size())) : 0.0;
}

/// Computes maximum drawdown from peak as a fraction.
double max_drawdown(const std::vector<double>& eq) {
    if (eq.size() < 2) return 0.0;
    double peak = eq[0], mdd = 0.0;
    for (size_t i = 1; i < eq.size(); ++i) {
        if (eq[i] > peak) peak = eq[i];
        double const dd = (peak - eq[i]) / peak;
        if (dd > mdd) mdd = dd;
    }
    return mdd;
}

/// Computes average drawdown (daily average of peak-to-current drawdown).
double avg_drawdown(const std::vector<double>& eq) {
    if (eq.size() < 2) return 0.0;
    double peak = eq[0], sum = 0.0; size_t cnt = 0;
    for (size_t i = 1; i < eq.size(); ++i) {
        if (eq[i] > peak) peak = eq[i];
        sum += (peak - eq[i]) / peak; ++cnt;
    }
    return cnt > 0 ? sum / static_cast<double>(cnt) : 0.0;
}

/// Computes simple total return (last/first - 1).
double simple_return(const std::vector<double>& eq) {
    if (eq.size() < 2) return 0.0;
    return (eq.back() - eq.front()) / eq.front();
}

} // anonymous namespace

Metrics compute_metrics(const std::vector<EquityPoint>& equity_curve,
                        const Config& cfg) {
    Metrics m{};
    if (equity_curve.size() < 2) return m;

    auto const d_eq = daily_equity(equity_curve);
    auto const d_ret = daily_returns(d_eq);
    double const n_days = static_cast<double>(d_ret.size());
    if (n_days < 1.0) return m;

    double const mean_ret = mean(d_ret);
    double const std_ret = stddev(d_ret);
    double const med_ret = median(d_ret);

    m.adg_usd = mean_ret;
    m.mdg_usd = med_ret;

    if (std_ret > 0.0) {
        m.sharpe_ratio_usd = (mean_ret / std_ret) * std::sqrt(252.0);
    }

    double const dstd = downside_dev(d_ret);
    if (dstd > 0.0) {
        m.sortino_ratio_usd = (mean_ret / dstd) * std::sqrt(252.0);
    }

    double const total_ret = simple_return(d_eq);
    double const cagr = std::pow(1.0 + total_ret, 252.0 / n_days) - 1.0;
    double const cagr_clamped = std::max(-0.999, std::min(cagr, 100.0));

    m.drawdown_worst = max_drawdown(d_eq);
    if (m.drawdown_worst > 0.0) m.calmar_ratio_usd = cagr_clamped / m.drawdown_worst;

    double const avg_dd = avg_drawdown(d_eq);
    double const st_denom = avg_dd + 0.10;
    if (st_denom > 0.0) m.sterling_ratio_usd = cagr_clamped / st_denom;

    // Omega
    double sum_pos = 0.0, sum_neg = 0.0;
    for (auto r : d_ret) { if (r > 0.0) sum_pos += r; else sum_neg += r; }
    if (sum_neg < 0.0) m.omega_ratio_usd = sum_pos / (-sum_neg);

    // Gain / loss ratios
    double spa = 0.0, sna = 0.0;
    double spl = 0.0, snl = 0.0;
    double sps = 0.0, sns = 0.0;
    for (size_t i = 1; i < equity_curve.size(); ++i) {
        double const ch = equity_curve[i].equity - equity_curve[i - 1].equity;
        double const exp = equity_curve[i].exposure_usd;
        if (ch > 0.0) {
            spa += ch;
            if (exp > 0.0) spl += ch; else sps += ch;
        } else {
            sna += ch;
            if (exp > 0.0) snl += ch; else sns += ch;
        }
    }
    m.gain_usd = spa;
    if (sna < 0.0) m.loss_profit_ratio = spa / (-sna);
    if (snl < 0.0) m.loss_profit_ratio_long = spl / (-snl);
    if (sns < 0.0) m.loss_profit_ratio_short = sps / (-sns);

    // Expected shortfall 1%
    if (d_ret.size() >= 100) {
        auto sorted = d_ret;
        std::sort(sorted.begin(), sorted.end());
        size_t const nw = std::max(size_t{1}, sorted.size() / 100);
        double es = 0.0;
        for (size_t i = 0; i < nw; ++i) es += sorted[i];
        m.expected_shortfall_1pct_usd = es / static_cast<double>(nw);
    }

    // Exponential fit error
    if (d_eq.size() >= 3) {
        double st = 0.0, sl = 0.0, st2 = 0.0, stl = 0.0;
        size_t const ne = d_eq.size();
        for (size_t i = 0; i < ne; ++i) {
            double const t = static_cast<double>(i);
            double const le = std::log(std::max(d_eq[i], 1e-10));
            st += t; sl += le; st2 += t * t; stl += t * le;
        }
        double const fn = static_cast<double>(ne);
        double const denom = fn * st2 - st * st;
        double a = 0.0, b = 0.0;
        if (std::abs(denom) > 1e-15) { a = (fn * stl - st * sl) / denom; b = (sl - a * st) / fn; }
        else { b = sl / fn; }
        double sq_err = 0.0;
        for (size_t i = 0; i < ne; ++i) {
            double const err = d_eq[i] - std::exp(a * static_cast<double>(i) + b);
            sq_err += err * err;
        }
        m.exponential_fit_error_usd = std::sqrt(sq_err / fn);
        if (m.exponential_fit_error_usd > 0.0) {
            m.adg_per_exponential_fit_error_usd = m.adg_usd / m.exponential_fit_error_usd;
            m.mdg_per_exponential_fit_error_usd = m.mdg_usd / m.exponential_fit_error_usd;
        }
    }

    // Equity choppiness
    {
        double cum_abs = 0.0, min_eq = equity_curve[0].equity, max_eq = equity_curve[0].equity;
        for (size_t i = 1; i < equity_curve.size(); ++i) {
            double const diff = equity_curve[i].equity - equity_curve[i - 1].equity;
            cum_abs += std::abs(diff);
            if (equity_curve[i].equity > max_eq) max_eq = equity_curve[i].equity;
            if (equity_curve[i].equity < min_eq) min_eq = equity_curve[i].equity;
        }
        double const range = max_eq - min_eq;
        if (range > 0.0) m.equity_choppiness_usd = cum_abs / range;
    }

    // Equity jerkiness
    if (equity_curve.size() >= 4) {
        double js = 0.0; size_t jc = 0;
        for (size_t i = 3; i < equity_curve.size(); ++i) {
            double const d1 = equity_curve[i].equity - equity_curve[i - 1].equity;
            double const d2 = equity_curve[i - 1].equity - equity_curve[i - 2].equity;
            double const d3 = equity_curve[i - 2].equity - equity_curve[i - 3].equity;
            js += std::abs((d1 - d2) - (d2 - d3)); ++jc;
        }
        if (jc > 0) m.equity_jerkiness_usd = js / static_cast<double>(jc);
    }

    // Equity / balance diff
    {
        double neg_max = 0.0, neg_sum = 0.0, neg_cnt = 0;
        double pos_max = 0.0, pos_sum = 0.0, pos_cnt = 0;
        for (const auto& pt : equity_curve) {
            double const d = pt.equity - pt.balance;
            if (d < 0.0) { if (d < neg_max) neg_max = d; neg_sum += d; ++neg_cnt; }
            else if (d > 0.0) { if (d > pos_max) pos_max = d; pos_sum += d; ++pos_cnt; }
        }
        m.equity_balance_diff_neg_max_usd = neg_max;
        m.equity_balance_diff_neg_mean_usd = neg_cnt > 0 ? neg_sum / neg_cnt : 0.0;
        m.equity_balance_diff_pos_max_usd = pos_max;
        m.equity_balance_diff_pos_mean_usd = pos_cnt > 0 ? pos_sum / pos_cnt : 0.0;
    }

    // Peak recovery hours
    {
        double peak = equity_curve[0].equity, max_rh = 0.0;
        size_t peak_idx = 0;
        for (size_t i = 1; i < equity_curve.size(); ++i) {
            if (equity_curve[i].equity > peak) {
                peak = equity_curve[i].equity; peak_idx = i;
            } else if (equity_curve[i].equity >= peak * 0.999) {
                double const hrs = static_cast<double>(equity_curve[i].timestamp - equity_curve[peak_idx].timestamp) / 3600000.0;
                if (hrs > max_rh) max_rh = hrs;
            }
        }
        m.peak_recovery_hours_equity_usd = max_rh;
    }

    // Position held hours
    {
        double max_h = 0.0, sum_h = 0.0;
        size_t cnt = 0;
        std::vector<double> dur;
        bool in_pos = false;
        size_t pos_start = 0;
        for (size_t i = 0; i < equity_curve.size(); ++i) {
            bool const has_exp = equity_curve[i].exposure_usd > 0.0;
            if (has_exp && !in_pos) { in_pos = true; pos_start = i; }
            else if (!has_exp && in_pos) {
                in_pos = false;
                double const hrs = static_cast<double>(equity_curve[i].timestamp - equity_curve[pos_start].timestamp) / 3600000.0;
                dur.push_back(hrs); sum_h += hrs; ++cnt;
                if (hrs > max_h) max_h = hrs;
            }
        }
        m.position_held_hours_max = max_h;
        if (cnt > 0) { m.position_held_hours_mean = sum_h / static_cast<double>(cnt); m.position_held_hours_median = median(dur); }
    }

    // Position unchanged hours max
    {
        double max_gap = 0.0;
        bool in_gap = false;
        size_t gap_start = 0;
        for (size_t i = 0; i < equity_curve.size(); ++i) {
            bool const has_exp = equity_curve[i].exposure_usd > 0.0;
            if (!has_exp && !in_gap) { in_gap = true; gap_start = i; }
            else if (has_exp && in_gap) {
                in_gap = false;
                double const hrs = static_cast<double>(equity_curve[i].timestamp - equity_curve[gap_start].timestamp) / 3600000.0;
                if (hrs > max_gap) max_gap = hrs;
            }
        }
        m.position_unchanged_hours_max = max_gap;
    }

    // Positions held per day
    {
        size_t periods = 0; bool in_pos = false;
        for (const auto& pt : equity_curve) {
            bool const has_exp = pt.exposure_usd > 0.0;
            if (has_exp && !in_pos) { in_pos = true; ++periods; }
            else if (!has_exp) in_pos = false;
        }
        if (n_days > 0.0) m.positions_held_per_day = static_cast<double>(periods) / n_days;
    }

    // Volume % per day avg
    {
        double total_traded = 0.0;
        for (size_t i = 1; i < equity_curve.size(); ++i) {
            total_traded += std::abs(equity_curve[i].balance - equity_curve[i - 1].balance);
        }
        double const avg_bal = (equity_curve.front().balance + equity_curve.back().balance) / 2.0;
        if (avg_bal > 0.0 && n_days > 0.0) m.volume_pct_per_day_avg = (total_traded / avg_bal) / n_days;
    }

    // ADG/MDG per exposure
    {
        double exp_long = 0.0, exp_short = 0.0;
        size_t cnt_long = 0, cnt_short = 0;
        for (const auto& pt : equity_curve) {
            if (pt.exposure_usd > 0.0) { exp_long += pt.exposure_usd; ++cnt_long; }
            else { exp_short += std::abs(pt.exposure_usd); ++cnt_short; }
        }
        double const el = cnt_long > 0 ? exp_long / static_cast<double>(cnt_long) : 0.0;
        double const es = cnt_short > 0 ? exp_short / static_cast<double>(cnt_short) : 0.0;
        if (el > 0.0) { m.adg_per_exposure_long_usd = mean_ret / el; m.mdg_per_exposure_long_usd = med_ret / el; m.gain_per_exposure_long_usd = m.gain_usd / el; }
        if (es > 0.0) { m.adg_per_exposure_short_usd = mean_ret / es; m.mdg_per_exposure_short_usd = med_ret / es; m.gain_per_exposure_short_usd = m.gain_usd / es; }
    }

    // Entry initial balance %
    {
        double const initial = cfg.initial_balance_usd;
        if (initial > 0.0) {
            double const tg = d_eq.back() - d_eq.front();
            if (tg > 0.0) m.entry_initial_balance_pct_long = tg / initial;
            else m.entry_initial_balance_pct_short = (-tg) / initial;
        }
    }

    return m;
}

} // namespace martingale
