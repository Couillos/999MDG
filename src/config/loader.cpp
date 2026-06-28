#include "loader.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <regex>
#include <simdjson.h>
#include <string>
#include <vector>

namespace powermdg {
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

/// Known methods for each module type.
bool is_valid_entry_condition(const std::string& method) {
    return method == "ema_dist_pct";
}
bool is_valid_entries_algo(const std::string& method) {
    return method == "martingale";
}
bool is_valid_closes_algo(const std::string& method) {
    return method == "simple_grid";
}

/// Returns the set of parameter names that each module method uses.
/// Used to validate that bounds only contain params for the selected modules.
std::vector<std::string> params_for_entry_condition(const std::string& method) {
    if (method == "ema_dist_pct") {
        return {"entry_ema_period", "entry_ema_distance_pct"};
    }
    return {};
}
std::vector<std::string> params_for_entries_algo(const std::string& method) {
    if (method == "martingale") {
        return {"entry_grid_spacing_pct", "double_down_factor"};
    }
    return {};
}
std::vector<std::string> params_for_closes_algo(const std::string& method) {
    if (method == "simple_grid") {
        return {"close_grid_spacing_pct", "close_grid_count"};
    }
    return {};
}

/// Common strategy params (not tied to a specific module) that can be optimized.
std::vector<std::string> common_optimizable_params() {
    return {
        "initial_qty_pct", "sl_upnl_pct", "n_positions",
        "parkinson_volatility_span", "maker_fee_pct",
        "time_based_unstuck_pct", "time_based_unstuck_age",
        "total_wallet_exposure"
    };
}

/// Returns true if `param_name` is a valid optimization target given the
/// selected modules in `sp`. A param is valid if it's:
///   - used by the selected entry_condition method, OR
///   - used by the selected entries_algo method, OR
///   - used by the selected closes_algo method, OR
///   - a common strategy param
bool is_valid_bound_param(const std::string& param_name, const StrategyParams& sp) {
    // Check entry_condition params
    for (auto const& p : params_for_entry_condition(sp.entry_condition_type)) {
        if (p == param_name) return true;
    }
    // Check entries_algo params
    for (auto const& p : params_for_entries_algo(sp.entries_algo_type)) {
        if (p == param_name) return true;
    }
    // Check closes_algo params
    for (auto const& p : params_for_closes_algo(sp.closes_algo_type)) {
        if (p == param_name) return true;
    }
    // Check common params
    for (auto const& p : common_optimizable_params()) {
        if (p == param_name) return true;
    }
    return false;
}

/// Parses a module block like:
///   "module_name": {
///     "method_name": {
///       "param1": value1,
///       "param2": value2
///     }
///   }
/// Sets the method name on sp.*_type and applies the params.
/// Returns the method name (empty if module not present — defaults used).
/// Fatals if the method name is unknown.
std::string parse_module(simdjson::ondemand::object parent, const char* module_name,
                         StrategyParams& sp,
                         bool (*validator)(const std::string&)) {
    simdjson::ondemand::object module_obj;
    if (parent[module_name].get_object().get(module_obj)) {
        return "";  // module not present, use defaults
    }
    // The module_obj has exactly one key = method name, value = params object
    for (auto field : module_obj) {
        std::string_view method_sv;
        if (field.unescaped_key().get(method_sv)) continue;
        std::string method(method_sv);

        // Validate that the method is known for this module type
        if (!validator(method)) {
            std::fprintf(stderr, "Config error: unknown %s method '%s'\n", module_name, method.c_str());
            std::exit(1);
        }

        simdjson::ondemand::object params_obj;
        if (field.value().get_object().get(params_obj)) continue;

        // Parse the params based on which method this is
        if (method == "ema_dist_pct") {
            sp.entry_ema_period       = static_cast<int>(req_u64(params_obj, "entry_ema_period"));
            sp.entry_ema_distance_pct = req_f64(params_obj, "entry_ema_distance_pct");
        } else if (method == "martingale") {
            sp.entry_grid_spacing_pct = req_f64(params_obj, "entry_grid_spacing_pct");
            sp.double_down_factor     = req_f64(params_obj, "double_down_factor");
        } else if (method == "simple_grid") {
            sp.close_grid_spacing_pct = req_f64(params_obj, "close_grid_spacing_pct");
            sp.close_grid_count       = static_cast<int>(req_u64(params_obj, "close_grid_count"));
        }
        return method;
    }
    // Module present but empty — error
    std::fprintf(stderr, "Config error: %s block is empty (must specify exactly one method)\n", module_name);
    std::exit(1);
}

/// Parses the strategy block (new modular schema).
StrategyParams parse_strategy(simdjson::ondemand::object strat) {
    StrategyParams sp{};

    // Parse modular blocks: entry_condition, entries_algo, closes_algo
    // Each must specify exactly one valid method.
    std::string ec = parse_module(strat, "entry_condition", sp, is_valid_entry_condition);
    if (ec.empty()) {
        std::fprintf(stderr, "Config error: strategy.entry_condition is required\n");
        std::exit(1);
    }
    sp.entry_condition_type = ec;

    std::string ea = parse_module(strat, "entries_algo", sp, is_valid_entries_algo);
    if (ea.empty()) {
        std::fprintf(stderr, "Config error: strategy.entries_algo is required\n");
        std::exit(1);
    }
    sp.entries_algo_type = ea;

    std::string ca = parse_module(strat, "closes_algo", sp, is_valid_closes_algo);
    if (ca.empty()) {
        std::fprintf(stderr, "Config error: strategy.closes_algo is required\n");
        std::exit(1);
    }
    sp.closes_algo_type = ca;

    // Common (top-level) strategy params
    sp.initial_qty_pct          = req_f64(strat, "initial_qty_pct");
    sp.sl_upnl_pct              = req_f64(strat, "sl_upnl_pct");
    sp.n_positions              = static_cast<int>(req_u64(strat, "n_positions"));
    sp.parkinson_volatility_span = static_cast<int>(req_u64(strat, "parkinson_volatility_span"));
    sp.maker_fee_pct            = req_f64(strat, "maker_fee_pct");
    sp.time_based_unstuck_pct   = opt_f64(strat, "time_based_unstuck_pct", 0.0);
    sp.time_based_unstuck_age   = static_cast<int>(opt_f64(strat, "time_based_unstuck_age", 0.0));
    return sp;
}

/// Parses the optional optimize section.
/// `sp` is the parsed strategy params — used to validate that each bound key
/// corresponds to a parameter of the selected modules.
OptimizeConfig parse_optimize(simdjson::ondemand::object opt, const StrategyParams& sp) {
    OptimizeConfig oc{};

    oc.n_workers = static_cast<int>(opt_f64(opt, "n_workers", 0.0));

    {
        uint64_t mi;
        oc.max_iterations = opt["max_iterations"].get_uint64().get(mi) ? 0 : static_cast<size_t>(mi);
    }

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
            // Parse goal (optional) and weight (optional)
            std::string_view goal_sv;
            bool has_goal = !so["goal"].get_string().get(goal_sv);
            sm.weight = opt_f64(so, "weight", 1.0);
            if (has_goal) {
                sm.goal = std::string(goal_sv);
                // weight is importance only; abs it
                if (sm.weight < 0) sm.weight = -sm.weight;
            } else {
                // Backward compat: infer goal from weight sign
                // Convention: POSITIVE weight => MAXIMIZE, NEGATIVE weight => MINIMIZE
                sm.goal = (sm.weight < 0) ? "min" : "max";
                sm.weight = std::abs(sm.weight);
            }
            if (sm.weight == 0.0) sm.weight = 1.0;
            sm.engine_sign = (sm.goal == "max") ? -1.0 : 1.0;
            oc.scoring.push_back(sm);
        }
    }

    // ga
    {
        simdjson::ondemand::object ga_obj;
        if (!opt["ga"].get_object().get(ga_obj)) {
            oc.ga.population_size  = static_cast<int>(opt_f64(ga_obj, "population_size", 100.0));
            oc.ga.n_generations    = static_cast<int>(opt_f64(ga_obj, "n_generations", 50.0));
            oc.ga.crossover_prob   = opt_f64(ga_obj, "crossover_prob", 0.8);
            oc.ga.crossover_eta    = opt_f64(ga_obj, "crossover_eta", 15.0);
            oc.ga.mutation_prob    = opt_f64(ga_obj, "mutation_prob", 0.2);
            oc.ga.mutation_eta     = opt_f64(ga_obj, "mutation_eta", 20.0);
            oc.ga.mutation_indpb   = opt_f64(ga_obj, "mutation_indpb", 0.1);
        }
    }

    // bounds — modular structure matching strategy schema:
    //   "bounds": {
    //       "entry_condition": { "ema_dist_pct": { "entry_ema_period": [4, 24, 1], ... } },
    //       "entries_algo":    { "martingale":   { "double_down_factor": [0.2, 1.5, 0.1], ... } },
    //       "closes_algo":     { "simple_grid":  { "close_grid_count": [1, 5, 1], ... } },
    //       "initial_qty_pct": [0.01, 0.03, 0.001],   // common params at top level
    //       ...
    //   }
    // Each module's method must match the one selected in strategy.
    // We flatten everything into oc.bounds (flat map) for the optimizer.
    simdjson::ondemand::object bounds_obj;
    if (!opt["bounds"].get_object().get(bounds_obj)) {
        // Helper: parse a single [lo, hi, step] array
        auto parse_bound_arr = [](simdjson::ondemand::array arr) -> std::array<double, 3> {
            std::array<double, 3> bnd{};
            bnd[2] = 0.0;
            size_t idx = 0;
            for (auto elem : arr) {
                if (idx >= 3) break;
                double v;
                if (!elem.get_double().get(v)) bnd[idx] = v;
                ++idx;
            }
            return bnd;
        };

        for (auto field : bounds_obj) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;
            std::string key_str(key);

            // Check if this is a module block (entry_condition/entries_algo/closes_algo)
            if (key_str == "entry_condition" || key_str == "entries_algo" || key_str == "closes_algo") {
                simdjson::ondemand::object module_obj;
                if (field.value().get_object().get(module_obj)) continue;

                for (auto mod_field : module_obj) {
                    std::string_view method_sv;
                    if (mod_field.unescaped_key().get(method_sv)) continue;
                    std::string method(method_sv);

                    // Validate method matches the strategy selection
                    if (key_str == "entry_condition" && method != sp.entry_condition_type) {
                        std::fprintf(stderr, "Config error: bounds.entry_condition method '%s' does not match strategy.entry_condition '%s'\n",
                            method.c_str(), sp.entry_condition_type.c_str());
                        std::exit(1);
                    }
                    if (key_str == "entries_algo" && method != sp.entries_algo_type) {
                        std::fprintf(stderr, "Config error: bounds.entries_algo method '%s' does not match strategy.entries_algo '%s'\n",
                            method.c_str(), sp.entries_algo_type.c_str());
                        std::exit(1);
                    }
                    if (key_str == "closes_algo" && method != sp.closes_algo_type) {
                        std::fprintf(stderr, "Config error: bounds.closes_algo method '%s' does not match strategy.closes_algo '%s'\n",
                            method.c_str(), sp.closes_algo_type.c_str());
                        std::exit(1);
                    }

                    // Parse the params inside the method
                    simdjson::ondemand::object params_obj;
                    if (mod_field.value().get_object().get(params_obj)) continue;

                    for (auto param_field : params_obj) {
                        std::string_view param_sv;
                        if (param_field.unescaped_key().get(param_sv)) continue;
                        std::string param_name(param_sv);

                        simdjson::ondemand::array arr;
                        if (param_field.value().get_array().get(arr)) continue;

                        std::string full_key = param_name;
                        if (!is_valid_bound_param(full_key, sp)) {
                            std::fprintf(stderr, "Config error: bound '%s' is not a valid parameter for %s.%s\n",
                                full_key.c_str(), key_str.c_str(), method.c_str());
                            std::exit(1);
                        }
                        oc.bounds[full_key] = parse_bound_arr(arr);
                    }
                }
            } else {
                // Top-level common parameter (initial_qty_pct, sl_upnl_pct, etc.)
                simdjson::ondemand::array arr;
                if (field.value().get_array().get(arr)) continue;

                if (!is_valid_bound_param(key_str, sp)) {
                    std::fprintf(stderr,
                        "Config error: bound '%s' does not match any parameter of the selected modules\n",
                        key_str.c_str());
                    std::fprintf(stderr, "  Selected: entry_condition=%s, entries_algo=%s, closes_algo=%s\n",
                        sp.entry_condition_type.c_str(), sp.entries_algo_type.c_str(), sp.closes_algo_type.c_str());
                    std::exit(1);
                }
                oc.bounds[key_str] = parse_bound_arr(arr);
            }
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
            cfg.optimize = parse_optimize(opt_obj, cfg.strategy);
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

}  // namespace powermdg
