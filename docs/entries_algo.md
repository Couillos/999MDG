# Entries Algorithm Module

The **entries algorithm** controls **how much** to buy on each entry. It decides
the position size for both the initial entry and any follow-up entries
(double-downs) when the price moves against you.

## How it works

On every candle, for every active symbol, the engine asks the entries algorithm
module: *"What entries should I make?"* The module returns a list of entry
orders (each with a quantity). The engine then executes them: updating the
position, applying fees, and recalculating the average entry price.

The module is called in two situations:
1. **First entry** — no position is open, and the entry condition said "yes"
2. **Double-down** — a position is already open, and the price dropped further

## Available methods

### `martingale` — Grid entries with double-down

Places entries at regular price intervals. When the price drops by
`entry_grid_spacing_pct` from the average entry price, a new entry is placed.
Each subsequent entry is scaled by `double_down_factor`.

**First entry size:** `balance × wallet_exposure / n_positions × initial_qty_pct / price`

**Double-down trigger:** price drops by `entry_grid_spacing_pct` from the
average entry price.

**Double-down size:** previous entry size × `double_down_factor`

**Example:** With `entry_grid_spacing_pct = 0.02` and `double_down_factor = 0.5`,
you enter again when the price drops 2%, with half the quantity of the previous
entry. With `double_down_factor = 1.5`, each double-down is 50% larger than the
last (aggressive martingale).

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `entry_grid_spacing_pct` | decimal ≥ 0 | Price drop between entries | `0.02` (= 2%) |
| `double_down_factor` | decimal ≥ 0 | Size multiplier per level | `0.5` (halving) |

**Shared parameters** (defined at the strategy level, not inside the module):

| Parameter | Description |
|-----------|-------------|
| `initial_qty_pct` | Fraction of capital for the first entry (`0.05` = 5%) |
| `n_positions` | Max simultaneous positions |
| `total_wallet_exposure` | Max wallet exposure (root config) |

## Usage in backtest

```json
"strategy": {
    "entries_algo": {
        "martingale": {
            "entry_grid_spacing_pct": 0.02,
            "double_down_factor": 0.5
        }
    },
    "initial_qty_pct": 0.05,
    ...
}
```

## Usage in optimization

```json
"bounds": {
    "entries_algo": {
        "martingale": {
            "entry_grid_spacing_pct": [0.001, 0.05, 0.001],
            "double_down_factor": [0.2, 1.5, 0.1]
        }
    }
}
```

The method name in `bounds` must match the one in `strategy`. You can also
optimize shared parameters at the top level of `bounds`:

```json
"bounds": {
    "initial_qty_pct": [0.01, 0.03, 0.001],
    ...
}
```
