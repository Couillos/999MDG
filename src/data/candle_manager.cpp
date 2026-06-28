#include "candle_manager.h"
#include "binance_client.h"
#include "cache.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <map>
#include <zstd.h>

namespace powermdg {
namespace {

// ── Date/time utilities ────────────────────────────────────────────────────

struct YMD { int y, m, d; };

/// Days since epoch (1970-01-01) using Howard Hinnant's civil calendar.
int64_t ymd_to_days(int y, unsigned m, unsigned d) {
    y -= static_cast<int>(m <= 2);
    int const era = (y >= 0 ? y : y - 399) / 400;
    unsigned const yoe = static_cast<unsigned>(y - era * 400);
    unsigned const doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    unsigned const doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

/// Converts a day count back to y/m/d.
YMD days_to_ymd(int64_t days) {
    days += 719468;
    int const era = static_cast<int>(days >= 0 ? days : days - 146096) / 146097;
    unsigned const doe = static_cast<unsigned>(days - static_cast<int64_t>(era) * 146097);
    unsigned const yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = static_cast<int>(yoe) + era * 400;
    unsigned const doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned const mp = (5u * doy + 2u) / 153u;
    unsigned const d = doy - (153u * mp + 2u) / 5u + 1u;
    unsigned const m = mp <= 9 ? (mp + 3u) : (mp - 9u);
    y += static_cast<int>(m <= 2);
    return {y, static_cast<int>(m), static_cast<int>(d)};
}

/// Parses "YYYY-MM-DD" to milliseconds since epoch (start of day UTC).
int64_t parse_date_ms(const std::string& date) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
        return 0;
    }
    return ymd_to_days(y, static_cast<unsigned>(m), static_cast<unsigned>(d)) * 86400000;
}

/// Converts a timeframe string to milliseconds.
int64_t timeframe_ms(const std::string& tf) {
    if (tf == "1m")   return 60000;
    if (tf == "5m")   return 300000;
    if (tf == "15m")  return 900000;
    if (tf == "30m")  return 1800000;
    if (tf == "1h")   return 3600000;
    if (tf == "4h")   return 14400000;
    if (tf == "12h")  return 43200000;
    if (tf == "1d")   return 86400000;
    return 60000;
}

/// Returns the start-of-day boundary for a given timestamp.
int64_t day_boundary(int64_t ms) {
    return ms - (ms % 86400000);
}

// ── File helpers ──────────────────────────────────────────────────────────

/// Returns the path for a symbol's daily compressed file, organised by timeframe.
std::string daily_path(const std::string& tf, const std::string& sym, int64_t day_ms) {
    auto const ymd = days_to_ymd(day_ms / 86400000);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "data/candles/%s/%s/%04d-%02d-%02d.bin.zst",
                  tf.c_str(), sym.c_str(), ymd.y, ymd.m, ymd.d);
    return std::string(buf);
}

/// Creates a directory and its parents if needed.
bool ensure_dir(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

/// Columnar-serializes and zstd-compresses candles into a daily file.
bool write_daily_file(const std::string& path, const std::vector<Candle>& candles) {
    auto const count = static_cast<uint32_t>(candles.size());
    if (count == 0) {
        return false;
    }

    // Columnar layout: [count:u32][ts:int64×N][open:double×N][high×N][low×N][close×N][vol×N]
    auto const col_size = static_cast<size_t>(count) * sizeof(int64_t);
    auto const col_dbl = static_cast<size_t>(count) * sizeof(double);
    size_t const raw_size = sizeof(uint32_t) + col_size + 5 * col_dbl;
    std::vector<uint8_t> raw(raw_size);
    size_t off = 0;

    std::memcpy(raw.data() + off, &count, sizeof(count));
    off += sizeof(count);

    auto const ts_off = off; off += col_size;
    auto const op_off = off; off += col_dbl;
    auto const hi_off = off; off += col_dbl;
    auto const lo_off = off; off += col_dbl;
    auto const cl_off = off; off += col_dbl;
    auto const vo_off = off;

    for (uint32_t i = 0; i < count; ++i) {
        auto const& c = candles[i];
        std::memcpy(raw.data() + ts_off + i * sizeof(int64_t), &c.timestamp, sizeof(c.timestamp));
        std::memcpy(raw.data() + op_off + i * sizeof(double),  &c.open,   sizeof(double));
        std::memcpy(raw.data() + hi_off + i * sizeof(double),  &c.high,   sizeof(double));
        std::memcpy(raw.data() + lo_off + i * sizeof(double),  &c.low,    sizeof(double));
        std::memcpy(raw.data() + cl_off + i * sizeof(double),  &c.close,  sizeof(double));
        std::memcpy(raw.data() + vo_off + i * sizeof(double),  &c.volume, sizeof(double));
    }

    // zstd compress
    auto const bound = ZSTD_compressBound(raw_size);
    std::vector<uint8_t> compressed(bound);
    auto const csize = ZSTD_compress(compressed.data(), bound, raw.data(), raw_size, 3);
    if (ZSTD_isError(csize)) {
        return false;
    }

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    bool ok = std::fwrite(compressed.data(), 1, csize, f) == csize;
    std::fclose(f);
    return ok;
}

/// Reads and decompresses a daily file.
std::optional<std::vector<Candle>> read_daily_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return std::nullopt;
    }

    std::fseek(f, 0, SEEK_END);
    auto const csize = static_cast<size_t>(std::ftell(f));
    std::fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> compressed(csize);
    if (std::fread(compressed.data(), 1, csize, f) != csize) {
        std::fclose(f);
        return std::nullopt;
    }
    std::fclose(f);

    auto const raw_size = ZSTD_getFrameContentSize(compressed.data(), csize);
    if (raw_size == ZSTD_CONTENTSIZE_ERROR || raw_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        return std::nullopt;
    }

    std::vector<uint8_t> raw(raw_size);
    auto const dsize = ZSTD_decompress(raw.data(), raw_size, compressed.data(), csize);
    if (ZSTD_isError(dsize) || dsize != raw_size) {
        return std::nullopt;
    }

    // Parse columnar
    size_t off = 0;
    uint32_t count = 0;
    std::memcpy(&count, raw.data() + off, sizeof(count));
    off += sizeof(count);
    if (count == 0) {
        return std::vector<Candle>{};
    }

    // Fix for audit issue D3: validate count against raw_size to prevent
    // heap over-read on truncated/corrupt files.
    auto const col_ts = static_cast<size_t>(count) * sizeof(int64_t);
    auto const col_dbl = static_cast<size_t>(count) * sizeof(double);
    size_t const expected = sizeof(uint32_t) + col_ts + 5 * col_dbl;
    if (expected > raw_size) {
        std::fprintf(stderr, "Warning: corrupt daily file %s (count=%u, expected=%llu, raw=%llu)\n",
                     path.c_str(), count,
                     static_cast<unsigned long long>(expected),
                     static_cast<unsigned long long>(raw_size));
        return std::nullopt;
    }

    auto const ts_off = off; off += col_ts;
    auto const op_off = off; off += col_dbl;
    auto const hi_off = off; off += col_dbl;
    auto const lo_off = off; off += col_dbl;
    auto const cl_off = off; off += col_dbl;
    auto const vo_off = off;

    std::vector<Candle> candles(count);
    for (uint32_t i = 0; i < count; ++i) {
        std::memcpy(&candles[i].timestamp, raw.data() + ts_off + i * sizeof(int64_t), sizeof(int64_t));
        std::memcpy(&candles[i].open,   raw.data() + op_off + i * sizeof(double), sizeof(double));
        std::memcpy(&candles[i].high,   raw.data() + hi_off + i * sizeof(double), sizeof(double));
        std::memcpy(&candles[i].low,    raw.data() + lo_off + i * sizeof(double), sizeof(double));
        std::memcpy(&candles[i].close,  raw.data() + cl_off + i * sizeof(double), sizeof(double));
        std::memcpy(&candles[i].volume, raw.data() + vo_off + i * sizeof(double), sizeof(double));
    }

    return candles;
}

// ── INDEX.bin ──────────────────────────────────────────────────────────────

/// Scans the timeframe-specific candle directory and writes INDEX.bin.
void build_index(const std::string& tf, const std::vector<std::string>& symbols) {
    auto const index_path = std::string("data/candles/") + tf + "/INDEX.bin";
    FILE* f = std::fopen(index_path.c_str(), "wb");
    if (!f) {
        return;
    }

    auto const nb_sym = static_cast<uint32_t>(symbols.size());
    std::fwrite(&nb_sym, sizeof(nb_sym), 1, f);

    for (const auto& sym : symbols) {
        auto const slen = static_cast<uint8_t>(std::min(sym.size(), size_t{255}));
        std::fwrite(&slen, sizeof(slen), 1, f);
        std::fwrite(sym.data(), 1, slen, f);

        auto const sym_dir = std::string("data/candles/") + tf + "/" + sym;
        std::vector<std::pair<uint32_t, uint64_t>> files;

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(sym_dir, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto const name = entry.path().filename().string();
            int y = 0, m = 0, d = 0;
            if (std::sscanf(name.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
                auto const days = ymd_to_days(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
                files.emplace_back(static_cast<uint32_t>(days), static_cast<uint64_t>(entry.file_size()));
            }
        }

        auto const nf = static_cast<uint32_t>(files.size());
        std::fwrite(&nf, sizeof(nf), 1, f);
        for (const auto& [epoch, fsize] : files) {
            std::fwrite(&epoch, sizeof(epoch), 1, f);
            std::fwrite(&fsize, sizeof(fsize), 1, f);
        }
    }

    std::fclose(f);
}

}  // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────

LoadedCandles load_candles(const Config& cfg) {
    // 1. Parse dates & timeframe
    auto const date_from_ms = parse_date_ms(cfg.date_from);
    auto const date_to_ms   = parse_date_ms(cfg.date_to);
    auto const tf_ms        = timeframe_ms(cfg.timeframe);
    auto data_start_ms = date_from_ms - static_cast<int64_t>(cfg.warmup_candles) * tf_ms;
    // Align to timeframe boundary
    data_start_ms -= data_start_ms % tf_ms;

    ensure_dir("data/candles");
    ensure_dir("data/cache");

    // 2. Download missing data in chunks of 1000×timeframe (Binance limit)
    {
        BinanceClient client;
        auto const chunk_ms = 1000 * tf_ms;

        for (const auto& sym : cfg.symbols) {
            auto const sym_dir = std::string("data/candles/") + cfg.timeframe + "/" + sym;
            ensure_dir(sym_dir);

            auto chunk_start = day_boundary(data_start_ms);
            auto const range_end = day_boundary(date_to_ms) + 86400000;

            while (chunk_start < range_end) {
                auto const chunk_end = std::min(chunk_start + chunk_ms, range_end);

                // Check if any daily file in this chunk is missing
                bool needs_download = false;
                for (auto t = chunk_start; t < chunk_end && !needs_download; t += 86400000) {
                    auto const path = daily_path(cfg.timeframe, sym, t);
                    std::error_code ec_fs;
                    if (!std::filesystem::exists(path, ec_fs)) {
                        needs_download = true;
                    }
                }

                if (needs_download) {
                    auto candles = client.fetch_klines(sym, cfg.timeframe, chunk_start, chunk_end);
                    if (candles && !candles->empty()) {
                        // Split chunk into daily files
                        std::map<int64_t, std::vector<Candle>> by_day;
                        for (auto& c : *candles) {
                            by_day[day_boundary(c.timestamp)].push_back(std::move(c));
                        }
                        for (auto& [day, day_candles] : by_day) {
                            write_daily_file(daily_path(cfg.timeframe, sym, day), day_candles);
                        }
                    } else if (!candles) {
                        std::fprintf(stderr, "Warning: failed to fetch %s %s %ld..%ld\n",
                                     sym.c_str(), cfg.timeframe.c_str(),
                                     static_cast<long>(chunk_start), static_cast<long>(chunk_end));
                    }
                }

                chunk_start = chunk_end;
            }
        }
    }

    // 3. Build INDEX.bin
    build_index(cfg.timeframe, cfg.symbols);

    // 4. Try cache
    auto const hash = make_cache_hash(cfg);
    auto cached = try_load_cache(hash);
    if (cached) {
        // Copy from mmap'd memory into a stable vector
        std::vector<Candle> vec(cached->candles.begin(), cached->candles.end());
        return {std::move(vec), cached->trading_start_idx};
    }

    // 5. Load daily files (already at target timeframe)
    std::vector<Candle> all_candles;

    for (const auto& sym : cfg.symbols) {
        auto day_start = day_boundary(data_start_ms);
        auto const day_end = day_boundary(date_to_ms) + 86400000;

        while (day_start <= day_end) {
            auto const path = daily_path(cfg.timeframe, sym, day_start);
            auto day_candles = read_daily_file(path);
            if (day_candles) {
                for (auto& c : *day_candles) {
                    if (c.timestamp >= data_start_ms && c.timestamp < date_to_ms + 86400000) {
                        all_candles.push_back(std::move(c));
                    }
                }
            }
            day_start += 86400000;
        }
    }

    // trading_start_idx = first warmup_candles of first symbol
    auto const trading_start = static_cast<size_t>(cfg.warmup_candles);
    write_cache(hash, all_candles, trading_start);

    return {std::move(all_candles), trading_start};
}

}  // namespace powermdg
