// Quick validation test for constraint-aware NSGA-II sort, plus optimizer
// unit tests for C3 (apply_param_to_cfg no clobbering) and H5 (weight scaling).
// Run: ./build_b/tests/test_constraint_sort
#include "optimizer/nsga2.h"
#include "optimizer/optimizer.h"
#include "config/types.h"
#include "metrics/types.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>

using namespace powermdg;

int main() {
    // 4 individuals, 2 objectives (minimize).
    // - ind 0: feasible, objectives [1.0, 1.0]
    // - ind 1: feasible, objectives [0.5, 2.0]  (dominates ind 0 on obj 0)
    // - ind 2: INFEASIBLE (cv=100), objectives [0.1, 0.1] (looks best on obj but infeasible)
    // - ind 3: INFEASIBLE (cv=50),  objectives [0.2, 0.2]  (lower cv than ind 2)
    //
    // Expected behavior (constraint-domination):
    //   - ind 1 dominates ind 0 (both feasible, ind 1 better on obj 0)
    //   - ind 0 and ind 1 both dominate ind 2 and ind 3 (feasible > infeasible)
    //   - ind 3 dominates ind 2 (both infeasible, ind 3 has lower cv)
    //
    // Expected ranks:
    //   - rank 1: ind 1 (feasible, not dominated by any other feasible)
    //   - rank 2: ind 0 (dominated by ind 1)
    //   - rank 3: ind 3 (infeasible, lower cv than ind 2)
    //   - rank 4: ind 2 (infeasible, higher cv)

    std::vector<std::vector<double>> objectives = {
        {1.0, 1.0},  // ind 0
        {0.5, 2.0},  // ind 1
        {0.1, 0.1},  // ind 2
        {0.2, 0.2},  // ind 3
    };
    std::vector<double> cv = {0.0, 0.0, 100.0, 50.0};

    auto ranks = fast_non_dominated_sort(objectives, cv);

    std::printf("Ranks with constraint-aware sort:\n");
    for (size_t i = 0; i < ranks.size(); ++i) {
        std::printf("  ind %zu (cv=%.1f, obj=[%.1f,%.1f]): rank %d\n",
                    i, cv[i], objectives[i][0], objectives[i][1], ranks[i]);
    }

    // Verify
    assert(ranks[0] == 2 && "ind 0 should be rank 2 (dominated by ind 1)");
    assert(ranks[1] == 1 && "ind 1 should be rank 1 (Pareto-best feasible)");
    assert(ranks[2] == 4 && "ind 2 should be rank 4 (infeasible, highest cv)");
    assert(ranks[3] == 3 && "ind 3 should be rank 3 (infeasible, lower cv than ind 2)");

    std::printf("\nPASS: constraint-aware sort correctly pushes infeasible candidates to deeper fronts.\n");

    // Now verify the OLD behavior would have been wrong:
    // Without constraints, ind 2 (obj=[0.1,0.1]) dominates everything → rank 1
    auto ranks_old = fast_non_dominated_sort(objectives, {});
    std::printf("\nRanks WITHOUT constraint-aware sort (old buggy behavior):\n");
    for (size_t i = 0; i < ranks_old.size(); ++i) {
        std::printf("  ind %zu (cv=%.1f, obj=[%.1f,%.1f]): rank %d\n",
                    i, cv[i], objectives[i][0], objectives[i][1], ranks_old[i]);
    }
    assert(ranks_old[2] == 1 && "ind 2 (infeasible but best obj) wrongly gets rank 1 in old sort");

    std::printf("\nPASS: confirmed the old sort was buggy (infeasible candidate got rank 1).\n");
    // -----------------------------------------------------------------------
    // C3: apply_param_to_cfg("atr_filter_mult", v) must NOT touch
    //     tp_min_upnl_pct or time_based_unstuck_age.
    // -----------------------------------------------------------------------
    {
        Config cfg{};
        cfg.strategy.tp_min_upnl_pct         = 0.123;
        cfg.strategy.time_based_unstuck_age   = 42;
        cfg.strategy.atr_filter_mult          = 0.0;

        double const new_val = 3.7;
        apply_param_to_cfg(cfg, "atr_filter_mult", new_val);

        assert(std::abs(cfg.strategy.atr_filter_mult - new_val) < 1e-12
               && "atr_filter_mult should be updated");
        assert(std::abs(cfg.strategy.tp_min_upnl_pct - 0.123) < 1e-12
               && "C3: tp_min_upnl_pct must NOT be overwritten by atr_filter_mult");
        assert(cfg.strategy.time_based_unstuck_age == 42
               && "C3: time_based_unstuck_age must NOT be overwritten by atr_filter_mult");

        std::printf("\nPASS C3: apply_param_to_cfg(\"atr_filter_mult\") leaves "
                    "tp_min_upnl_pct and time_based_unstuck_age unchanged.\n");
    }

    // -----------------------------------------------------------------------
    // H5: scoring weight scales objectives.
    // Build two ScoringMetric arrays that differ only in weight, and verify
    // that the resulting objectives are proportionally scaled.
    // -----------------------------------------------------------------------
    {
        // Build one Individual with a known metric value (gain = 2.0).
        Individual ind;
        ind.metrics.gain = 2.0;

        // Scoring: goal "max" → engine_sign = -1 (NSGA-II minimises, so we
        // flip the sign for "max" goals).
        ScoringMetric sm_w1;
        sm_w1.metric      = "gain";
        sm_w1.goal        = "max";
        sm_w1.engine_sign = -1.0;
        sm_w1.weight      = 1.0;

        ScoringMetric sm_w3;
        sm_w3.metric      = "gain";
        sm_w3.goal        = "max";
        sm_w3.engine_sign = -1.0;
        sm_w3.weight      = 3.0;

        std::vector<Individual> pop1 = {ind};
        std::vector<Individual> pop3 = {ind};

        compute_objectives_for_population(pop1, {sm_w1});
        compute_objectives_for_population(pop3, {sm_w3});

        // With weight=1: obj = 2.0 * (-1) * 1.0 = -2.0
        // With weight=3: obj = 2.0 * (-1) * 3.0 = -6.0
        assert(std::abs(pop1[0].objectives[0] - (-2.0)) < 1e-12
               && "H5: weight=1 objective should be -2.0");
        assert(std::abs(pop3[0].objectives[0] - (-6.0)) < 1e-12
               && "H5: weight=3 objective should be -6.0");

        // The ratio must equal the weight ratio
        double const ratio = pop3[0].objectives[0] / pop1[0].objectives[0];
        assert(std::abs(ratio - 3.0) < 1e-12
               && "H5: objective ratio must equal weight ratio (3.0)");

        // Zero weight must default to 1.0 (not collapse the axis)
        ScoringMetric sm_w0 = sm_w1;
        sm_w0.weight = 0.0;
        std::vector<Individual> pop0 = {ind};
        compute_objectives_for_population(pop0, {sm_w0});
        assert(std::abs(pop0[0].objectives[0] - (-2.0)) < 1e-12
               && "H5: weight=0 should default to 1.0, giving same result as weight=1");

        std::printf("\nPASS H5: scoring weights correctly scale objectives "
                    "(weight=1 → -2, weight=3 → -6, weight=0 → -2).\n");
    }

    std::printf("\nAll tests passed.\n");
    return 0;
}
