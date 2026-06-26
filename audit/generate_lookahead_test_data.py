#!/usr/bin/env python3
"""Generate synthetic OHLCV data in columnar zstd format for testing."""
import struct, os, argparse, math, random
from datetime import datetime, timedelta
import zstandard

def generate(symbol, count, output_dir, base_price=50000.0, volatility=0.02):
    """Generate `count` 1m candles and write daily .bin.zst files."""
    os.makedirs(f"{output_dir}/{symbol}", exist_ok=True)
    candles_by_day = {}
    price = base_price
    for i in range(count):
        ts = int(datetime(2024, 1, 1).timestamp() * 1000) + i * 60000
        day = datetime.utcfromtimestamp(ts / 1000).strftime("%Y-%m-%d")
        change = price * volatility * random.gauss(0, 1)
        o, c = price, price + change
        h, l = max(o, c) * (1 + abs(random.gauss(0, 0.5)) * volatility), min(o, c) * (1 - abs(random.gauss(0, 0.5)) * volatility)
        v = random.uniform(100, 1000)
        candles_by_day.setdefault(day, []).append((ts, o, h, l, c, v))
        price = c
    # Write daily files
    for day, candles in sorted(candles_by_day.items()):
        n = len(candles)
        buf = struct.pack('<I', n)
        for col in range(6):
            for c in candles:
                val = [c[0], c[1], c[2], c[3], c[4], c[5]][col]
                buf += struct.pack('<q' if col == 0 else '<d', val)
        compressed = zstandard.ZstdCompressor(level=3).compress(buf)
        with open(f"{output_dir}/{symbol}/{day}.bin.zst", 'wb') as f:
            f.write(compressed)
    # Write INDEX.bin
    index_path = f"{output_dir}/INDEX.bin"
    existing = {}
    if os.path.exists(index_path):
        with open(index_path, 'rb') as f:
            data = f.read()
    # For simplicity, just overwrite
    with open(index_path, 'wb') as f:
        sym_bytes = symbol.encode()
        f.write(struct.pack('<I', 1))  # nb symbols
        f.write(struct.pack('<B', len(sym_bytes)))
        f.write(sym_bytes)
        days = sorted(candles_by_day.keys())
        f.write(struct.pack('<I', len(days)))
        for day in days:
            epoch = int(datetime.strptime(day, "%Y-%m-%d").timestamp())
            fpath = f"{output_dir}/{symbol}/{day}.bin.zst"
            fsz = os.path.getsize(fpath)
            f.write(struct.pack('<IQ', epoch, fsz))
    print(f"Generated {count} candles for {symbol} in {output_dir}/{symbol}/")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--symbol', default='TESTUSDT')
    parser.add_argument('--count', type=int, default=5000)
    parser.add_argument('--output', default='data/candles')
    args = parser.parse_args()
    generate(args.symbol, args.count, args.output)
