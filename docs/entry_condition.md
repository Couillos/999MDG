# Entry Condition Module

The **entry condition** controls **when** a new position is opened. Think of it
as the "green light" signal — it checks the market and decides whether it's a
good time to enter.

## How it works

On every candle, for every active symbol that doesn't have an open position,
the engine asks the entry condition module: *"Should I open a position now?"*
The module looks at the current price, the EMA, or any other signal it uses,
and answers yes or no.

The entry condition does **not** decide the position size — that's the job of
the [entries_algo](entries_algo.md) module. It only gives the go-ahead.

## Available methods

### `ema_dist_pct` — EMA distance entry

Opens a position when the closing price is above the EMA by a given percentage.
This acts as a trend filter: you only enter when the market is moving up,
avoiding entries in downtrends.

**Entry rule:** `close > EMA × (1 + entry_ema_distance_pct)`

**Example:** With `entry_ema_period = 24` and `entry_ema_distance_pct = 0.01`,
you enter when the price is more than 1% above the 24-period EMA.

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `entry_ema_period` | integer ≥ 2 | Number of candles for the EMA | `24` |
| `entry_ema_distance_pct` | decimal ≥ 0 | Minimum distance above EMA | `0.01` (= 1%) |

## Usage in backtest

```json
"strategy": {
    "entry_condition": {
        "ema_dist_pct": {
            "entry_ema_period": 24,
            "entry_ema_distance_pct": 0.01
        }
    },
    ...
}
```

## Usage in optimization

To optimize this module's parameters, add them under `bounds.entry_condition`:

```json
"bounds": {
    "entry_condition": {
        "ema_dist_pct": {
            "entry_ema_period": [4, 24, 1],
            "entry_ema_distance_pct": [0.001, 0.01, 0.0001]
        }
    }
}
```

The method name in `bounds` must match the one in `strategy`. The optimizer
will search within the given ranges. Format: `[min, max, step]` (step is
optional).

You don't have to optimize every parameter — only list the ones you want
the optimizer to explore. Unlisted parameters keep their `strategy` value.
