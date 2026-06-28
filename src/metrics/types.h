#ifndef POWERMDG_METRICS_TYPES_H
#define POWERMDG_METRICS_TYPES_H

namespace powermdg {

/// All computed performance metrics for a backtest run.
struct Metrics {
    double adg_usd = 0.0;
    double adg_per_exponential_fit_error_usd = 0.0;
    double adg_per_exposure_long_usd = 0.0;
    double adg_per_exposure_short_usd = 0.0;
    double adg_smoothed = 0.0;
    double calmar_ratio_usd = 0.0;
    double drawdown_worst = 0.0;
    double drawdown_worst_mean_1pct = 0.0;
    double entry_initial_balance_pct_long = 0.0;
    double entry_initial_balance_pct_short = 0.0;
    double equity_balance_diff_neg_max_usd = 0.0;
    double equity_balance_diff_neg_mean_usd = 0.0;
    double equity_balance_diff_pos_max_usd = 0.0;
    double equity_balance_diff_pos_mean_usd = 0.0;
    double equity_choppiness_usd = 0.0;
    double equity_jerkiness_usd = 0.0;
    double expected_shortfall_1pct_usd = 0.0;
    double exponential_fit_error_usd = 0.0;
    double gain = 0.0;       // smoothed terminal geometric gain ratio (PassivBot)
    double gain_usd = 0.0;
    double gain_per_exposure_long_usd = 0.0;
    double gain_per_exposure_short_usd = 0.0;
    double loss_profit_ratio = 0.0;
    double loss_profit_ratio_long = 0.0;
    double loss_profit_ratio_short = 0.0;
    double mdg_usd = 0.0;
    double mdg_per_exponential_fit_error_usd = 0.0;
    double mdg_per_exposure_long_usd = 0.0;
    double mdg_per_exposure_short_usd = 0.0;
    double omega_ratio_usd = 0.0;
    double peak_recovery_hours_equity_usd = 0.0;
    double position_held_hours_max = 0.0;
    double position_held_hours_mean = 0.0;
    double position_held_hours_median = 0.0;
    double position_unchanged_hours_max = 0.0;
    double positions_held_per_day = 0.0;
    double sharpe_ratio_usd = 0.0;
    double sortino_ratio_usd = 0.0;
    double sterling_ratio = 0.0;
    double volume_pct_per_day_avg = 0.0;
};

} // namespace powermdg

#endif // POWERMDG_METRICS_TYPES_H
