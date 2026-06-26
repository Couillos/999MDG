#ifndef MARTINGALE_DATA_SYMBOL_INFO_H
#define MARTINGALE_DATA_SYMBOL_INFO_H

#include <string>
#include <vector>

namespace martingale {

/// Trading symbol information from the Binance exchange info endpoint.
struct SymbolInfo {
    std::string symbol;
    double min_qty;
    double step_size;
    double min_notional;
    int price_decimals;
};

/// Fetches symbol info, caching results locally.
std::vector<SymbolInfo> fetch_symbol_info(const std::string& cache_dir);

}  // namespace martingale

#endif  // MARTINGALE_DATA_SYMBOL_INFO_H
