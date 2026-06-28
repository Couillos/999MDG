# Entry Condition Module

The **entry condition** controls **when** a new position is opened. It checks the market and decides whether it's a good time to enter. The entry condition does **not** decide the position size — that's the [entries_algo](entries_algo.md) module.

## Available methods

### `ema_dist_pct` — EMA distance entry (trend-following)

Opens when `close > EMA × (1 + entry_ema_distance_pct)`. Acts as a trend filter.

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `entry_ema_period` | integer ≥ 2 | EMA period | `24` |
| `entry_ema_distance_pct` | decimal ≥ 0 | Min distance above EMA | `0.01` |

### `bb_reversion` — Bollinger Band reversion entry (mean-reversion)

Opens when `close ≤ EMA − bb_std_mult × stdev` (oversold). A bandwidth filter prevents entries in low volatility.

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `entry_ema_period` | integer ≥ 2 | EMA period (also for stdev) | `24` |
| `bb_std_mult` | decimal ≥ 0.5 | Std dev multiplier | `2.0` |
| `bb_min_bandwidth_pct` | decimal ≥ 0 | Min band width | `0.02` |

## Usage in backtest

```json
"entry_condition": {
    "ema_dist_pct": { "entry_ema_period": 24, "entry_ema_distance_pct": 0.01 }
}
```

## Usage in optimization

```json
"bounds": {
    "entry_condition": {
        "ema_dist_pct": { "entry_ema_period": [4, 24, 1], "entry_ema_distance_pct": [0.001, 0.01, 0.0001] }
    }
}
```

The method name in `bounds` must match `strategy`. Format: `[min, max, step]` (step optional).
