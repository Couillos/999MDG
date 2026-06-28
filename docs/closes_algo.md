# Closes Algorithm Module

The **closes algorithm** controls **when** and **how much** to sell.

## Available methods

### `simple_grid` — Grid take-profit (trend-following)

Closes `1/close_grid_count` of position at each price level above entry.

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `close_grid_spacing_pct` | decimal ≥ 0 | Price rise between levels | `0.01` |
| `close_grid_count` | integer ≥ 1 | Number of levels | `2` |

### `mean_revert_tp` — Mean-reversion take-profit

Closes based on price vs EMA. Partial close at EMA, full close above EMA × (1 + overshoot_pct).

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `revert_close_frac` | decimal 0–1 | Fraction to close at EMA | `0.5` |
| `overshoot_pct` | decimal ≥ 0 | Full close above EMA by this | `0.01` |
| `tp_min_upnl_pct` | decimal | Min UPnL to allow close | `0.0` |

**Shared:** `sl_upnl_pct`, `maker_fee_pct`, `time_based_unstuck_pct`, `time_based_unstuck_age` (strategy level).

## Usage in backtest

```json
"closes_algo": { "simple_grid": { "close_grid_spacing_pct": 0.01, "close_grid_count": 2 } }
```

## Usage in optimization

```json
"bounds": {
    "closes_algo": { "simple_grid": { "close_grid_count": [1, 5, 1] } }
}
```
