# Closes Algorithm Module

The **closes algorithm** controls **when** and **how much** to sell from an
open position. It decides when to take profit and in what increments.

## How it works

On every candle, for every open position, the engine asks the closes algorithm
module: *"What closes should I make?"* The module returns a list of close
orders (each with a quantity). The engine then executes them: computing the
realized PnL, applying fees, and checking if the position is fully closed.

Closes are processed **before** entries, so a position can be closed and
reopened in the same candle if conditions allow.

## Available methods

### `simple_grid` — Grid take-profit

Closes a fixed fraction of the position at each price level above the entry.
When the price rises by `close_grid_spacing_pct` from the average entry price,
`1 / close_grid_count` of the position is closed.

**Close trigger:** price rises by `k × close_grid_spacing_pct` from entry
(where k = 1, 2, ..., close_grid_count).

**Close size:** `position_qty / close_grid_count` per level.

**Example:** With `close_grid_spacing_pct = 0.01` and `close_grid_count = 2`,
you close 50% of the position when the price is 1% above entry, and the
remaining 50% when it's 2% above.

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `close_grid_spacing_pct` | decimal ≥ 0 | Price rise between close levels | `0.01` (= 1%) |
| `close_grid_count` | integer ≥ 1 | Number of close levels | `2` |

**Shared parameters** (defined at the strategy level):

| Parameter | Description |
|-----------|-------------|
| `sl_upnl_pct` | Stop-loss: closes everything if unrealized PnL drops below this (`-0.1` = -10%) |
| `maker_fee_pct` | Trading fee applied to each close (`0.001` = 0.1%) |
| `time_based_unstuck_pct` | Progressive close after `unstuck_age` hours (0 = disabled) |
| `time_based_unstuck_age` | Hours before unstuck triggers |

## Usage in backtest

```json
"strategy": {
    "closes_algo": {
        "simple_grid": {
            "close_grid_spacing_pct": 0.01,
            "close_grid_count": 2
        }
    },
    "sl_upnl_pct": -0.1,
    ...
}
```

## Usage in optimization

```json
"bounds": {
    "closes_algo": {
        "simple_grid": {
            "close_grid_spacing_pct": [0.001, 0.03, 0.001],
            "close_grid_count": [1, 5, 1]
        }
    }
}
```

The method name in `bounds` must match the one in `strategy`. You can also
optimize shared parameters at the top level of `bounds`:

```json
"bounds": {
    "sl_upnl_pct": [-0.5, -0.001, 0.01],
    "time_based_unstuck_pct": [0.15, 0.3, 0.05],
    ...
}
```
