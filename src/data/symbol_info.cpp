#include "symbol_info.h"
#include "binance_client.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <simdjson.h>
#include <sys/stat.h>

namespace powermdg {
namespace {

/// Checks whether a file is older than 24 hours.
bool is_older_than_24h(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        return true;
    }
    auto const now = std::chrono::system_clock::now();
    auto const mtime = std::chrono::system_clock::from_time_t(st.st_mtime);
    auto const age = std::chrono::duration_cast<std::chrono::hours>(now - mtime).count();
    return age >= 24;
}

}  // anonymous namespace

std::vector<SymbolInfo> fetch_symbol_info(const std::string& cache_dir) {
    auto const cache_path = cache_dir + "/SYMBOLS.bin";

    // Try loading from cache if it exists and is < 24 h old
    if (!is_older_than_24h(cache_path)) {
        FILE* f = std::fopen(cache_path.c_str(), "rb");
        if (f) {
            uint32_t count = 0;
            if (std::fread(&count, sizeof(count), 1, f) == 1 && count > 0) {
                std::vector<SymbolInfo> infos;
                infos.reserve(count);
                bool ok = true;
                for (uint32_t i = 0; i < count && ok; ++i) {
                    SymbolInfo si{};
                    uint8_t slen = 0;
                    if (std::fread(&slen, sizeof(slen), 1, f) != 1) { ok = false; break; }
                    si.symbol.resize(slen);
                    if (std::fread(si.symbol.data(), 1, slen, f) != slen) { ok = false; break; }
                    if (std::fread(&si.min_qty, sizeof(si.min_qty), 1, f) != 1) { ok = false; break; }
                    if (std::fread(&si.step_size, sizeof(si.step_size), 1, f) != 1) { ok = false; break; }
                    if (std::fread(&si.min_notional, sizeof(si.min_notional), 1, f) != 1) { ok = false; break; }
                    if (std::fread(&si.price_decimals, sizeof(si.price_decimals), 1, f) != 1) { ok = false; break; }
                    if (ok) {
                        infos.push_back(si);
                    }
                }
                if (ok) {
                    std::fclose(f);
                    return infos;
                }
            }
            std::fclose(f);
        }
    }

    // Fetch from API
    BinanceClient client;
    auto json = client.fetch_exchange_info();
    if (!json) {
        return {};
    }

    simdjson::padded_string padded(json.value());
    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) {
        return {};
    }

    simdjson::ondemand::object root;
    if (doc.get_object().get(root) != simdjson::SUCCESS) {
        return {};
    }

    simdjson::ondemand::array symbols_arr;
    if (root["symbols"].get_array().get(symbols_arr) != simdjson::SUCCESS) {
        return {};
    }

    std::vector<SymbolInfo> infos;
    for (auto elem : symbols_arr) {
        simdjson::ondemand::object sym_obj;
        if (elem.get_object().get(sym_obj) != simdjson::SUCCESS) {
            continue;
        }

        std::string_view status;
        if (sym_obj["status"].get_string().get(status) != simdjson::SUCCESS) {
            continue;
        }
        if (status != "TRADING") {
            continue;
        }

        SymbolInfo si{};

        std::string_view sym_sv;
        if (sym_obj["symbol"].get_string().get(sym_sv) != simdjson::SUCCESS) {
            continue;
        }
        si.symbol = std::string(sym_sv);

        // quoteAssetPrecision
        {
            uint64_t prec = 8;
            if (sym_obj["quotePrecision"].get_uint64().get(prec) != simdjson::SUCCESS) {
                prec = 8;
            }
            si.price_decimals = static_cast<int>(prec);
        }

        // Parse filters
        simdjson::ondemand::array filters;
        if (sym_obj["filters"].get_array().get(filters) == simdjson::SUCCESS) {
            for (auto f_elem : filters) {
                simdjson::ondemand::object f_obj;
                if (f_elem.get_object().get(f_obj) != simdjson::SUCCESS) {
                    continue;
                }

                std::string_view ftype;
                if (f_obj["filterType"].get_string().get(ftype) != simdjson::SUCCESS) {
                    continue;
                }

                if (ftype == "LOT_SIZE") {
                    std::string_view sv;
                    if (f_obj["minQty"].get_string().get(sv) == simdjson::SUCCESS) {
                        si.min_qty = std::strtod(std::string(sv).c_str(), nullptr);
                    }
                    if (f_obj["stepSize"].get_string().get(sv) == simdjson::SUCCESS) {
                        si.step_size = std::strtod(std::string(sv).c_str(), nullptr);
                    }
                } else if (ftype == "MIN_NOTIONAL") {
                    std::string_view sv;
                    if (f_obj["minNotional"].get_string().get(sv) == simdjson::SUCCESS) {
                        si.min_notional = std::strtod(std::string(sv).c_str(), nullptr);
                    }
                }
            }
        }

        infos.push_back(si);
    }

    // Write cache
    FILE* f = std::fopen(cache_path.c_str(), "wb");
    if (f) {
        auto const count = static_cast<uint32_t>(infos.size());
        std::fwrite(&count, sizeof(count), 1, f);
        for (const auto& si : infos) {
            auto const slen = static_cast<uint8_t>(std::min(si.symbol.size(), size_t{255}));
            std::fwrite(&slen, sizeof(slen), 1, f);
            std::fwrite(si.symbol.data(), 1, slen, f);
            std::fwrite(&si.min_qty, sizeof(si.min_qty), 1, f);
            std::fwrite(&si.step_size, sizeof(si.step_size), 1, f);
            std::fwrite(&si.min_notional, sizeof(si.min_notional), 1, f);
            std::fwrite(&si.price_decimals, sizeof(si.price_decimals), 1, f);
        }
        std::fclose(f);
    }

    return infos;
}

}  // namespace powermdg
