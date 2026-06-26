#ifndef MARTINGALE_OPTIMIZER_NSGA2_H
#define MARTINGALE_OPTIMIZER_NSGA2_H

#include "config/types.h"
#include "metrics/types.h"
#include <vector>
#include <string>
#include <map>
#include <functional>
#include <random>

namespace martingale {

/// A single candidate solution in the GA population.
struct Individual {
    std::vector<double> genes;          // parameter values (raw, not normalized)
    Config config;                       // derived config from genes
    Metrics metrics;                     // computed backtest metrics
    std::vector<double> objectives;      // engine-space objective values (always minimize)
    double constraint_violation = 0.0;
    int rank = 0;
    double crowding_distance = 0.0;
    int generation = 0;
};

/// Fast non-dominated sort.
/// Returns a vector of front indices: result[i] = front rank of individual i.
std::vector<int> fast_non_dominated_sort(const std::vector<std::vector<double>>& objectives);

/// Calculate crowding distance for individuals within a single front.
/// Modifies individuals' crowding_distance in place.
void calculate_crowding_distance(std::vector<Individual>& front);

/// Selection helpers
/// Tournament selection: pick k individuals randomly, return the best by (rank, -crowding_distance).
size_t tournament_select(const std::vector<Individual>& population,
                         int tournament_size,
                         std::mt19937_64& rng);

/// SBX crossover (Simulated Binary Crossover) in-place on two parents' genes.
/// parent1 and parent2 are modified to become children.
void sbx_crossover(std::vector<double>& p1, std::vector<double>& p2,
                   const std::vector<double>& lo, const std::vector<double>& hi,
                   double crossover_prob, double eta_c,
                   std::mt19937_64& rng);

/// Polynomial mutation in-place on a single individual's genes.
void polynomial_mutation(std::vector<double>& genes,
                         const std::vector<double>& lo, const std::vector<double>& hi,
                         double mutation_prob, double mutation_indpb, double eta_m,
                         std::mt19937_64& rng);

/// Select the best `n` individuals from a combined population using NSGA-II criteria
/// (rank first, then crowding distance).
std::vector<Individual> select_next_generation(
    const std::vector<Individual>& combined,
    size_t n);

/// Random initial population: generate `n` individuals with random genes in [lo, hi].
std::vector<std::vector<double>> random_population(
    size_t n,
    const std::vector<double>& lo,
    const std::vector<double>& hi,
    std::mt19937_64& rng);


} // namespace martingale

#endif
