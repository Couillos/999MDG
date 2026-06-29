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

/// Reads a param that may be either a scalar (e.g. 14) or an array ["1h", 14].
/// If array, stores the timeframe in sp.indicator_timeframes[param_name] and returns the value.
/// If scalar, returns the value as-is (timeframe defaults to base timeframe).
/// Fatals with a clear message if the param does not support timeframes but an array is given.
double read_tf_param(simdjson::ondemand::object obj, const char* name, StrategyParams& sp) {
    simdjson::ondemand::value val;
    if (obj[name].get(val)) {
        std::fprintf(stderr, "Config error: missing required param '%s'\n", name);
        std::exit(1);
    }
    // Try array first
    simdjson::ondemand::array arr;
    if (!val.get_array().get(arr)) {
        if (!supports_timeframe(name)) {
            std::fprintf(stderr, "Config error: parameter '%s' does not support timeframes, "
                "use a plain number instead of [\"tf\", value]\n", name);
            std::exit(1);
        }
        // It's an array: ["1h", value]
        std::string_view tf;
        double v = 0.0;
        bool got_tf = false, got_v = false;
        for (auto elem : arr) {
            if (!got_tf) {
                if (!elem.get_string().get(tf)) {
                    std::string tf_str(tf);
                    if (timeframe_to_minutes(tf_str) <= 0) {
                        std::fprintf(stderr, "Config error: invalid timeframe '%s' for param '%s'\n",
                            tf_str.c_str(), name);
                        std::exit(1);
                    }
                    sp.indicator_timeframes[name] = tf_str;
                    got_tf = true;
                }
            } else if (!got_v) {
                if (!elem.get_double().get(v)) got_v = true;
                // Also try uint64 for integer params
                uint64_t u;
                if (!elem.get_uint64().get(u)) { v = static_cast<double>(u); got_v = true; }
            }
        }
        if (!got_tf) {
            std::fprintf(stderr, "Config error: param '%s' array must start with a timeframe string, e.g. [\"1h\", value]\n", name);
            std::exit(1);
        }
        if (!got_v) {
            std::fprintf(stderr, "Config error: param '%s' array must contain a value after the timeframe, e.g. [\"1h\", value]\n", name);
            std::exit(1);
        }
        return v;
    }
    // Scalar value
    double v;
    if (!val.get_double().get(v)) return v;
    uint64_t u;
    if (!val.get_uint64().get(u)) return static_cast<double>(u);
    std::fprintf(stderr, "Config error: param '%s' must be a number or [timeframe, value] array\n", name);
    std::exit(1);
}

/// Reads a param that may be scalar or ["tf", value], with a default.
double read_tf_param_opt(simdjson::ondemand::object obj, const char* name, StrategyParams& sp, double def) {
    simdjson::ondemand::value val;
    if (obj[name].get(val)) return def;
    simdjson::ondemand::array arr;
    if (!val.get_array().get(arr)) {
        if (!supports_timeframe(name)) {
            std::fprintf(stderr, "Config error: parameter '%s' does not support timeframes, "
                "use a plain number instead of [\"tf\", value]\n", name);
            std::exit(1);
        }
        std::string_view tf;
        double v = def;
        bool got_tf = false, got_v = false;
        for (auto elem : arr) {
            if (!got_tf) {
                if (!elem.get_string().get(tf)) {
                    std::string tf_str(tf);
                    if (timeframe_to_minutes(tf_str) <= 0) {
                        std::fprintf(stderr, "Config error: invalid timeframe '%s' for param '%s'\n",
                            tf_str.c_str(), name);
                        std::exit(1);
                    }
                    sp.indicator_timeframes[name] = tf_str;
                    got_tf = true;
                }
            } else if (!got_v) {
                if (!elem.get_double().get(v)) got_v = true;
                uint64_t u;
                if (!elem.get_uint64().get(u)) { v = static_cast<double>(u); got_v = true; }
            }
        }
        if (!got_tf) {
            std::fprintf(stderr, "Config error: param '%s' array must start with a timeframe string, e.g. [\"1h\", value]\n", name);
            std::exit(1);
        }
        if (!got_v) {
            std::fprintf(stderr, "Config error: param '%s' array must contain a value after the timeframe, e.g. [\"1h\", value]\n", name);
            std::exit(1);
        }
        return v;
    }
    double v;
    if (!val.get_double().get(v)) return v;
    uint64_t u;
    if (!val.get_uint64().get(u)) return static_cast<double>(u);
    return def;
}

/// Known methods for each module type.
bool is_valid_entry_condition(const std::string& method) {
    return method == "ema_dist_pct" || method == "bb_reversion" || method == "zscore_ou";
}
bool is_valid_entries_algo(const std::string& method) {
    return method == "martingale" || method == "dca_linear";
}
bool is_valid_closes_algo(const std::string& method) {
    return method == "simple_grid" || method == "mean_revert_tp" || method == "graduated_tp";
}
bool is_valid_loss_module(const std::string& method) {
    return method == "legacy_stop_loss" || method == "legacy_unstuck" || method == "z_stop" || method == "atr_stop" || method == "time_stop";
}

/// Returns the set of parameter names that each module method uses.
/// Used to validate that bounds only contain params for the selected modules.
std::vector<std::string> params_for_entry_condition(const std::string& method) {
    if (method == "ema_dist_pct") return {"entry_ema_period", "entry_ema_distance_pct"};
    if (method == "bb_reversion") return {"entry_ema_period", "bb_std_mult", "bb_min_bandwidth_pct"};
    if (method == "zscore_ou") return {"zscore_entry_threshold", "zscore_vwap_lookback", "atr_period", "atr_filter_mult"};
    return {};
}
std::vector<std::string> params_for_entries_algo(const std::string& method) {
    if (method == "martingale") {
        return {"entry_grid_spacing_pct", "double_down_factor"};
    }
    if (method == "dca_linear") {
        return {"entry_grid_spacing_pct", "linear_step"};
    }
    return {};
}
std::vector<std::string> params_for_closes_algo(const std::string& method) {
    if (method == "simple_grid") return {"close_grid_spacing_pct", "close_grid_count"};
    if (method == "mean_revert_tp") return {"revert_close_frac", "overshoot_pct", "tp_min_upnl_pct"};
    if (method == "graduated_tp") return {"tp1_z_threshold", "tp1_frac", "tp2_z_threshold", "tp2_frac", "trailing_atr_mult", "zscore_vwap_lookback", "atr_period"};
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
std::vector<std::string> params_for_loss_module(const std::string& method) {
    if (method == "z_stop") return {"z_stop_threshold", "zscore_vwap_lookback"};
    if (method == "atr_stop") return {"atr_period", "atr_stop_mult"};
    if (method == "time_stop") return {"time_stop_hours"};
    return {};
}

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
    // M4: Check loss module params ONLY if the corresponding module is active.
    // A bound on e.g. atr_stop_mult without atr_stop in loss_algo_types would waste
    // a genome dimension on a parameter that has no effect.
    for (auto const& method : sp.loss_algo_types) {
        for (auto const& p : params_for_loss_module(method)) {
            if (p == param_name) return true;
        }
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
        return "";
    }
    for (auto field : module_obj) {
        std::string_view method_sv;
        if (field.unescaped_key().get(method_sv)) continue;
        std::string method(method_sv);
        if (!validator(method)) {
            std::fprintf(stderr, "Config error: unknown %s method '%s'\n", module_name, method.c_str());
            std::exit(1);
        }
        simdjson::ondemand::object params_obj;
        if (field.value().get_object().get(params_obj)) continue;

        if (method == "ema_dist_pct") {
            sp.entry_ema_period       = static_cast<int>(read_tf_param(params_obj, "entry_ema_period", sp));
            sp.entry_ema_distance_pct = req_f64(params_obj, "entry_ema_distance_pct");
        } else if (method == "bb_reversion") {
            sp.entry_ema_period     = static_cast<int>(read_tf_param(params_obj, "entry_ema_period", sp));
            sp.bb_std_mult          = req_f64(params_obj, "bb_std_mult");
            sp.bb_min_bandwidth_pct = req_f64(params_obj, "bb_min_bandwidth_pct");
        } else if (method == "zscore_ou") {
            sp.zscore_entry_threshold = req_f64(params_obj, "zscore_entry_threshold");
            sp.zscore_vwap_lookback   = static_cast<int>(read_tf_param(params_obj, "zscore_vwap_lookback", sp));
            sp.atr_period             = static_cast<int>(read_tf_param(params_obj, "atr_period", sp));
            sp.atr_filter_mult        = req_f64(params_obj, "atr_filter_mult");
        } else if (method == "martingale") {
            sp.entry_grid_spacing_pct = req_f64(params_obj, "entry_grid_spacing_pct");
            sp.double_down_factor     = req_f64(params_obj, "double_down_factor");
        } else if (method == "dca_linear") {
            sp.entry_grid_spacing_pct = req_f64(params_obj, "entry_grid_spacing_pct");
            sp.linear_step            = req_f64(params_obj, "linear_step");
        } else if (method == "simple_grid") {
            sp.close_grid_spacing_pct = req_f64(params_obj, "close_grid_spacing_pct");
            sp.close_grid_count       = static_cast<int>(req_u64(params_obj, "close_grid_count"));
        } else if (method == "mean_revert_tp") {
            sp.revert_close_frac = req_f64(params_obj, "revert_close_frac");
            sp.overshoot_pct     = req_f64(params_obj, "overshoot_pct");
            sp.tp_min_upnl_pct   = req_f64(params_obj, "tp_min_upnl_pct");
        } else if (method == "graduated_tp") {
            sp.tp1_z_threshold   = req_f64(params_obj, "tp1_z_threshold");
            sp.tp1_frac          = req_f64(params_obj, "tp1_frac");
            sp.tp2_z_threshold   = req_f64(params_obj, "tp2_z_threshold");
            sp.tp2_frac          = req_f64(params_obj, "tp2_frac");
            sp.trailing_atr_mult = req_f64(params_obj, "trailing_atr_mult");
        }
        return method;
    }
    std::fprintf(stderr, "Config error: %s block is empty\n", module_name);
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

    // Parse loss_algo (multiple methods, OR logic)
    {
        simdjson::ondemand::object loss_obj;
        if (!strat["loss_algo"].get_object().get(loss_obj)) {
            for (auto field : loss_obj) {
                std::string_view method_sv;
                if (field.unescaped_key().get(method_sv)) continue;
                std::string method(method_sv);
                if (!is_valid_loss_module(method)) {
                    std::fprintf(stderr, "Config error: unknown loss_algo method '%s'\n", method.c_str());
                    std::exit(1);
                }
                sp.loss_algo_types.push_back(method);
                simdjson::ondemand::object params_obj;
                if (field.value().get_object().get(params_obj)) continue;
                if (method == "z_stop") {
                    sp.z_stop_threshold = req_f64(params_obj, "z_stop_threshold");
                } else if (method == "atr_stop") {
                    sp.atr_period = static_cast<int>(read_tf_param(params_obj, "atr_period", sp));
                    sp.atr_stop_mult = req_f64(params_obj, "atr_stop_mult");
                } else if (method == "time_stop") {
                    sp.time_stop_hours = req_f64(params_obj, "time_stop_hours");
                }
            }
        }
    }

    // Common (top-level) strategy params
    sp.initial_qty_pct          = req_f64(strat, "initial_qty_pct");
    sp.sl_upnl_pct              = req_f64(strat, "sl_upnl_pct");
    sp.n_positions              = static_cast<int>(req_u64(strat, "n_positions"));
    sp.parkinson_volatility_span = static_cast<int>(read_tf_param(strat, "parkinson_volatility_span", sp));
    sp.maker_fee_pct            = req_f64(strat, "maker_fee_pct");
    sp.time_based_unstuck_pct   = opt_f64(strat, "time_based_unstuck_pct", 0.0);
    sp.time_based_unstuck_age   = static_cast<int>(opt_f64(strat, "time_based_unstuck_age", 0.0));
    return sp;
}

/// Parses the optional optimize section.
/// `sp` is the parsed strategy params — used to validate that each bound key
/// corresponds to a parameter of the selected modules.
OptimizeConfig parse_optimize(simdjson::ondemand::object opt, StrategyParams& sp) {
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
    //
    // Bound array formats:
    //   [lo, hi, step]              — plain numeric (existing)
    //   ["timeframe", lo, hi, step] — with timeframe prefix for mtf params
    simdjson::ondemand::object bounds_obj;
    if (!opt["bounds"].get_object().get(bounds_obj)) {
        // Helper: parse a single bound array, supporting both plain and timeframe-prefixed formats.
        // Returns a BoundSpec; if a timeframe prefix is found, validates the param supports it
        // and returns the timeframe in spec.timeframe.
        auto parse_bound_arr = [&](simdjson::ondemand::array arr,
                                   const std::string& pname) -> BoundSpec {
            BoundSpec bnd{};
            bnd.step = 0.0;

            // Single pass: detect timeframe prefix, collect numbers
            std::string tf_str;
            double vals[3] = {0.0, 0.0, 0.0};
            size_t num_count = 0;
            size_t elem_idx = 0;

            for (auto elem : arr) {
                // Try string (timeframe prefix, must be first element)
                std::string_view sv;
                if (!elem.get_string().get(sv)) {
                    if (elem_idx != 0) {
                        std::fprintf(stderr, "Config error: bound '%s' has a string at position %zu, "
                            "which is only allowed as the first element (timeframe)\n",
                            pname.c_str(), elem_idx);
                        std::exit(1);
                    }
                    if (!supports_timeframe(pname)) {
                        std::fprintf(stderr, "Config error: bound '%s' does not support timeframes, "
                            "use [lo, hi, step] without a string prefix\n", pname.c_str());
                        std::exit(1);
                    }
                    tf_str = std::string(sv);
                    if (timeframe_to_minutes(tf_str) <= 0) {
                        std::fprintf(stderr, "Config error: invalid timeframe '%s' in bounds for '%s'\n",
                            tf_str.c_str(), pname.c_str());
                        std::exit(1);
                    }
                    ++elem_idx;
                    continue;
                }
                // Try double
                double v;
                if (!elem.get_double().get(v)) {
                    if (num_count >= 3) {
                        std::fprintf(stderr, "Config error: bound '%s' has too many elements "
                            "(max 3 numbers, or [\"tf\", lo, hi, step])\n", pname.c_str());
                        std::exit(1);
                    }
                    vals[num_count++] = v;
                    ++elem_idx;
                    continue;
                }
                // Try uint64
                uint64_t u;
                if (!elem.get_uint64().get(u)) {
                    if (num_count >= 3) {
                        std::fprintf(stderr, "Config error: bound '%s' has too many elements\n",
                            pname.c_str());
                        std::exit(1);
                    }
                    vals[num_count++] = static_cast<double>(u);
                    ++elem_idx;
                    continue;
                }
                std::fprintf(stderr, "Config error: bound '%s' element %zu must be a string or number\n",
                    pname.c_str(), elem_idx);
                std::exit(1);
            }

            bnd.timeframe = tf_str;

            if (!tf_str.empty()) {
                // Format: ["tf", lo, hi, step]  — need exactly 3 numbers
                if (num_count != 3) {
                    std::fprintf(stderr, "Config error: bound '%s' with timeframe needs 3 numbers "
                        "(lo, hi, step), got %zu\n", pname.c_str(), num_count);
                    std::exit(1);
                }
            } else {
                // Format: [lo, hi] or [lo, hi, step] — 2 or 3 numbers
                if (num_count < 2 || num_count > 3) {
                    std::fprintf(stderr, "Config error: bound '%s' needs 2 or 3 numbers "
                        "(lo, hi[, step]), got %zu\n", pname.c_str(), num_count);
                    std::exit(1);
                }
                // Step defaults to 0.0 if not provided
                if (num_count == 2) vals[2] = 0.0;
            }

            bnd.lo = vals[0];
            bnd.hi = vals[1];
            bnd.step = vals[2];

            // Validation
            if (bnd.lo > bnd.hi) {
                std::fprintf(stderr, "Config error: bound '%s' lo (%.4f) > hi (%.4f)\n",
                    pname.c_str(), bnd.lo, bnd.hi);
                std::exit(1);
            }
            if (bnd.step < 0.0) {
                std::fprintf(stderr, "Config error: bound '%s' step (%.4f) must be >= 0\n",
                    pname.c_str(), bnd.step);
                std::exit(1);
            }

            // Warning when step is omitted (0.0) for a non-trivial range
            if (bnd.step == 0.0 && std::abs(bnd.lo - bnd.hi) > 1e-15) {
                std::fprintf(stderr, "Warning: bound '%s' has range [%.4f, %.4f] without step.\n"
                    "  Without a step, int params are auto-sampled (max 10 values) and\n"
                    "  float params get 5 linear samples. Add a step for explicit control.\n",
                    pname.c_str(), bnd.lo, bnd.hi);
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
                        BoundSpec spec = parse_bound_arr(arr, full_key);
                        oc.bounds[full_key] = spec;
                        // Inject bound timeframe into strategy config if present
                        if (!spec.timeframe.empty()) {
                            auto it = sp.indicator_timeframes.find(full_key);
                            if (it != sp.indicator_timeframes.end() && it->second != spec.timeframe) {
                                std::fprintf(stderr, "Config error: mismatch for '%s': strategy config uses "
                                    "timeframe '%s' but bounds use '%s'\n",
                                    full_key.c_str(), it->second.c_str(), spec.timeframe.c_str());
                                std::exit(1);
                            }
                            sp.indicator_timeframes[full_key] = spec.timeframe;
                        }
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
                BoundSpec spec = parse_bound_arr(arr, key_str);
                oc.bounds[key_str] = spec;
                // Inject bound timeframe into strategy config if present
                if (!spec.timeframe.empty()) {
                    auto it = sp.indicator_timeframes.find(key_str);
                    if (it != sp.indicator_timeframes.end() && it->second != spec.timeframe) {
                        std::fprintf(stderr, "Config error: mismatch for '%s': strategy config uses "
                            "timeframe '%s' but bounds use '%s'\n",
                            key_str.c_str(), it->second.c_str(), spec.timeframe.c_str());
                        std::exit(1);
                    }
                    sp.indicator_timeframes[key_str] = spec.timeframe;
                }
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
        // validate common strategy param ranges
        // entry_ema_period: only validate if the selected module uses it
        bool needs_ema_period = (cfg.strategy.entry_condition_type == "ema_dist_pct"
                              || cfg.strategy.entry_condition_type == "bb_reversion");
        if (needs_ema_period && cfg.strategy.entry_ema_period < 2) fatal("entry_ema_period must be >= 2");
        // H4: strict validation — zero entry_grid_spacing_pct causes div-by-zero
        // in martingale.cpp and dca_linear.cpp
        if (cfg.strategy.entries_algo_type == "martingale" || cfg.strategy.entries_algo_type == "dca_linear") {
            if (cfg.strategy.entry_grid_spacing_pct <= 0.0) {
                std::fprintf(stderr,
                    "Config error: entry_grid_spacing_pct must be > 0 for entries_algo '%s' "
                    "(got %g — zero causes division-by-zero)\n",
                    cfg.strategy.entries_algo_type.c_str(),
                    cfg.strategy.entry_grid_spacing_pct);
                std::exit(1);
            }
        } else {
            if (cfg.strategy.entry_grid_spacing_pct < 0.0) fatal("entry_grid_spacing_pct must be >= 0");
        }
        if (cfg.strategy.initial_qty_pct < 0.0 || cfg.strategy.initial_qty_pct > 1.0) fatal("initial_qty_pct must be in [0, 1]");
        if (cfg.strategy.sl_upnl_pct > 0.0) fatal("sl_upnl_pct must be <= 0");
        if (cfg.strategy.n_positions < 1) fatal("n_positions must be >= 1");
        if (cfg.strategy.parkinson_volatility_span < 2) fatal("parkinson_volatility_span must be >= 2");
        if (cfg.strategy.maker_fee_pct < 0.0 || cfg.strategy.maker_fee_pct > 1.0) fatal("maker_fee_pct must be in [0, 1]");
        if (cfg.strategy.entry_condition_type == "ema_dist_pct") {
            if (cfg.strategy.entry_ema_distance_pct < 0.0) fatal("entry_ema_distance_pct must be >= 0");
        }
        if (cfg.strategy.entry_condition_type == "bb_reversion") {
            if (cfg.strategy.bb_std_mult < 0.5) fatal("bb_std_mult must be >= 0.5");
            if (cfg.strategy.bb_min_bandwidth_pct < 0.0) fatal("bb_min_bandwidth_pct must be >= 0");
        }
        if (cfg.strategy.entries_algo_type == "martingale") {
            if (cfg.strategy.double_down_factor < 0.0) fatal("double_down_factor must be >= 0");
        }
        if (cfg.strategy.entries_algo_type == "dca_linear") {
            if (cfg.strategy.linear_step < 0.0) fatal("linear_step must be >= 0");
        }
        if (cfg.strategy.closes_algo_type == "simple_grid") {
            if (cfg.strategy.close_grid_spacing_pct < 0.0) fatal("close_grid_spacing_pct must be >= 0");
            if (cfg.strategy.close_grid_count < 1) fatal("close_grid_count must be >= 1");
        }
        if (cfg.strategy.closes_algo_type == "mean_revert_tp") {
            if (cfg.strategy.revert_close_frac < 0.0 || cfg.strategy.revert_close_frac > 1.0) fatal("revert_close_frac must be in [0, 1]");
            if (cfg.strategy.overshoot_pct < 0.0) fatal("overshoot_pct must be >= 0");
        }
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

// computed warmup: max of parkinson_span, entry_ema_period (if used), and zscore_vwap_lookback (if used)
    int a = cfg.strategy.parkinson_volatility_span;
    if ((cfg.strategy.entry_condition_type == "ema_dist_pct" || cfg.strategy.entry_condition_type == "bb_reversion") && cfg.strategy.entry_ema_period > a) a = cfg.strategy.entry_ema_period;
    if (cfg.strategy.entry_condition_type == "zscore_ou" && cfg.strategy.zscore_vwap_lookback > a) a = cfg.strategy.zscore_vwap_lookback;
    cfg.warmup_candles = a;

    return cfg;
}

// ===========================================================================
// Timeframe helpers
// ===========================================================================

int timeframe_to_minutes(const std::string& tf) {
    if (tf.empty()) return 0;
    char unit = tf.back();
    int mult;
    switch (unit) {
        case 'm': mult = 1; break;
        case 'h': mult = 60; break;
        case 'd': mult = 1440; break;
        case 'w': mult = 10080; break;
        default: return 0;
    }
    int num = 0;
    for (size_t i = 0; i + 1 < tf.size(); ++i) {
        if (tf[i] < '0' || tf[i] > '9') return 0;
        num = num * 10 + (tf[i] - '0');
    }
    return num * mult;
}

int64_t timeframe_to_ms(const std::string& tf) {
    return static_cast<int64_t>(timeframe_to_minutes(tf)) * 60000;
}

double candle_ratio(const std::string& tf, const std::string& base) {
    int a = timeframe_to_minutes(tf);
    int b = timeframe_to_minutes(base);
    if (a <= 0 || b <= 0) return 1.0;
    return static_cast<double>(a) / static_cast<double>(b);
}

/// Returns the set of parameter names that support a timeframe prefix.
bool supports_timeframe(const std::string& name) {
    return name == "entry_ema_period"
        || name == "atr_period"
        || name == "zscore_vwap_lookback"
        || name == "parkinson_volatility_span";
}

}  // namespace powermdg
