// Quick validation test for constraint-aware NSGA-II sort.
// Run: ./build/tests/test_constraint_sort
#include "optimizer/nsga2.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace martingale;

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
    std::printf("\nAll tests passed.\n");
    return 0;
}
