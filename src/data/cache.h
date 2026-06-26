#ifndef MARTINGALE_DATA_CACHE_H
#define MARTINGALE_DATA_CACHE_H

#include "candle.h"
#include "config/types.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace martingale {

/// Holds mmap'd cache data; keep alive while using .candles span.
struct CacheResult {
    std::span<const Candle> candles;
    size_t trading_start_idx = 0;

    ~CacheResult();
    CacheResult() = default;
    CacheResult(CacheResult&&) noexcept;
    CacheResult& operator=(CacheResult&&) noexcept;
    CacheResult(const CacheResult&) = delete;
    CacheResult& operator=(const CacheResult&) = delete;

private:
    friend std::optional<CacheResult> try_load_cache(const std::string& hash);
    int fd_ = -1;
    void* mapped_ = nullptr;
    size_t mapped_size_ = 0;
};

/// Generates a deterministic cache key from the configuration.
std::string make_cache_hash(const Config& cfg);

/// Loads cached candles if the cache file exists.
std::optional<CacheResult> try_load_cache(const std::string& hash);

/// Writes candles to the cache file.
void write_cache(const std::string& hash, const std::vector<Candle>& candles, size_t trading_start_idx);

}  // namespace martingale

#endif  // MARTINGALE_DATA_CACHE_H
