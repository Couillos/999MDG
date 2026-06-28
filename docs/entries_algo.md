# Entries Algorithm Module

The **entries algorithm** controls **how much** to buy on each entry (initial + double-downs).

## Available methods

### `martingale` — Grid entries with exponential double-down

Each double-down level's size = base × `double_down_factor`^level.

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `entry_grid_spacing_pct` | decimal ≥ 0 | Price drop between entries | `0.02` |
| `double_down_factor` | decimal ≥ 0 | Size multiplier per level | `0.5` |

### `dca_linear` — Linear-escalation DCA entries

Each level's size = base × (1 + `linear_step` × level). Bounded tail risk vs martingale.

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `entry_grid_spacing_pct` | decimal ≥ 0 | Price drop between entries | `0.02` |
| `linear_step` | decimal ≥ 0 | Linear escalation (0 = flat, 1 = +100%/level) | `0.3` |

**Shared:** `initial_qty_pct`, `n_positions`, `total_wallet_exposure` (strategy level).

## Usage in backtest

```json
"entries_algo": { "martingale": { "entry_grid_spacing_pct": 0.02, "double_down_factor": 0.5 } }
```

## Usage in optimization

```json
"bounds": {
    "entries_algo": { "martingale": { "double_down_factor": [0.2, 1.5, 0.1] } }
}
```
