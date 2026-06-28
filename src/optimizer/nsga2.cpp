#include "optimizer/nsga2.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

namespace powermdg {

// ---------------------------------------------------------------------------
// Fast non-dominated sort  O(M * N^2)
// Implements constraint-domination (Deb et al. 2002):
//   - feasible (cv <= eps) dominates infeasible (cv > eps)
//   - among infeasible, lower cv dominates
//   - among feasible, classic Pareto domination on objectives
// ---------------------------------------------------------------------------

namespace {

constexpr double CV_EPS = 1e-10;

/// Returns true if individual i constraint-dominates individual j.
///   i dominates j if:
///     - i is feasible and j is infeasible, OR
///     - both infeasible and i has strictly lower cv, OR
///     - both feasible and i Pareto-dominates j on objectives
bool constraint_dominates(
    size_t i, size_t j,
    const std::vector<std::vector<double>>& objectives,
    const std::vector<double>& cv,
    bool use_cv)
{
    if (use_cv) {
        double const cv_i = cv[i];
        double const cv_j = cv[j];
        bool const i_feas = cv_i <= CV_EPS;
        bool const j_feas = cv_j <= CV_EPS;
        if (i_feas && !j_feas) return true;
        if (!i_feas && j_feas) return false;
        if (!i_feas && !j_feas) {
            return cv_i < cv_j;
        }
        // both feasible → fall through to Pareto
    }

    // Pure Pareto domination on objectives (all objectives are minimize).
    size_t const m = objectives[i].size();
    bool i_le_j = true;  // i <= j on all objectives
    bool i_lt_j = false; // i < j on at least one
    for (size_t k = 0; k < m; ++k) {
        double const vi = objectives[i][k];
        double const vj = objectives[j][k];
        if (vi > vj) { i_le_j = false; break; }
        if (vi < vj) i_lt_j = true;
    }
    return i_le_j && i_lt_j;
}

} // anonymous namespace

std::vector<int> fast_non_dominated_sort(
    const std::vector<std::vector<double>>& objectives,
    const std::vector<double>& constraint_violation)
{
    size_t n = objectives.size();
    std::vector<int> rank(n, 0);
    if (n == 0) return rank;

    bool const use_cv = !constraint_violation.empty();
    size_t m = objectives[0].size();
    if (m == 0) {
        for (size_t i = 0; i < n; ++i) rank[i] = 1;
        return rank;
    }

    std::vector<int> domination_count(n, 0);
    std::vector<std::vector<size_t>> dominated(n);

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            if (constraint_dominates(i, j, objectives, constraint_violation, use_cv)) {
                dominated[i].push_back(j);
            } else if (constraint_dominates(j, i, objectives, constraint_violation, use_cv)) {
                domination_count[i]++;
            }
        }
        if (domination_count[i] == 0) {
            rank[i] = 1;
        }
    }

    std::vector<size_t> current_front;
    for (size_t i = 0; i < n; ++i) {
        if (domination_count[i] == 0) {
            current_front.push_back(i);
        }
    }

    int front_number = 1;
    while (!current_front.empty()) {
        std::vector<size_t> next_front;
        for (size_t i : current_front) {
            for (size_t j : dominated[i]) {
                domination_count[j]--;
                if (domination_count[j] == 0) {
                    rank[j] = front_number + 1;
                    next_front.push_back(j);
                }
            }
        }
        current_front = std::move(next_front);
        front_number++;
    }

    return rank;
}

// ---------------------------------------------------------------------------
// Crowding distance
// ---------------------------------------------------------------------------

void calculate_crowding_distance(std::vector<Individual>& front) {
    size_t fsize = front.size();
    if (fsize == 0) return;

    for (auto& ind : front) {
        ind.crowding_distance = 0.0;
    }

    if (fsize <= 2) {
        for (auto& ind : front) {
            ind.crowding_distance = std::numeric_limits<double>::max();
        }
        return;
    }

    size_t nobj = front[0].objectives.size();
    if (nobj == 0) return;

    for (size_t obj = 0; obj < nobj; ++obj) {
        std::sort(front.begin(), front.end(),
            [obj](const Individual& a, const Individual& b) {
                return a.objectives[obj] < b.objectives[obj];
            });

        double min_obj = front.front().objectives[obj];
        double max_obj = front.back().objectives[obj];

        front.front().crowding_distance = std::numeric_limits<double>::max();
        front.back().crowding_distance = std::numeric_limits<double>::max();

        if (max_obj - min_obj < 1e-15) continue;

        for (size_t i = 1; i < fsize - 1; ++i) {
            double diff = front[i + 1].objectives[obj] - front[i - 1].objectives[obj];
            front[i].crowding_distance += diff / (max_obj - min_obj);
        }
    }
}

// ---------------------------------------------------------------------------
// Tournament selection with constraint-domination rules
// ---------------------------------------------------------------------------

namespace {

bool tournament_better(const Individual& a, const Individual& b) {
    constexpr double eps = 1e-10;
    bool a_feas = a.constraint_violation <= eps;
    bool b_feas = b.constraint_violation <= eps;

    if (a_feas && !b_feas) return true;
    if (!a_feas && b_feas) return false;
    if (!a_feas && !b_feas) return a.constraint_violation < b.constraint_violation;

    if (a.rank != b.rank) return a.rank < b.rank;
    return a.crowding_distance > b.crowding_distance;
}

} // anonymous namespace

size_t tournament_select(const std::vector<Individual>& population,
                         int tournament_size,
                         std::mt19937_64& rng)
{
    size_t pop_size = population.size();
    if (pop_size == 0) return 0;

    std::uniform_int_distribution<size_t> dist(0, pop_size - 1);
    size_t best_idx = dist(rng);

    for (int i = 1; i < tournament_size; ++i) {
        size_t idx = dist(rng);
        if (tournament_better(population[idx], population[best_idx])) {
            best_idx = idx;
        }
    }

    return best_idx;
}

// ---------------------------------------------------------------------------
// SBX crossover
// ---------------------------------------------------------------------------

void sbx_crossover(std::vector<double>& p1, std::vector<double>& p2,
                   const std::vector<double>& lo, const std::vector<double>& hi,
                   double crossover_prob, double eta_c,
                   std::mt19937_64& rng)
{
    size_t n = p1.size();
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    for (size_t i = 0; i < n; ++i) {
        if (unit(rng) >= crossover_prob) continue;

        double u = unit(rng);
        double beta;
        if (u <= 0.5) {
            beta = std::pow(2.0 * u, 1.0 / (eta_c + 1.0));
        } else {
            beta = std::pow(1.0 / (2.0 * (1.0 - u)), 1.0 / (eta_c + 1.0));
        }

        double c1 = 0.5 * ((1.0 + beta) * p1[i] + (1.0 - beta) * p2[i]);
        double c2 = 0.5 * ((1.0 - beta) * p1[i] + (1.0 + beta) * p2[i]);

        if (c1 < lo[i]) c1 = lo[i];
        if (c1 > hi[i]) c1 = hi[i];
        if (c2 < lo[i]) c2 = lo[i];
        if (c2 > hi[i]) c2 = hi[i];

        p1[i] = c1;
        p2[i] = c2;
    }
}

// ---------------------------------------------------------------------------
// Polynomial mutation (standard Deb formula)
// ---------------------------------------------------------------------------

void polynomial_mutation(std::vector<double>& genes,
                         const std::vector<double>& lo, const std::vector<double>& hi,
                         double /*mutation_prob*/, double mutation_indpb, double eta_m,
                         std::mt19937_64& rng)
{
    size_t n = genes.size();
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    for (size_t i = 0; i < n; ++i) {
        if (unit(rng) >= mutation_indpb) continue;

        double r = unit(rng);
        double deltaq;
        if (r < 0.5) {
            deltaq = std::pow(2.0 * r, 1.0 / (eta_m + 1.0)) - 1.0;
        } else {
            deltaq = 1.0 - std::pow(2.0 * (1.0 - r), 1.0 / (eta_m + 1.0));
        }

        genes[i] += deltaq * (hi[i] - lo[i]);

        if (genes[i] < lo[i]) genes[i] = lo[i];
        if (genes[i] > hi[i]) genes[i] = hi[i];
    }
}

// ---------------------------------------------------------------------------
// Select next generation using NSGA-II criteria
// ---------------------------------------------------------------------------

std::vector<Individual> select_next_generation(
    const std::vector<Individual>& combined,
    size_t n)
{
    if (combined.empty() || n == 0) return {};

    // NOTE: do NOT early-return when n >= combined.size(): we still need to
    // compute ranks and crowding distances so that subsequent tournament
    // selection works correctly. (Audit issue O2: the previous early-return
    // left the initial population with rank=0, crowding=0 for ALL individuals,
    // making tournament_select effectively random for the entire first
    // generation.)

    std::vector<std::vector<double>> objectives(combined.size());
    std::vector<double> cv(combined.size());
    for (size_t i = 0; i < combined.size(); ++i) {
        objectives[i] = combined[i].objectives;
        cv[i] = combined[i].constraint_violation;
    }

    // Constraint-aware sort: feasible candidates dominate infeasible ones,
    // so infeasible candidates get pushed to deeper fronts and are less likely
    // to survive environmental selection.
    std::vector<int> ranks = fast_non_dominated_sort(objectives, cv);

    int max_rank = 0;
    for (int r : ranks) {
        if (r > max_rank) max_rank = r;
    }
    if (max_rank <= 0) {
        // No feasible front — return combined as-is with default rank/crowding
        return combined;
    }

    std::vector<std::vector<size_t>> fronts(static_cast<size_t>(max_rank));
    for (size_t i = 0; i < ranks.size(); ++i) {
        if (ranks[i] > 0) {
            fronts[static_cast<size_t>(ranks[i] - 1)].push_back(i);
        }
    }

    std::vector<Individual> selection;

    for (auto& front_indices : fronts) {
        if (selection.size() >= n) break;

        std::vector<Individual> front_individuals;
        front_individuals.reserve(front_indices.size());
        for (size_t idx : front_indices) {
            Individual ind = combined[idx];
            ind.rank = ranks[idx];
            ind.crowding_distance = 0.0;
            front_individuals.push_back(std::move(ind));
        }

        calculate_crowding_distance(front_individuals);

        if (selection.size() + front_individuals.size() <= n) {
            for (auto& ind : front_individuals) {
                selection.push_back(std::move(ind));
            }
        } else {
            size_t remaining = n - selection.size();
            std::sort(front_individuals.begin(), front_individuals.end(),
                [](const Individual& a, const Individual& b) {
                    return a.crowding_distance > b.crowding_distance;
                });

            for (size_t i = 0; i < remaining && i < front_individuals.size(); ++i) {
                selection.push_back(std::move(front_individuals[i]));
            }
            break;
        }
    }

    return selection;
}

// ---------------------------------------------------------------------------
// Random initial population
// ---------------------------------------------------------------------------

std::vector<std::vector<double>> random_population(
    size_t n,
    const std::vector<double>& lo,
    const std::vector<double>& hi,
    std::mt19937_64& rng)
{
    std::vector<std::vector<double>> pop(n);
    size_t nparams = lo.size();

    for (size_t i = 0; i < n; ++i) {
        pop[i].resize(nparams);
        for (size_t j = 0; j < nparams; ++j) {
            std::uniform_real_distribution<double> dist(lo[j], hi[j]);
            pop[i][j] = dist(rng);
        }
    }

    return pop;
}

} // namespace powermdg
