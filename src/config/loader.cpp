#include "loader.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <regex>
#include <simdjson.h>
#include <string>
#include <vector>

namespace martingale {
namespace {

/// Fatal error: prints message and exits.
[[noreturn]] void fatal(const char* msg) {
    std::fprintf(stderr, "Config error: %s\n", msg);
    std::exit(1);
}

/// Reads a required string field or fatals.
std::string req_str(simdjson::ondemand::object obj, const char* name) {
    std::string_view sv;
    if (obj[name].get_string().get(sv)) {
        fatal(name);
    }
    return std::string(sv);
}

/// Reads a required uint64 field or fatals.
uint64_t req_u64(simdjson::ondemand::object obj, const char* name) {
    uint64_t val;
    if (obj[name].get_uint64().get(val)) {
        fatal(name);
    }
    return val;
}

/// Reads a required double field or fatals.
double req_f64(simdjson::ondemand::object obj, const char* name) {
    double val;
    if (obj[name].get_double().get(val)) {
        fatal(name);
    }
    return val;
}

/// Reads a required object field or fatals.
simdjson::ondemand::object req_obj(simdjson::ondemand::object obj, const char* name) {
    simdjson::ondemand::object val;
    if (obj[name].get_object().get(val)) {
        fatal(name);
    }
    return val;
}

/// Reads an optional double, returns default if missing.
double opt_f64(simdjson::ondemand::object obj, const char* name, double def) {
    simdjson::ondemand::value val;
    auto err = obj[name].get(val);
    if (err) {
        return def;
    }
    double result;
    if (val.get_double().get(result)) {
        return def;
    }
    return result;
}

/// Parses the strategy.params block.
StrategyParams parse_strategy(simdjson::ondemand::object strat) {
    StrategyParams sp{};
    sp.entry_ema_period         = static_cast<int>(req_u64(strat, "entry_ema_period"));
    sp.entry_ema_distance_pct   = req_f64(strat, "entry_ema_distance_pct");
    sp.entry_grid_spacing_pct   = req_f64(strat, "entry_grid_spacing_pct");
    sp.initial_qty_pct          = req_f64(strat, "initial_qty_pct");
    sp.double_down_factor       = req_f64(strat, "double_down_factor");
    sp.close_grid_spacing_pct   = req_f64(strat, "close_grid_spacing_pct");
    sp.close_grid_count         = static_cast<int>(req_u64(strat, "close_grid_count"));
    sp.sl_upnl_pct              = req_f64(strat, "sl_upnl_pct");
    sp.n_positions              = static_cast<int>(req_u64(strat, "n_positions"));
    sp.parkinson_volatility_span = static_cast<int>(req_u64(strat, "parkinson_volatility_span"));
    sp.maker_fee_pct            = req_f64(strat, "maker_fee_pct");
    return sp;
}

/// Parses the optional optimize section.
OptimizeConfig parse_optimize(simdjson::ondemand::object opt) {
    OptimizeConfig oc{};

    oc.n_workers = static_cast<int>(opt_f64(opt, "n_workers", 0.0));

    // limits
    simdjson::ondemand::object limits_obj;
    if (!opt["limits"].get_object().get(limits_obj)) {
        for (auto field : limits_obj) {
            std::string_view key;
            if (field.unescaped_key().get(key)) {
                continue;
            }
            simdjson::ondemand::object lim_obj;
            if (field.value().get_object().get(lim_obj)) {
                continue;
            }
            Limit lim{};
            lim.has_min = !lim_obj["min"].get_double().get(lim.min);
            lim.has_max = !lim_obj["max"].get_double().get(lim.max);
            oc.limits[std::string(key)] = lim;
        }
    }

    // scoring
    simdjson::ondemand::array scoring_arr;
    if (!opt["scoring"].get_array().get(scoring_arr)) {
        for (auto elem : scoring_arr) {
            simdjson::ondemand::object so;
            if (elem.get_object().get(so)) {
                continue;
            }
            ScoringMetric sm{};
            sm.metric = req_str(so, "metric");
            sm.weight = req_f64(so, "weight");
            oc.scoring.push_back(sm);
        }
    }

    // bounds
    simdjson::ondemand::object bounds_obj;
    if (!opt["bounds"].get_object().get(bounds_obj)) {
        for (auto field : bounds_obj) {
            std::string_view key;
            if (field.unescaped_key().get(key)) {
                continue;
            }
            simdjson::ondemand::array arr;
            if (field.value().get_array().get(arr)) {
                continue;
            }
            std::array<double, 2> bnd{};
            size_t idx = 0;
            for (auto elem : arr) {
                if (idx >= 2) {
                    break;
                }
                double v;
                if (!elem.get_double().get(v)) {
                    bnd[idx] = v;
                }
                ++idx;
            }
            oc.bounds[std::string(key)] = bnd;
        }
    }

    return oc;
}

/// Parses the output block.
OutputConfig parse_output(simdjson::ondemand::object out) {
    OutputConfig oc{};
    oc.dir = req_str(out, "dir");
    return oc;
}

}  // anonymous namespace

Config load_config(const std::string& path, Mode mode) {
    simdjson::padded_string json_data;
    auto load_err = simdjson::padded_string::load(path).get(json_data);
    if (load_err) {
        std::fprintf(stderr, "Cannot read config file: %s\n", path.c_str());
        std::exit(1);
    }

    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    auto parse_err = parser.iterate(json_data).get(doc);
    if (parse_err) {
        fatal("Invalid JSON syntax");
    }

    simdjson::ondemand::object root;
    auto obj_err = doc.get_object().get(root);
    if (obj_err) {
        fatal("Root must be a JSON object");
    }

    Config cfg{};
    cfg.mode = mode;

    // symbols
    {
        simdjson::ondemand::array arr;
        if (root["symbols"].get_array().get(arr)) {
            fatal("symbols must be an array");
        }
        for (auto elem : arr) {
            std::string_view sv;
            if (elem.get_string().get(sv)) {
                fatal("symbol element must be a string");
            }
            cfg.symbols.push_back(std::string(sv));
        }
        if (cfg.symbols.empty()) {
            fatal("At least one symbol is required");
        }
        for (const auto& sym : cfg.symbols) {
            if (!std::regex_match(sym, std::regex("^[A-Z]{2,10}USDT$"))) {
                std::fprintf(stderr, "Config error: invalid symbol '%s' (must match ^[A-Z]{2,10}USDT$)\n", sym.c_str());
                std::exit(1);
            }
        }
    }

    cfg.timeframe = req_str(root, "timeframe");
    {
        static const char* valid_tfs[] = {"1m","5m","15m","30m","1h","4h","12h","1d"};
        bool ok = false;
        for (auto tf : valid_tfs) { if (cfg.timeframe == tf) { ok = true; break; } }
        if (!ok) {
            std::fprintf(stderr, "Config error: invalid timeframe '%s'\n", cfg.timeframe.c_str());
            std::exit(1);
        }
    }

    cfg.date_from = req_str(root, "date_from");
    cfg.date_to   = req_str(root, "date_to");
    if (!std::regex_match(cfg.date_from, std::regex("^\\d{4}-\\d{2}-\\d{2}$")) ||
        !std::regex_match(cfg.date_to,   std::regex("^\\d{4}-\\d{2}-\\d{2}$"))) {
        fatal("date_from and date_to must be YYYY-MM-DD");
    }

    cfg.initial_balance_usd   = req_f64(root, "initial_balance_usd");
    if (cfg.initial_balance_usd < 1.0) fatal("initial_balance_usd must be >= 1");
    cfg.total_wallet_exposure = req_f64(root, "total_wallet_exposure");
    if (cfg.total_wallet_exposure < 0.1) fatal("total_wallet_exposure must be >= 0.1");

    // strategy
    {
        auto strat_obj = req_obj(root, "strategy");
        cfg.strategy = parse_strategy(strat_obj);
        // validate strategy param ranges
        if (cfg.strategy.entry_ema_period < 2) fatal("entry_ema_period must be >= 2");
        if (cfg.strategy.entry_ema_distance_pct < 0.0) fatal("entry_ema_distance_pct must be >= 0");
        if (cfg.strategy.entry_grid_spacing_pct < 0.0) fatal("entry_grid_spacing_pct must be >= 0");
        if (cfg.strategy.initial_qty_pct < 0.0 || cfg.strategy.initial_qty_pct > 1.0) fatal("initial_qty_pct must be in [0, 1]");
        if (cfg.strategy.double_down_factor < 0.0) fatal("double_down_factor must be >= 0");
        if (cfg.strategy.close_grid_spacing_pct < 0.0) fatal("close_grid_spacing_pct must be >= 0");
        if (cfg.strategy.close_grid_count < 1) fatal("close_grid_count must be >= 1");
        if (cfg.strategy.sl_upnl_pct > 0.0) fatal("sl_upnl_pct must be <= 0");
        if (cfg.strategy.n_positions < 1) fatal("n_positions must be >= 1");
        if (cfg.strategy.parkinson_volatility_span < 2) fatal("parkinson_volatility_span must be >= 2");
        if (cfg.strategy.maker_fee_pct < 0.0 || cfg.strategy.maker_fee_pct > 1.0) fatal("maker_fee_pct must be in [0, 1]");
    }

    // optimize (only parse in OPTIMIZE mode to avoid wasted work)
    if (mode == Mode::OPTIMIZE) {
        simdjson::ondemand::object opt_obj;
        if (!root["optimize"].get_object().get(opt_obj)) {
            cfg.optimize = parse_optimize(opt_obj);
        }
    }

    // output
    {
        auto out_obj = req_obj(root, "output");
        cfg.output = parse_output(out_obj);
    }

    // computed
    int const a = cfg.strategy.entry_ema_period;
    int const b = cfg.strategy.parkinson_volatility_span;
    cfg.warmup_candles = (a > b) ? a : b;

    return cfg;
}

}  // namespace martingale
