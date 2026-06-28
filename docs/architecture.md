# Architecture: Composable Module System

## Overview

Modules declare data needs via `DataNeed` bitmask. Engine only computes requested indicators.

## DataNeed Bitmask

```cpp
enum class DataNeed : uint32_t { None=0, Ema=1, RollingStdev=2, CandleSeries=4 };
```

## Per-Module Dependencies

| Module | Ema | RollingStdev | CandleSeries |
|--------|:---:|:------------:|:------------:|
| ema_dist_pct | ✓ | — | — |
| bb_reversion | ✓ | ✓ | — |
| martingale | — | — | — |
| dca_linear | — | — | — |
| simple_grid | — | — | — |
| mean_revert_tp | ✓ | — | — |

## How It Works

1. Engine creates modules via factories
2. Aggregates `data_needs()` from all 3 modules
3. Conditionally allocates/computes EMA, stdev, candle_series
4. Passes lean `ModuleContext` to each module

## Design Principles

- Modules are pure functions (read context, return orders)
- strategy.cpp executes all orders (mutates Position, balance, fees)
- Lazy computation: indicators only computed when needed
- Extensible: new DataNeed bits don't break existing modules
