#include "../../include/alns/ScatterSearch.h"
#include "../../include/alns/ALNSSolver.h"
#include "../../include/core/Instance.h"
#include "../../include/core/Vehicle.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace alns {

// ============================================================
// Constructor
// ============================================================

ScatterSearch::ScatterSearch(std::shared_ptr<Instance> instance, Config config,
                             std::mt19937 &rng)
    : instance_(std::move(instance)), config_(config), rng_(rng) {}

// ============================================================
// Helper: Get objective value by index
// ============================================================

double ScatterSearch::getObjective(const Solution &s, int objIdx) {
  switch (objIdx) {
  case 0:
    return s.getTotalDistance();
  case 1:
    return s.getWorkloadGini();
  case 2:
    return s.getMaxTime();
  default:
    return s.getTotalDistance();
  }
}

// ============================================================
// Crowding Distance (NSGA-II standard, 3 objectives)
// ============================================================

std::vector<double>
ScatterSearch::computeCrowdingDistance(const std::vector<Solution> &front) {
  int n = static_cast<int>(front.size());
  std::vector<double> dist(n, 0.0);

  if (n <= 2) {
    std::fill(dist.begin(), dist.end(),
              std::numeric_limits<double>::infinity());
    return dist;
  }

  const int numObjectives = 3; // distance, gini, maxTime

  for (int obj = 0; obj < numObjectives; ++obj) {
    // Sort indices by this objective
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);

    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
      return getObjective(front[a], obj) < getObjective(front[b], obj);
    });

    // Boundary solutions always kept
    dist[idx[0]] = std::numeric_limits<double>::infinity();
    dist[idx[n - 1]] = std::numeric_limits<double>::infinity();

    double range =
        getObjective(front[idx[n - 1]], obj) - getObjective(front[idx[0]], obj);
    if (range < 1e-10)
      continue;

    for (int i = 1; i < n - 1; ++i) {
      dist[idx[i]] += (getObjective(front[idx[i + 1]], obj) -
                       getObjective(front[idx[i - 1]], obj)) /
                      range;
    }
  }
  return dist;
}

// ============================================================
// rebuildRefSetFromArchive — Rebuild Tier 1 (quality) + Tier 2 (crowding)
// ============================================================

void ScatterSearch::rebuildRefSetFromArchive(ParetoArchive &archive) {
  tier1_.clear();
  tier2_.clear();

  const auto &front = archive.getFront();
  if (front.empty())
    return;

  int n = static_cast<int>(front.size());

  // ─── Tier 1: Best quality (vehicles ASC, then distance ASC) ───
  std::vector<int> qualityIdx(n);
  std::iota(qualityIdx.begin(), qualityIdx.end(), 0);
  std::sort(qualityIdx.begin(), qualityIdx.end(), [&](int a, int b) {
    if (front[a].getTotalVehicles() != front[b].getTotalVehicles())
      return front[a].getTotalVehicles() < front[b].getTotalVehicles();
    return front[a].getTotalDistance() < front[b].getTotalDistance();
  });

  int b1 = std::min(config_.refSetSizeQuality, n);
  std::vector<bool> inTier1(n, false);
  for (int i = 0; i < b1; ++i) {
    tier1_.push_back(front[qualityIdx[i]]);
    inTier1[qualityIdx[i]] = true;
  }

  // ─── Tier 2: Highest crowding distance (excluding Tier 1 members) ───
  auto crowding = computeCrowdingDistance(front);

  // Build index list of candidates not in Tier 1
  std::vector<int> diverseIdx;
  for (int i = 0; i < n; ++i) {
    if (!inTier1[i])
      diverseIdx.push_back(i);
  }

  // Sort by crowding distance descending (most spread out first)
  std::sort(diverseIdx.begin(), diverseIdx.end(),
            [&](int a, int b) { return crowding[a] > crowding[b]; });

  int b2 = std::min(config_.refSetSizeDiversity,
                    static_cast<int>(diverseIdx.size()));
  for (int i = 0; i < b2; ++i) {
    tier2_.push_back(front[diverseIdx[i]]);
  }

  std::cout << "[SS] RefSet rebuilt: Tier1=" << tier1_.size()
            << " (quality), Tier2=" << tier2_.size() << " (diverse)"
            << std::endl;
}

// ============================================================
// computeDiff — Symmetric difference between two solutions
// ============================================================

std::vector<ScatterSearch::PRMove>
ScatterSearch::computeDiff(const Solution &init, const Solution &guide) const {
  struct RouteInfo {
    int representative;
    std::vector<int> customers;
  };

  auto buildRouteMap = [](const Solution &sol) -> std::unordered_map<int, RouteInfo> {
    std::unordered_map<int, RouteInfo> custToRoute;
    for (const auto &route : sol.getRoutes()) {
      auto customers = route.getCustomers();
      if (customers.empty())
        continue;
      int rep = *std::min_element(customers.begin(), customers.end());
      for (int c : customers) {
        custToRoute[c] = {rep, customers};
      }
    }
    return custToRoute;
  };

  auto initMap = buildRouteMap(init);
  auto guideMap = buildRouteMap(guide);

  std::vector<PRMove> moves;
  for (const auto &[custId, guideInfo] : guideMap) {
    auto it = initMap.find(custId);
    if (it == initMap.end()) {
      // Customer exists in guide but not in init — skip
      continue;
    }
    if (it->second.representative != guideInfo.representative) {
      // Customer is in a different route → generate a move
      moves.push_back({custId, guideInfo.customers});
    }
  }
  return moves;
}

// ============================================================
// applyPRMove — Relocate customer to route identified by seed
// ============================================================

void ScatterSearch::applyPRMove(Solution &sol, const PRMove &move) const {
  // Step 1: Remove customer from its current route
  sol.removeCustomer(move.customerId);
  sol.removeEmptyRoutes();

  // Step 2: Find the route containing the most customers from guideRouteCustomers
  auto &routes = sol.getRoutes();
  int targetRouteIdx = -1;
  int bestOverlap = 0;

  for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
    const auto &custs = routes[r].getCustomers();
    int overlap = 0;
    for (int c : custs) {
      if (std::find(move.guideRouteCustomers.begin(), move.guideRouteCustomers.end(), c) != move.guideRouteCustomers.end()) {
        overlap++;
      }
    }
    if (overlap > bestOverlap) {
      bestOverlap = overlap;
      targetRouteIdx = r;
    }
  }

  if (targetRouteIdx < 0) {
    // Seed customer not found (route was emptied/removed).
    // Fallback: insert at best position across all routes.
    double bestCost = std::numeric_limits<double>::max();
    int bestRoute = -1;
    size_t bestPos = 0;

    for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
      for (size_t pos = 1; pos < routes[r].size(); ++pos) {
        auto result = routes[r].checkInsertionCost(move.customerId, pos);
        if (result.isFeasible && result.deltaDistance < bestCost) {
          bestCost = result.deltaDistance;
          bestRoute = r;
          bestPos = pos;
        }
      }
    }
    if (bestRoute >= 0) {
      routes[bestRoute].addNode(move.customerId, bestPos);
    }
    // If no feasible position found, customer stays unassigned (infeasible)
    return;
  }

  // Step 3: Insert at best position in target route
  auto &targetRoute = routes[targetRouteIdx];
  double bestCost = std::numeric_limits<double>::max();
  size_t bestPos = 1; // default: after depot

  for (size_t pos = 1; pos < targetRoute.size(); ++pos) {
    auto result = targetRoute.checkInsertionCost(move.customerId, pos);
    if (result.isFeasible && result.deltaDistance < bestCost) {
      bestCost = result.deltaDistance;
      bestPos = pos;
    }
  }

  // If no feasible position in target route, try all routes as fallback
  if (bestCost >= std::numeric_limits<double>::max() - 1.0) {
    int fallbackRoute = -1;
    for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
      for (size_t pos = 1; pos < routes[r].size(); ++pos) {
        auto result = routes[r].checkInsertionCost(move.customerId, pos);
        if (result.isFeasible && result.deltaDistance < bestCost) {
          bestCost = result.deltaDistance;
          fallbackRoute = r;
          bestPos = pos;
        }
      }
    }
    if (fallbackRoute >= 0) {
      routes[fallbackRoute].addNode(move.customerId, bestPos);
    } else {
      // Fallback 2: Create a new route
      int newRouteId = sol.getNumRoutes();
      auto vehicle = std::make_shared<Vehicle>(
          newRouteId,
          instance_->getVehicleCapacity(),
          instance_->getVehicleBattery(),
          instance_->getVehicleEnergyRate()
      );
      Route newRoute(newRouteId, vehicle, instance_);
      newRoute.addNode(move.customerId, 1);
      sol.addRoute(newRoute);
    }
    return;
  }

  targetRoute.addNode(move.customerId, bestPos);
}

// ============================================================
// pathRelinkingOneDirection — Greedy best-first, single direction
// ============================================================

std::vector<Solution> ScatterSearch::pathRelinkingOneDirection(const Solution &init,
                                                  const Solution &guide) {
  std::vector<Solution> collected;
  Solution current = init;
  current.evaluateRoutes();

  auto diffMoves = computeDiff(init, guide);
  if (diffMoves.empty())
    return collected;

  int numMoves = static_cast<int>(diffMoves.size());
  std::vector<bool> applied(numMoves, false);

  int maxMoves = std::min(numMoves, config_.maxPRMoves);
  int moveCount = 0;

  while (moveCount < maxMoves) {
    // Greedy best-first: try all remaining moves, pick best delta
    int bestIdx = -1;
    double bestDelta = std::numeric_limits<double>::infinity();

    for (int i = 0; i < numMoves; ++i) {
      if (applied[i])
        continue;

      Solution temp = current;
      applyPRMove(temp, diffMoves[i]);
      temp.evaluateRoutes();

      if (!temp.isFeasible())
        continue;

      // Delta on primary metric: distance (within same vehicle count)
      double delta = temp.getTotalDistance() - current.getTotalDistance();

      // Penalize vehicle count increase heavily
      int vehDiff = temp.getTotalVehicles() - current.getTotalVehicles();
      if (vehDiff > 0)
        delta += vehDiff * 100000.0;

      if (delta < bestDelta) {
        bestDelta = delta;
        bestIdx = i;
      }
    }

    if (bestIdx == -1)
      break; // No feasible move remaining

    // Apply the best move
    applyPRMove(current, diffMoves[bestIdx]);
    applied[bestIdx] = true;
    current.evaluateRoutes();
    ++moveCount;

    if (current.isFeasible()) {
      collected.push_back(current);
    }
  }

  return collected;
}

// ============================================================
// pathRelinking — Bidirectional: forward + backward, return all intermediates
// ============================================================

std::vector<Solution> ScatterSearch::pathRelinking(const Solution &init,
                                      const Solution &guide) {
  std::vector<Solution> collected;

  // Forward: init → guide
  auto forwardIntermediates = pathRelinkingOneDirection(init, guide);
  collected.insert(collected.end(), forwardIntermediates.begin(), forwardIntermediates.end());

  // Backward: guide → init
  auto backwardIntermediates = pathRelinkingOneDirection(guide, init);
  collected.insert(collected.end(), backwardIntermediates.begin(), backwardIntermediates.end());

  return collected;
}

// ============================================================
// run() — Main Scatter Search loop
// ============================================================

int ScatterSearch::run(ParetoArchive &archive, ALNSSolver &solver) {
  int newSolutionsAdded = 0;
  auto startTime = std::chrono::steady_clock::now();
  int itersWithoutImprovement = 0;

  std::cout
      << "[SS] Starting Scatter Search (Path Relinking + Crowding Distance)"
      << std::endl;

  for (int ssIter = 0; ssIter < config_.maxScatterIters; ++ssIter) {
    // ─── Time budget check ───
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - startTime)
                       .count();
    if (elapsed >= config_.timeLimitMs) {
      std::cout << "[SS] Time budget " << config_.timeLimitMs
                << "ms exhausted (" << elapsed << "ms). Stopping." << std::endl;
      break;
    }

    // ─── Rebuild RefSet from archive ───
    rebuildRefSetFromArchive(archive);

    if (tier1_.empty() || tier2_.empty()) {
      std::cout << "[SS] RefSet too small (T1=" << tier1_.size()
                << ", T2=" << tier2_.size() << "). Breaking." << std::endl;
      break;
    }

    int pairCount = 0;
    bool anyNewInThisIter = false;

    // ─── Phase 1: Combine Tier1 × Tier1 (Exploitation) ───
    for (size_t t1a = 0; t1a < tier1_.size(); ++t1a) {
      for (size_t t1b = t1a + 1; t1b < tier1_.size(); ++t1b) {
        auto elapsedNow = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - startTime)
                              .count();
        if (elapsedNow >= config_.timeLimitMs)
          goto done;

        ++pairCount;

        // Bidirectional Path Relinking: collect intermediates
        auto intermediates = pathRelinking(tier1_[t1a], tier1_[t1b]);
        for (auto &child : intermediates) {
          child.evaluateRoutes();
          if (!child.isFeasible())
            continue;

          // Improve child with ALNS mini-loop (use half iterations to save time)
          solver.improveSolution(child, std::max(1, config_.alnsItersPerCombination / 2));
          child.evaluateRoutes();

          if (!child.isFeasible())
            continue;

          // Try to add to archive
          AddResult result = archive.tryAdd(child);
          if (result == AddResult::DOMINATING ||
              result == AddResult::NON_DOMINATED) {
            ++newSolutionsAdded;
            anyNewInThisIter = true;
          }
        }
      }
    }

    // ─── Phase 2: Combine Tier1 × Tier2 (Exploration) ───
    for (size_t t1i = 0; t1i < tier1_.size(); ++t1i) {
      for (size_t t2i = 0; t2i < tier2_.size(); ++t2i) {
        // Per-pair time budget check
        auto elapsedNow = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - startTime)
                              .count();
        if (elapsedNow >= config_.timeLimitMs)
          goto done;

        ++pairCount;

        // Bidirectional Path Relinking: collect intermediates
        auto intermediates = pathRelinking(tier1_[t1i], tier2_[t2i]);
        for (auto &child : intermediates) {
          child.evaluateRoutes();
          if (!child.isFeasible())
            continue;

          // Improve child with ALNS mini-loop (use half iterations to save time)
          solver.improveSolution(child, std::max(1, config_.alnsItersPerCombination / 2));
          child.evaluateRoutes();

          if (!child.isFeasible())
            continue;

          // Try to add to archive
          AddResult result = archive.tryAdd(child);
          if (result == AddResult::DOMINATING ||
              result == AddResult::NON_DOMINATED) {
            ++newSolutionsAdded;
            anyNewInThisIter = true;
          }
        }
      }
    }

    std::cout << "[SS] Round " << (ssIter + 1) << "/" << config_.maxScatterIters
              << ": " << pairCount
              << " pairs via PR, total new=" << newSolutionsAdded << std::endl;

    // ─── Stagnation handling ───
    if (!anyNewInThisIter) {
      itersWithoutImprovement++;
      if (itersWithoutImprovement >= config_.stagnationThreshold) {
        std::cout << "[SS] Stagnation detected (" << itersWithoutImprovement
                  << " rounds). Injecting perturbed solutions..." << std::endl;

        // Perturb Tier 1 solutions: destroy 30% customers, then improve
        for (auto &t1Sol : tier1_) {
          Solution perturbed = t1Sol;

          // Destroy: remove ~30% of customers randomly
          std::vector<int> allCustomers;
          for (const auto &route : perturbed.getRoutes()) {
            for (int c : route.getCustomers()) {
              allCustomers.push_back(c);
            }
          }
          int toRemove =
              std::max(1, static_cast<int>(allCustomers.size() * 0.3));
          std::shuffle(allCustomers.begin(), allCustomers.end(), rng_);
          for (int k = 0;
               k < toRemove && k < static_cast<int>(allCustomers.size()); ++k) {
            perturbed.removeCustomer(allCustomers[k]);
          }
          perturbed.removeEmptyRoutes();
          perturbed.evaluateRoutes();

          // Repair + improve via ALNS mini-loop
          solver.improveSolution(perturbed,
                                 config_.alnsItersPerCombination * 2);
          perturbed.evaluateRoutes();

          if (perturbed.isFeasible()) {
            AddResult res = archive.tryAdd(perturbed);
            if (res == AddResult::DOMINATING ||
                res == AddResult::NON_DOMINATED) {
              ++newSolutionsAdded;
              std::cout << "[SS] Perturbation added: Veh="
                        << perturbed.getTotalVehicles()
                        << ", Dist=" << perturbed.getTotalDistance()
                        << std::endl;
            }
          }
        }
        itersWithoutImprovement = 0;
      }
    } else {
      itersWithoutImprovement = 0;
    }
  }

done:
  std::cout << "[SS] Scatter Search finished. Total new solutions: "
            << newSolutionsAdded << std::endl;
  return newSolutionsAdded;
}

} // namespace alns
