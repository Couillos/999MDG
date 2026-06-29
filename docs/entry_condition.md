# Entry Condition Module

Controls **when** to open a position. Modules act as OR — any can trigger.

## Methods

### `ema_dist_pct` — EMA distance (trend-following)
Enters when `close > EMA × (1 + entry_ema_distance_pct)`.
Params: `entry_ema_period`, `entry_ema_distance_pct`

### `bb_reversion` — Bollinger Band reversion (mean-reversion)
Enters when `close ≤ EMA − bb_std_mult × stdev`.
Params: `entry_ema_period`, `bb_std_mult`, `bb_min_bandwidth_pct`

### `zscore_ou` — Z-score Ornstein-Uhlenbeck (mean-reversion)
Enters when Z ≤ −threshold, where Z = (close − VWAP) / stdev.
VWAP and stdev are pre-computed once per tick and passed via ModuleContext.
ATR regime filter on HTF candles.
Params: `zscore_entry_threshold`, `zscore_vwap_lookback`, `atr_period`, `atr_filter_mult`
DataNeeds: CandleSeries + HtfCandles
