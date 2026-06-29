# PowerMDG — Grid Trading Backtest Engine

C++23 backtest/optimization engine for a power MDG grid strategy on Binance futures.
Supports multi-timeframe indicators (HTF candles for ATR, Parkinson volatility).

## Dependencies

- **Conan** (v2) — install via `pip install conan`
- **Clang++** (only supported compiler)
- **CMake** ≥ 3.24
- **gnuplot** (for PNG charts)
- **ncurses** (for real-time TUI during optimization)

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

The binary is `build/src/powermdg`.

## Usage

```
./build/src/powermdg <backtest|optimize> <config.json> [--backtest-best]
```

### Backtest

```bash
./build/src/powermdg backtest configs/template.json
```

### Optimizer (NSGA-II) with live TUI

```bash
./build/src/powermdg optimize configs/template.json
```

A real-time ncurses table shows the top 25 candidates during optimization. Press `q` to abort.

### Optimize + backtest the best candidate

```bash
./build/src/powermdg optimize configs/template.json --backtest-best
```

After optimization, the top-ranked candidate is automatically backtested with full output (charts, CSVs, analysis.json) in a `best/` subfolder.

A single config file (`configs/template.json`) contains both the `strategy` block (for backtest) and the `optimize` block (for grid-search bounds, scoring, and limits). The schema is defined in `configs/schema.json`.

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
| `equity_chart.png` | Equity (green) + Balance (blue dashed) curves over time |
| `exposure_chart.png` | Total capital exposure over time with initial balance reference |
| `pnl_per_symbol.png` | Cumulative PnL per symbol, one colored curve per coin |
| `data/equity_curve.csv` | `timestamp,equity,balance` |
| `data/exposure.csv` | `timestamp,exposure_usd` |
| `data/pnl_symbol.csv` | `timestamp,symbol1,symbol2,...` |

Charts are generated via **gnuplot** with a dark theme. Requires `gnuplot` on the system (see `scripts/install_gnuplot.sh`).

### Optimizer output: `results/optimize/YYYY-MM-DD_HH-MM-SS/`

| File | Contents |
|---|---|
| `results.zst` | All parameter combinations sorted by score descending, zstd-compressed JSON array |
| `best/` | (only with `--backtest-best`) Full backtest output for the #1 candidate |

## Data

Historical candle data is stored compressed in `data/candles/<SYMBOL>/<DATE>.bin.zst`. Synthetic test data for `TESTUSDT` is preloaded. Live data is fetched from Binance on demand.
