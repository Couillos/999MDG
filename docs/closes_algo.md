# Closes Algorithm Module

Controls **when** and **how much** to sell. Multiple modules act as CONCAT (all orders executed).

## Methods

### `simple_grid` — Grid take-profit
Closes 1/N at each price level above entry.
Params: `close_grid_spacing_pct`, `close_grid_count`

### `mean_revert_tp` — Mean-reversion TP
Closes based on price vs EMA. Partial at EMA, full above EMA×(1+overshoot).
Params: `revert_close_frac`, `overshoot_pct`, `tp_min_upnl_pct`

### `graduated_tp` — Graduated Z-score TP
TP1: close tp1_frac when |Z| ≤ tp1_z_threshold.
TP2: close tp2_frac when |Z| ≤ tp2_z_threshold.
TP3: trailing ATR on remaining.
Params: `tp1_z_threshold`, `tp1_frac`, `tp2_z_threshold`, `tp2_frac`, `trailing_atr_mult`
DataNeeds: CandleSeries + HtfCandles
