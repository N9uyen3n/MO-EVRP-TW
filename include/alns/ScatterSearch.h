#pragma once
#include <memory>
#include <random>
#include <vector>

#include "../core/Instance.h"
#include "../core/Solution.h"
#include "ParetoArchive.h"

// Forward declaration to avoid circular dependency
namespace alns {
class ALNSSolver;
} // namespace alns

namespace alns {

/**
 * @brief Scatter Search intensification phase for MOSS-ALNS.
 *
 * Based on López-Sánchez, Sánchez-Oro & Laguna (2021).
 * Uses Path Relinking as combination method, Crowding Distance for diversity
 * measurement, and rebuilds the Reference Set from the Pareto Archive
 * each iteration.
 */
class ScatterSearch {
public:
  struct Config {
    int refSetSizeQuality = 5;        // b1: top quality solutions (Tier 1)
    int refSetSizeDiversity = 5;      // b2: most diverse by crowding (Tier 2)
    int alnsItersPerCombination = 30; // ALNS mini-iters per child
    int maxScatterIters = 5;          // Max SS rounds
    long long timeLimitMs = 30000;    // Hard time budget: 10 seconds
    int stagnationThreshold =
        2; // Rounds without improvement before perturbation
    int maxPRMoves =
        25; // Max moves per Path Relinking call (early termination)
  };

  ScatterSearch(std::shared_ptr<Instance> instance, Config config,
                std::mt19937 &rng);

  /**
   * @brief Run the Scatter Search intensification phase.
   * @param archive  The Pareto archive to read from and write to.
   * @param solver   The ALNSSolver, used to call improveSolution().
   * @return Number of new non-dominated solutions added to archive.
   */
  int run(ParetoArchive &archive, ALNSSolver &solver);

private:
  // ── Path Relinking ──

  /// A single relocation move: move customerId to the route identified by
  /// routeSeedCustomer
  struct PRMove {
    int customerId;
    std::vector<int> guideRouteCustomers; // Customers of the target route in guiding solution
  };

  /**
   * @brief Compute the symmetric difference between two solutions.
   * For each customer, compare its route assignment. A move is generated
   * for each customer assigned to a different route.
   */
  std::vector<PRMove> computeDiff(const Solution &init,
                                  const Solution &guide) const;

  /**
   * @brief Apply a single PR move: remove customer from its current route
   * and insert it at the best position in the route containing
   * routeSeedCustomer.
   */
  void applyPRMove(Solution &sol, const PRMove &move) const;

  /**
   * @brief Bidirectional Path Relinking with greedy best-first move selection.
   * Runs forward (init→guide) and backward (guide→init), returns all feasible
   * intermediate solutions found across both directions.
   */
  std::vector<Solution> pathRelinking(const Solution &init, const Solution &guide);

  /**
   * @brief Single-direction Path Relinking (greedy best-first).
   */
  std::vector<Solution> pathRelinkingOneDirection(const Solution &init,
                                                  const Solution &guide);

  // ── Reference Set Management ──

  /**
   * @brief Rebuild RefSet entirely from the Pareto Archive.
   * Tier 1 = best quality (vehicles ASC, distance ASC).
   * Tier 2 = highest crowding distance (most diverse on the front).
   */
  void rebuildRefSetFromArchive(ParetoArchive &archive);

  /**
   * @brief Compute NSGA-II crowding distance over 3 objectives.
   * Returns per-solution crowding distance. Boundary solutions get infinity.
   */
  static std::vector<double>
  computeCrowdingDistance(const std::vector<Solution> &front);

  /**
   * @brief Helper: extract objective value by index (0=dist, 1=gini,
   * 2=maxTime).
   */
  static double getObjective(const Solution &s, int objIdx);

  std::shared_ptr<Instance> instance_;
  Config config_;
  std::mt19937 &rng_;

  // Dynamic Reference Set
  std::vector<Solution> tier1_; // Quality (best solutions)
  std::vector<Solution> tier2_; // Diversity (highest crowding distance)
};

} // namespace alns
