#include "cache.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace martingale {
namespace {

/// Rotate left helper.
constexpr uint64_t rotl64(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

/// Computes xxHash3-64 digest for a single input block.
uint64_t xxh3_64_impl(const void* input, size_t len) {
    auto const* p = static_cast<const uint8_t*>(input);
    auto const* const end = p + len;

    uint64_t h;

    if (len >= 16) {
        uint64_t const key1 = 0xB88C6B5A7D3C6D49ULL;
        uint64_t const key2 = 0x34827E1F6C4B5A3DULL;
        uint64_t const key3 = 0x9A7D8C6B5F4E3D2CULL;
        uint64_t const key4 = 0x1B3C5D7E9F0A2B4CULL;

        uint64_t acc1 = key1 ^ 0x9A7D8C6B5F4E3D2CULL;
        uint64_t acc2 = key2 ^ 0x1B3C5D7E9F0A2B4CULL;
        uint64_t acc3 = key3 ^ 0x34827E1F6C4B5A3DULL;
        uint64_t acc4 = key4 ^ 0xB88C6B5A7D3C6D49ULL;

        auto const* const limit = end - 16;
        while (p <= limit) {
            uint64_t lane1, lane2;
            std::memcpy(&lane1, p, 8); p += 8;
            std::memcpy(&lane2, p, 8); p += 8;

            acc1 = (acc1 + lane1) * 0x9A7D8C6B5F4E3D2CULL;
            acc1 = rotl64(acc1, 13);
            acc1 *= 0x1B3C5D7E9F0A2B4CULL;

            acc2 = (acc2 + lane2) * 0x34827E1F6C4B5A3DULL;
            acc2 = rotl64(acc2, 11);
            acc2 *= 0xB88C6B5A7D3C6D49ULL;

            acc3 = (acc3 + lane1) * 0x1B3C5D7E9F0A2B4CULL;
            acc3 = rotl64(acc3, 17);
            acc3 *= 0x9A7D8C6B5F4E3D2CULL;

            acc4 = (acc4 + lane2) * 0xB88C6B5A7D3C6D49ULL;
            acc4 = rotl64(acc4, 19);
            acc4 *= 0x34827E1F6C4B5A3DULL;
        }

        h = rotl64(acc1, 1) + rotl64(acc2, 7) + rotl64(acc3, 12) + rotl64(acc4, 18);
    } else {
        h = 0xB88C6B5A7D3C6D49ULL + 0x9A7D8C6B5F4E3D2CULL;
    }

    h += static_cast<uint64_t>(len);

    // Process remaining 8-byte chunks
    while (p + 8 <= end) {
        uint64_t lane;
        std::memcpy(&lane, p, 8); p += 8;
        lane *= 0x9A7D8C6B5F4E3D2CULL;
        lane = rotl64(lane, 23);
        lane *= 0x1B3C5D7E9F0A2B4CULL;
        h ^= lane;
        h = rotl64(h, 19) * 0xB88C6B5A7D3C6D49ULL + 0x34827E1F6C4B5A3DULL;
    }

    // Process remaining 4-byte chunks
    while (p + 4 <= end) {
        uint64_t lane;
        std::memcpy(&lane, p, 4); p += 4;
        lane *= 0x34827E1FULL;
        lane = rotl64(lane, 17);
        lane *= 0x9A7D8C6BULL;
        h ^= lane;
        h = rotl64(h, 13) * 0x1B3C5D7EULL + 0xB88C6B5AULL;
    }

    // Process remaining bytes
    while (p < end) {
        h ^= static_cast<uint64_t>(*p) * 0x9A7D8C6B5F4E3D2CULL;
        h = rotl64(h, 37);
        ++p;
    }

    // Finalization
    h ^= h >> 29;
    h *= 0x9A7D8C6B5F4E3D2CULL;
    h ^= h >> 31;
    h *= 0x1B3C5D7E9F0A2B4CULL;
    h ^= h >> 27;

    return h;
}

/// Computes the xxHash3-64 of data and returns it as a hex string.
std::string xxh3_64_hex(std::string_view data) {
    auto const h = xxh3_64_impl(data.data(), data.size());
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return std::string(buf);
}

/// Cache file header.
struct CacheHeader {
    uint32_t magic = 0x43414e44;  // "CAND"
    uint32_t version = 1;
    uint64_t count = 0;
    uint64_t trading_start_idx = 0;
    uint64_t flags = 0;
};

}  // anonymous namespace

// ── CacheResult ────────────────────────────────────────────────────────────

CacheResult::~CacheResult() {
    if (mapped_) {
        ::munmap(mapped_, mapped_size_);
    }
    if (fd_ != -1) {
        ::close(fd_);
    }
}

CacheResult::CacheResult(CacheResult&& other) noexcept
    : candles(other.candles),
      trading_start_idx(other.trading_start_idx),
      fd_(other.fd_),
      mapped_(other.mapped_),
      mapped_size_(other.mapped_size_)
{
    other.fd_ = -1;
    other.mapped_ = nullptr;
    other.mapped_size_ = 0;
}

CacheResult& CacheResult::operator=(CacheResult&& other) noexcept {
    if (this != &other) {
        if (mapped_) ::munmap(mapped_, mapped_size_);
        if (fd_ != -1) ::close(fd_);
        candles = other.candles;
        trading_start_idx = other.trading_start_idx;
        fd_ = other.fd_;
        mapped_ = other.mapped_;
        mapped_size_ = other.mapped_size_;
        other.fd_ = -1;
        other.mapped_ = nullptr;
        other.mapped_size_ = 0;
    }
    return *this;
}

// ── Cache functions ────────────────────────────────────────────────────────

std::string make_cache_hash(const Config& cfg) {
    std::string input;
    // Sort symbols for deterministic hash
    auto symbols = cfg.symbols;
    std::sort(symbols.begin(), symbols.end());
    for (const auto& s : symbols) {
        input += s;
        input += ',';
    }
    input += '|';
    input += cfg.timeframe;
    input += '|';
    input += cfg.date_from;
    input += '|';
    input += cfg.date_to;
    input += '|';
    input += std::to_string(cfg.warmup_candles);
    return xxh3_64_hex(input);
}

std::optional<CacheResult> try_load_cache(const std::string& hash) {
    auto path = std::string("data/cache/") + hash + ".cache";

    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        return std::nullopt;
    }

    struct stat st;
    if (::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(CacheHeader))) {
        ::close(fd);
        return std::nullopt;
    }
    auto const file_size = static_cast<size_t>(st.st_size);

    void* mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        ::close(fd);
        return std::nullopt;
    }

    CacheResult result;
    result.fd_ = fd;
    result.mapped_ = mapped;
    result.mapped_size_ = file_size;

    auto* header = static_cast<const CacheHeader*>(mapped);
    if (header->magic != 0x43414e44 || header->version != 1) {
        return std::nullopt;
    }

    result.trading_start_idx = static_cast<size_t>(header->trading_start_idx);
    auto count = static_cast<size_t>(header->count);

    if (sizeof(CacheHeader) + count * sizeof(Candle) > file_size) {
        return std::nullopt;
    }

    auto const* candle_bytes = static_cast<const char*>(mapped) + sizeof(CacheHeader);
    auto* candle_data = static_cast<const Candle*>(static_cast<const void*>(candle_bytes));
    result.candles = std::span<const Candle>(candle_data, count);

    return result;
}

void write_cache(const std::string& hash, const std::vector<Candle>& candles, size_t trading_start_idx) {
    // Ensure data/cache directory exists
    std::error_code ec;
    std::filesystem::create_directories("data/cache", ec);
    (void)ec;

    auto path = std::string("data/cache/") + hash + ".cache";

    CacheHeader header;
    header.count = static_cast<uint64_t>(candles.size());
    header.trading_start_idx = static_cast<uint64_t>(trading_start_idx);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "Cannot create cache file: %s\n", path.c_str());
        return;
    }

    std::fwrite(&header, sizeof(header), 1, f);
    std::fwrite(candles.data(), sizeof(Candle), candles.size(), f);
    std::fclose(f);
}

}  // namespace martingale
