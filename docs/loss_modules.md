# Loss Algorithm Module

Controls **when to force-close** a position. Multiple methods act as OR — any can trigger a close.

## Config

```json
"loss_algo": {
    "z_stop": { "z_stop_threshold": 3.5 },
    "atr_stop": { "atr_period": 14, "atr_stop_mult": 2.0 },
    "time_stop": { "time_stop_hours": 4.0 }
}
```

If `loss_algo` is not specified, defaults to `legacy_stop_loss` + `legacy_unstuck`.

## Methods

### `legacy_stop_loss` — UPnL stop loss (backward compat)
Exits when UPnL ≤ `sl_upnl_pct`. Uses `sl_upnl_pct` from strategy level.

### `legacy_unstuck` — Time-based unstuck (backward compat)
Progressive close after `time_based_unstuck_age` hours. Uses strategy-level params.

### `z_stop` — Z-score stop
Exits when |Z| > threshold. Z = (close − VWAP) / stdev, computed from candle series.

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `z_stop_threshold` | decimal | \|Z\| exit threshold | `3.5` |

### `atr_stop` — ATR-based stop
Exits when price moves N × ATR against entry. ATR computed from HTF candles.

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `atr_period` | integer | ATR period | `14` |
| `atr_stop_mult` | decimal | ATR multiplier | `2.0` |

### `time_stop` — Time-based stop
Exits after N hours if TP1 hasn't fired.

| Parameter | Type | Description | Example |
|-----------|------|-------------|---------|
| `time_stop_hours` | decimal | Max hours without TP1 | `4.0` |

## Optimization

```json
"bounds": {
    "loss_algo": {
        "z_stop": { "z_stop_threshold": [3.0, 5.0, 0.5] },
        "time_stop": { "time_stop_hours": [2.0, 8.0, 1.0] }
    }
}
```
