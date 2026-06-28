#include "binance_client.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <simdjson.h>
#include <string_view>
#include <thread>

namespace powermdg {
namespace {

/// Base URL for the Binance REST API.
constexpr char BASE_URL[] = "https://api.binance.com";

/// Global CURL initialization — must be called once before any curl_easy_init.
/// Fix for audit issue D2: previously every BinanceClient instance called
/// curl_easy_init() without curl_global_init(), which is thread-unsafe and
/// UB when the optimizer spawns N worker threads that each create a client.
struct CurlGlobalInit {
    CurlGlobalInit()  { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalInit() { curl_global_cleanup(); }
};

CurlGlobalInit g_curl_init{};

/// CURL write callback: appends received data to a std::string.
size_t curl_write(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    auto const n = size * nmemb;
    buf->append(static_cast<char*>(ptr), n);
    return n;
}

/// Parses a decimal string returned by Binance into a double.
double parse_decimal(std::string_view sv) {
    std::string tmp(sv);
    return std::strtod(tmp.c_str(), nullptr);
}

/// Current monotonic time in milliseconds.
int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/// Sleeps for the given number of milliseconds.
void sleep_ms(int64_t ms) {
    if (ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
}

}  // anonymous namespace

BinanceClient::BinanceClient() : last_req_ms_(0), req_count_(0) {}

BinanceClient::~BinanceClient() = default;

void BinanceClient::throttle() {
    auto const now = now_ms();
    auto const elapsed = now - last_req_ms_;

    // Reset counter every 60 s window
    if (elapsed > 60000) {
        req_count_ = 0;
    }

    // Enforce 20 req/s (50 ms spacing)
    if (elapsed < 50) {
        sleep_ms(50 - elapsed);
    }

    // Enforce 1200 req/min
    if (req_count_ >= 1200) {
        auto const wait = 60000 - elapsed;
        if (wait > 0) {
            sleep_ms(wait);
        }
        req_count_ = 0;
    }

    last_req_ms_ = now_ms();
    ++req_count_;
}

std::optional<std::string> BinanceClient::http_get(const std::string& url) {
    throttle();

    auto* curl = curl_easy_init();
    if (!curl) {
        return std::nullopt;
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "powermdg/1.0");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::fprintf(stderr, "Warning: CURL error %d (%s) on %s\n",
                     static_cast<int>(res), curl_easy_strerror(res), url.c_str());
        return std::nullopt;
    }
    if (http_code != 200) {
        std::fprintf(stderr, "Warning: HTTP %ld on %s\n", http_code, url.c_str());
        return std::nullopt;
    }

    return response;
}

std::optional<std::vector<Candle>> BinanceClient::fetch_klines(
    const std::string& symbol,
    const std::string& interval,
    int64_t start_ms,
    int64_t end_ms)
{
    std::vector<Candle> all;
    int64_t current_start = start_ms;

    while (current_start < end_ms) {
        // Request up to 1000 candles (1000 minutes at 1m)
        auto const limit = 1000;

        std::string url = BASE_URL;
        url += "/api/v3/klines?symbol=" + symbol;
        url += "&interval=" + interval;
        url += "&startTime=" + std::to_string(current_start);
        url += "&endTime=" + std::to_string(end_ms);
        url += "&limit=" + std::to_string(limit);

        auto resp = http_get(url);
        if (!resp) {
            return std::nullopt;
        }

        // Parse JSON array with simdjson
        simdjson::padded_string json(resp.value());
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc;
        if (parser.iterate(json).get(doc) != simdjson::SUCCESS) {
            return std::nullopt;
        }

        simdjson::ondemand::array arr;
        if (doc.get_array().get(arr) != simdjson::SUCCESS) {
            return std::nullopt;
        }

        int count = 0;
        for (auto elem : arr) {
            simdjson::ondemand::array kline;
            if (elem.get_array().get(kline) != simdjson::SUCCESS) {
                break;
            }

            Candle c{};
            int kidx = 0;
            for (auto kval : kline) {
                switch (kidx) {
                case 0: {
                    int64_t ts = 0;
                    if (kval.get_int64().get(ts) != simdjson::SUCCESS) { ts = 0; }
                    c.timestamp = ts;
                    break;
                }
                case 1: {
                    std::string_view sv{};
                    if (kval.get_string().get(sv) == simdjson::SUCCESS) {
                        c.open = parse_decimal(sv);
                    }
                    break;
                }
                case 2: {
                    std::string_view sv{};
                    if (kval.get_string().get(sv) == simdjson::SUCCESS) {
                        c.high = parse_decimal(sv);
                    }
                    break;
                }
                case 3: {
                    std::string_view sv{};
                    if (kval.get_string().get(sv) == simdjson::SUCCESS) {
                        c.low = parse_decimal(sv);
                    }
                    break;
                }
                case 4: {
                    std::string_view sv{};
                    if (kval.get_string().get(sv) == simdjson::SUCCESS) {
                        c.close = parse_decimal(sv);
                    }
                    break;
                }
                case 5: {
                    std::string_view sv{};
                    if (kval.get_string().get(sv) == simdjson::SUCCESS) {
                        c.volume = parse_decimal(sv);
                    }
                    break;
                }
                default:
                    break;
                }
                ++kidx;
            }

            all.push_back(c);
            ++count;
        }

        if (count == 0) {
            break;  // no more data
        }

        // Advance to next batch (last candle's timestamp + 1 ms)
        current_start = all.back().timestamp + 1;
    }

    return all;
}

std::optional<std::string> BinanceClient::fetch_exchange_info() {
    std::string url = BASE_URL;
    url += "/api/v3/exchangeInfo";
    return http_get(url);
}

}  // namespace powermdg
