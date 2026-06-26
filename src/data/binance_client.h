#ifndef MARTINGALE_DATA_BINANCE_CLIENT_H
#define MARTINGALE_DATA_BINANCE_CLIENT_H

#include "candle.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace martingale {

/// Minimal HTTP client for the Binance REST API using libcurl.
class BinanceClient {
public:
    BinanceClient();
    ~BinanceClient();

    BinanceClient(const BinanceClient&) = delete;
    BinanceClient& operator=(const BinanceClient&) = delete;

    /// Performs an HTTP GET and returns the response body.
    std::optional<std::string> http_get(const std::string& url);

    /// Fetches klines (candles) for a symbol and time interval.
    std::optional<std::vector<Candle>> fetch_klines(
        const std::string& symbol,
        const std::string& interval,
        int64_t start_ms,
        int64_t end_ms);

    /// Fetches the raw exchange info JSON.
    std::optional<std::string> fetch_exchange_info();

private:
    int64_t last_req_ms_;
    int req_count_;

    /// Respects Binance rate limits (1200 req/min).
    void throttle();
};

}  // namespace martingale

#endif  // MARTINGALE_DATA_BINANCE_CLIENT_H
