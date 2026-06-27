#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
MARTINGALE="$BUILD_DIR/martingale"

if [ ! -f "$MARTINGALE" ]; then
    echo "Building martingale first..."
    cmake -B "$BUILD_DIR" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

echo "Running benchmark..."
# TODO: add Google Benchmark tests
time "$MARTINGALE" backtest "$PROJECT_DIR/configs/benchmark.json" 2>&1
