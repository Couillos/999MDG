# Martingale — Grid Trading Backtest Engine

C++23 backtest/optimization engine for a martingale grid strategy on Binance futures.

## Dependencies

- **Conan** (v2) — install via `pip install conan`
- **Clang++** (only supported compiler)
- **CMake** ≥ 3.24

## Build

```bash
# Install dependencies
conan install . --output-folder=build --build=missing

# Configure & build (Release)
cmake -B build -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
  -DBUILD_TESTING=ON
cmake --build build -j$(nproc)
```

The binary is `build/src/martingale`.

## Usage

```
./build/src/martingale <backtest|optimize> <config.json>
```

### Backtest

```bash
./build/src/martingale backtest configs/test_backtest.json
```

### Optimizer (grid search)

```bash
./build/src/martingale optimize configs/test_optimize.json
```

See `configs/test_backtest.json` / `configs/test_optimize.json` for example configs. The schema is defined in `configs/schema.json`.

## Tests

```bash
build/tests/test_config
build/tests/test_strategy
build/tests/test_metrics
# or
cd build && ctest
```

## Output

Results go under the directory specified by `output.dir` in the config (default: `results/`).

### Backtest output: `results/YYYY-MM-DD_HH-MM-SS/`

| File | Contents |
|---|---|
| `analysis.json` | Input config + 37 computed metrics (Sharpe, Sortino, Calmar, ADG, MDG, drawdown, recovery, exposure ratios, etc.) |
| `data/equity_curve.csv` | `timestamp,equity,balance` |
| `data/exposure.csv` | `timestamp,exposure_usd` |
| `data/pnl_symbol.csv` | `timestamp,symbol1,symbol2,...` |

### Optimizer output: `results/optimize/YYYY-MM-DD_HH-MM-SS/`

| File | Contents |
|---|---|
| `results.json` | Array of all parameter combinations sorted by score descending, with `params`, `score`, `valid` flag, and key metrics |

## Data

Historical candle data is stored compressed in `data/candles/<SYMBOL>/<DATE>.bin.zst`. Synthetic test data for `TESTUSDT` is preloaded. Live data is fetched from Binance on demand.
