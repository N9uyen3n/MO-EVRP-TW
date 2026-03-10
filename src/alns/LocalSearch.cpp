// =============================================================================
// LocalSearch.cpp — Optimized
// =============================================================================
// OPTIMIZATIONS APPLIED (vs original):
//
// [OPT-1] demandById_  — Flat O(1) demand lookup (like nodeTypeById_ for type).
//         Eliminates getNodeById()->getDemand() calls inside hot inner loops.
//         *** Requires adding `std::vector<double> demandById_;` to LocalSearch.h ***
//
// [OPT-2] nodeTypeById_ usage completed — All remaining getNodeById()->getType()
//         calls in searchSwap, searchCrossExchange, repositionStations,
//         searchStationSwap, removeRedundantStations replaced with O(1) array lookup.
//
// [OPT-3] searchInterTwoOpt — Route splice via direct vector construction
//         instead of repeated O(N) removeNode/addNode calls. O(N²) → O(N).
//
// [OPT-4] customer_pool erase — swap-and-pop O(1) instead of erase+remove O(N).
//         Applied in tryEliminateSmallestRoute and ejectionChain.
//
// [OPT-5] findBestInsertionPositions_KNN — unordered_set<int> for O(1) neighbor
//         lookup, vector<bool> for O(1) position dedup. Was O(n·K) with std::set.
//
// [OPT-6] thread_local candidate buffers — reuse heap allocation across calls
//         in findBestInsertionPositions_TimeAware and getTopKInsertionPositions.
//
// [OPT-7] calculateEuclideanDistance — std::hypot replaces sqrt(pow+pow).
//
// [OPT-8] getDemand() double-call in ejectionChain::findDirect removed (one call).
//
// =============================================================================

#include "../../include/alns/LocalSearch.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Station.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>

LocalSearch::LocalSearch(std::shared_ptr<Instance> inst) : instance(inst) {
  for (const auto &node : instance->getNodes()) {
    if (node->getType() == NodeType::STATION) {
      stationIds.push_back(node->getId());
    }
  }

  // Preprocess K-Nearest Neighbors
  preprocessKNN();

  // Preprocess Granular Neighborhoods
  preprocessGranularity();

  // Preprocess TW Next
  preprocessTWNext();

  // Precompute static Node Type list to avoid repeated hashtable lookups
  int maxNodeId = 0;
  for (const auto &node : instance->getNodes()) {
    maxNodeId = std::max(maxNodeId, node->getId());
  }

  nodeTypeById_.resize(maxNodeId + 1, NodeType::DEPOT);
  for (const auto &node : instance->getNodes()) {
    nodeTypeById_[node->getId()] = node->getType();
  }

  // [OPT-1] Precompute flat demand cache — eliminates getNodeById()->getDemand()
  // in hot inner loops (searchCrossExchange, ejectionChain, etc.)
  demandById_.assign(maxNodeId + 1, 0.0);
  for (const auto &cust : instance->getCustomers()) {
    demandById_[cust->getId()] = cust->getDemand();
  }
}

// ============================================================================
// Algorithm: 4-Phase Hybrid Local Search
// ============================================================================
void LocalSearch::run(Solution &solution) {
  // ⭐ VITAL: Must invalidate context from previous ALNS iterations
  searchContext_.invalidate();
  noImprovementCount_ = 0;

  // ── 3-Phase LS loop ────────────────────────────────────────────────────────
  // Phase 1: VND distance optimization  (every iter)
  // Phase 2: Charging cleanup           (every CHARGING_FREQUENCY iters)
  // Phase 3: Vehicle reduction          (every iter when routes > 1)
  //          Merged: ejectionChain + tryEliminate + smartMerge
  //          ElectricityFree was redundant with smartMerge — removed.
  //
  // Removed modulo throttling for vehicle reduction: if Phase 1 finds a
  // better solution the vehicle reduction should run immediately, not wait
  // for the counter to tick over.
  // ──────────────────────────────────────────────────────────────────────────
  for (int iter = 0; iter < MAX_LS_ITERATIONS; ++iter) {
    bool improved = false;

    updateSearchContext(solution);

    // --- Phase 1: Distance Optimization (VND) ---
    bool phase1Improved = runDistanceOptimization(solution);
    if (phase1Improved) {
      improved = true;
      searchContext_.invalidate();
    }

    // --- Phase 2: Charging Optimization (periodic) ---
    if (iter % CHARGING_FREQUENCY == 0) {
      if (runChargingOptimization(solution)) {
        improved = true;
        searchContext_.invalidate();
      }
    }

    // --- Phase 3: Vehicle Reduction (every iter, no throttle) ---
    // CHỈ chạy khi Phase 1 vừa improve, HOẶC mỗi 2 iter làm fallback
    if (solution.getRoutes().size() > 1) {
      if (phase1Improved || iter % 2 == 0) {
        if (runVehicleReduction(solution)) {
          improved = true;
          noImprovementCount_ = 0;
          searchContext_.invalidate();
        }
      }
    }

    // --- Adaptive Neighborhood Sizing ---
    if (improved) {
      solution.evaluateRoutes();
      noImprovementCount_ = 0;
      maxNodesToCheck_ = std::max(MIN_NODES_TO_CHECK, maxNodesToCheck_ - 1);
      maxSwapAttempts_ = std::max(MIN_SWAP_ATTEMPTS, maxSwapAttempts_ - 1);
    } else {
      noImprovementCount_++;
      maxNodesToCheck_ = std::min(MAX_NODES_TO_CHECK, maxNodesToCheck_ + 1);
      maxSwapAttempts_ = std::min(MAX_SWAP_ATTEMPTS, maxSwapAttempts_ + 1);

      if (noImprovementCount_ >= EARLY_STOP_THRESHOLD)
        break;
    }
  }
}

void LocalSearch::evaluateMove(const Solution &solution, MoveDescriptor &move,
                               const LocalSearchWeights &weights) {
  move.eval.reset();
  move.eval.isFeasible = false;
  const auto &routes = solution.getRoutes();

  // ================================================================
  // ⭐ FAST PATH: Distance pre-check BEFORE copying routes
  // ================================================================

  // --- INTER_RELOCATE: Full fast path ---
  if (move.type == MoveType::INTER_RELOCATE) {
    int nodeId = routes[move.routeIdx1].getNodeAt(move.nodeIdx1);
    const auto &r2 = routes[move.routeIdx2];

    InsertionResult result = r2.checkInsertionCost(nodeId, move.nodeIdx2);
    if (!result.isFeasible) {
      return;
    }

    double distance_removed;
    if (move.cachedRemovalSavings != 0.0) {
      distance_removed = move.cachedRemovalSavings;
    } else {
      const auto &r1 = routes[move.routeIdx1];
      int prev_node = r1.getNodeAt(move.nodeIdx1 - 1);
      int next_node = r1.getNodeAt(move.nodeIdx1 + 1);
      distance_removed = instance->getDistance(prev_node, nodeId) +
                         instance->getDistance(nodeId, next_node) -
                         instance->getDistance(prev_node, next_node);
    }

    move.eval.isFeasible = true;
    move.eval.distanceDelta = result.deltaDistance - distance_removed;
    move.eval.objectiveDelta = move.eval.distanceDelta * weights.dist;
    return;
  }

  // --- INTRA_RELOCATE: Distance delta O(1) pre-check ---
  if (move.type == MoveType::INTRA_RELOCATE) {
    const auto &r1 = routes[move.routeIdx1];
    int nodeId = r1.getNodeAt(move.nodeIdx1);

    int prev = r1.getNodeAt(move.nodeIdx1 - 1);
    int next = r1.getNodeAt(move.nodeIdx1 + 1);
    double removalSavings = instance->getDistance(prev, nodeId) +
                            instance->getDistance(nodeId, next) -
                            instance->getDistance(prev, next);

    int insertPos = move.nodeIdx2;
    if (move.nodeIdx1 < move.nodeIdx2)
      insertPos--;

    const auto &nodes = r1.getNodes();
    int ins_prev = nodes[insertPos - 1];
    int ins_next = nodes[insertPos];
    double insertionCost = instance->getDistance(ins_prev, nodeId) +
                           instance->getDistance(nodeId, ins_next) -
                           instance->getDistance(ins_prev, ins_next);

    double distDelta = insertionCost - removalSavings;
    if (distDelta >= -1e-9)
      return;
  }

  // --- INTRA_TWO_OPT: Skip copy if segment has no station ---
  if (move.type == MoveType::INTRA_TWO_OPT) {
    const auto &r1 = routes[move.routeIdx1];
    const auto &nodes = r1.getNodes();

    bool hasStation = (move.stationId == 1);

    if (!hasStation) {
      int prev   = nodes[move.nodeIdx1 - 1];
      int nodeI  = nodes[move.nodeIdx1];
      int nodeJ  = nodes[move.nodeIdx2];
      int next_node = nodes[move.nodeIdx2 + 1];

      double oldDist = instance->getDistance(prev, nodeI) +
                       instance->getDistance(nodeJ, next_node);
      double newDist = instance->getDistance(prev, nodeJ) +
                       instance->getDistance(nodeI, next_node);

      if (newDist >= oldDist - 1e-9)
        return;
    }
  }

  // ================================================================
  // END FAST PATH — only moves with good distance improvement reach here
  // ================================================================

  Route r1_copy = routes[move.routeIdx1];

  bool twoRoutes = (move.routeIdx1 != move.routeIdx2 && move.routeIdx2 >= 0);
  Route r2_copy = twoRoutes ? routes[move.routeIdx2] : r1_copy;

  try {
    switch (move.type) {
    case MoveType::INTRA_RELOCATE:
    case MoveType::INTER_RELOCATE: {
      int nodeId = r1_copy.getNodeAt(move.nodeIdx1);
      r1_copy.removeNode(move.nodeIdx1);
      if (twoRoutes) {
        r2_copy.addNode(nodeId, move.nodeIdx2);
      } else {
        int targetIdx = move.nodeIdx2;
        if (move.nodeIdx1 < move.nodeIdx2)
          targetIdx--;
        r1_copy.addNode(nodeId, targetIdx);
      }
      break;
    }
    case MoveType::INTER_OR_OPT: {
      std::vector<int> segment;
      segment.reserve(move.segmentLength);
      for (int i = 0; i < move.segmentLength; ++i) {
        segment.push_back(r1_copy.getNodeAt(move.nodeIdx1 + i));
      }
      for (int i = move.segmentLength - 1; i >= 0; --i) {
        r1_copy.removeNode(move.nodeIdx1 + i);
      }
      if (twoRoutes) {
        for (int i = 0; i < move.segmentLength; ++i) {
          r2_copy.addNode(segment[i], move.nodeIdx2 + i);
        }
      } else {
        int targetIdx = move.nodeIdx2;
        if (move.nodeIdx1 < move.nodeIdx2) {
          targetIdx -= move.segmentLength;
        }
        for (int i = 0; i < move.segmentLength; ++i) {
          r1_copy.addNode(segment[i], targetIdx + i);
        }
      }
      break;
    }
    case MoveType::INTRA_TWO_OPT:
      r1_copy.reverseNodes(move.nodeIdx1, move.nodeIdx2);
      break;
    case MoveType::INTER_CROSS_EXCHANGE: {
      if (!twoRoutes)
        return;
      int len1 = move.segmentLength;
      int len2 = move.segmentLength2;

      std::vector<int> seg1, seg2;
      seg1.reserve(len1);
      seg2.reserve(len2);
      for (int i = 0; i < len1; ++i)
        seg1.push_back(r1_copy.getNodeAt(move.nodeIdx1 + i));
      for (int i = 0; i < len2; ++i)
        seg2.push_back(r2_copy.getNodeAt(move.nodeIdx2 + i));

      for (int i = len1 - 1; i >= 0; --i)
        r1_copy.removeNode(move.nodeIdx1 + i);
      for (int i = len2 - 1; i >= 0; --i)
        r2_copy.removeNode(move.nodeIdx2 + i);

      for (int i = 0; i < len2; ++i)
        r1_copy.addNode(seg2[i], move.nodeIdx1 + i);
      for (int i = 0; i < len1; ++i)
        r2_copy.addNode(seg1[i], move.nodeIdx2 + i);
      break;
    }
    case MoveType::INTRA_SWAP: {
      int nodeId1 = r1_copy.getNodeAt(move.nodeIdx1);
      int nodeId2 = r1_copy.getNodeAt(move.nodeIdx2);
      r1_copy.removeNode(move.nodeIdx1);
      r1_copy.addNode(nodeId2, move.nodeIdx1);
      r1_copy.removeNode(move.nodeIdx2);
      r1_copy.addNode(nodeId1, move.nodeIdx2);
      break;
    }
    case MoveType::INTER_SWAP: {
      if (!twoRoutes)
        return;
      int nodeId1 = r1_copy.getNodeAt(move.nodeIdx1);
      int nodeId2 = r2_copy.getNodeAt(move.nodeIdx2);
      r1_copy.removeNode(move.nodeIdx1);
      r2_copy.removeNode(move.nodeIdx2);
      r1_copy.addNode(nodeId2, move.nodeIdx1);
      r2_copy.addNode(nodeId1, move.nodeIdx2);
      break;
    }
    case MoveType::STATION_REMOVE: {
      r1_copy.removeNode(move.nodeIdx1);
      break;
    }
    case MoveType::INTER_TWO_OPT:
      return;
    default:
      return;
    }

    r1_copy.evaluate();
    if (twoRoutes)
      r2_copy.evaluate();

    if (!r1_copy.isFeasible() || (twoRoutes && !r2_copy.isFeasible())) {
      return;
    }

    double newDist = r1_copy.getTotalDistance() +
                     (twoRoutes ? r2_copy.getTotalDistance() : 0);
    double oldDist =
        routes[move.routeIdx1].getTotalDistance() +
        (twoRoutes ? routes[move.routeIdx2].getTotalDistance() : 0);

    move.eval.isFeasible = true;
    move.eval.distanceDelta = newDist - oldDist;
    move.eval.objectiveDelta = move.eval.distanceDelta * weights.dist;

  } catch (...) {
    move.eval.isFeasible = false;
  }
}

void LocalSearch::applyMove(Solution &solution, const MoveDescriptor &move) {
  auto &routes = solution.getRoutes();

  switch (move.type) {
  case MoveType::INTRA_RELOCATE: {
    Route &r1 = routes[move.routeIdx1];
    int nodeId = r1.getNodeAt(move.nodeIdx1);
    r1.removeNode(move.nodeIdx1);
    int targetIdx = move.nodeIdx2;
    if (move.nodeIdx1 < move.nodeIdx2)
      targetIdx--;
    r1.addNode(nodeId, targetIdx);
    r1.evaluate();
    break;
  }
  case MoveType::INTER_RELOCATE: {
    Route &r1 = routes[move.routeIdx1];
    Route &r2 = routes[move.routeIdx2];
    int nodeId = r1.getNodeAt(move.nodeIdx1);
    r1.removeNode(move.nodeIdx1);
    r2.addNode(nodeId, move.nodeIdx2);
    r1.evaluate();
    r2.evaluate();
    break;
  }
  case MoveType::INTER_OR_OPT: {
    Route &r1 = routes[move.routeIdx1];
    std::vector<int> segment;
    segment.reserve(move.segmentLength);
    for (int i = 0; i < move.segmentLength; ++i) {
      segment.push_back(r1.getNodeAt(move.nodeIdx1 + i));
    }
    for (int i = move.segmentLength - 1; i >= 0; --i) {
      r1.removeNode(move.nodeIdx1 + i);
    }
    bool isInterRoute =
        (move.routeIdx1 != move.routeIdx2 && move.routeIdx2 >= 0);
    if (isInterRoute) {
      Route &r2 = routes[move.routeIdx2];
      for (int i = 0; i < move.segmentLength; ++i) {
        r2.addNode(segment[i], move.nodeIdx2 + i);
      }
      r1.evaluate();
      r2.evaluate();
    } else {
      int targetIdx = move.nodeIdx2;
      if (move.nodeIdx1 < move.nodeIdx2) {
        targetIdx -= move.segmentLength;
      }
      for (int i = 0; i < move.segmentLength; ++i) {
        r1.addNode(segment[i], targetIdx + i);
      }
      r1.evaluate();
    }
    break;
  }
  case MoveType::INTRA_TWO_OPT: {
    Route &r1 = routes[move.routeIdx1];
    r1.reverseNodes(move.nodeIdx1, move.nodeIdx2);
    r1.evaluate();
    break;
  }
  case MoveType::INTRA_SWAP: {
    Route &r1 = routes[move.routeIdx1];
    int nodeId1 = r1.getNodeAt(move.nodeIdx1);
    int nodeId2 = r1.getNodeAt(move.nodeIdx2);
    r1.removeNode(move.nodeIdx1);
    r1.addNode(nodeId2, move.nodeIdx1);
    r1.removeNode(move.nodeIdx2);
    r1.addNode(nodeId1, move.nodeIdx2);
    r1.evaluate();
    break;
  }
  case MoveType::INTER_SWAP: {
    Route &r1 = routes[move.routeIdx1];
    Route &r2 = routes[move.routeIdx2];
    int nodeId1 = r1.getNodeAt(move.nodeIdx1);
    int nodeId2 = r2.getNodeAt(move.nodeIdx2);
    r1.removeNode(move.nodeIdx1);
    r2.removeNode(move.nodeIdx2);
    r1.addNode(nodeId2, move.nodeIdx1);
    r2.addNode(nodeId1, move.nodeIdx2);
    r1.evaluate();
    r2.evaluate();
    break;
  }
  case MoveType::STATION_REMOVE: {
    Route &r1 = routes[move.routeIdx1];
    r1.removeNode(move.nodeIdx1);
    r1.evaluate();
    break;
  }
  case MoveType::INTER_CROSS_EXCHANGE: {
    Route &r1 = routes[move.routeIdx1];
    Route &r2 = routes[move.routeIdx2];
    int len1 = move.segmentLength;
    int len2 = move.segmentLength2;

    std::vector<int> seg1, seg2;
    seg1.reserve(len1);
    seg2.reserve(len2);
    for (int i = 0; i < len1; ++i)
      seg1.push_back(r1.getNodeAt(move.nodeIdx1 + i));
    for (int i = 0; i < len2; ++i)
      seg2.push_back(r2.getNodeAt(move.nodeIdx2 + i));

    for (int i = len1 - 1; i >= 0; --i)
      r1.removeNode(move.nodeIdx1 + i);
    for (int i = len2 - 1; i >= 0; --i)
      r2.removeNode(move.nodeIdx2 + i);

    for (int i = 0; i < len2; ++i)
      r1.addNode(seg2[i], move.nodeIdx1 + i);
    for (int i = 0; i < len1; ++i)
      r2.addNode(seg1[i], move.nodeIdx2 + i);

    r1.evaluate();
    r2.evaluate();
    break;
  }
  case MoveType::INTER_TWO_OPT:
    break;
  default:
    break;
  }

  searchContext_.markDirty(move.routeIdx1, move.routeIdx2);
  solution.markDirty();
}

// ============================================================================
// Phase 1: Distance Optimization — Variable Neighborhood Descent (VND)
// ============================================================================
bool LocalSearch::runDistanceOptimization(Solution &solution) {
  // [CHANGE-3] Removed dead `bestMove` param — all operators use first-
  // improvement and apply moves immediately via activeMove scratch buffer.
  // outBestMove was never populated; passing it was misleading.
  LocalSearchWeights weights;
  weights.dist = 1.0;
  bool anyImproved = false;

  int k = 0;
  while (k < 6) {
    bool improved = false;

    switch (k) {
    case 0: improved = searchRelocate(solution, weights, searchContext_);    break;
    case 1: improved = searchSwap(solution, weights, searchContext_);        break;
    case 2: improved = searchOrOpt(solution, weights);                       break;
    case 3: improved = searchTwoOpt(solution, weights);                      break;
    case 4: improved = searchInterTwoOpt(solution, weights, searchContext_); break;
    case 5: improved = searchCrossExchange(solution, weights, searchContext_); break;
    }

    if (improved) {
      anyImproved = true;
      k = 0;
    } else {
      k++;
    }
  }

  return anyImproved;
}

bool LocalSearch::searchRelocate(Solution &solution,
                                 const LocalSearchWeights &weights,
                                 SearchContext &ctx) {
  MoveDescriptor move; // [CHANGE-5] local — was member move
  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  if (!ctx.isValid || ctx.centroids.size() != numRoutes ||
      ctx.neighborLists.size() != numRoutes) {
    return false;
  }

  const auto &centroids    = ctx.centroids;
  const auto &neighborLists = ctx.neighborLists;

  for (int r1 = 0; r1 < numRoutes; ++r1) {
    if (routes[r1].size() <= 2)
      continue;

    const auto &rankedNodes = this->getCachedRanking(r1, routes[r1], ctx);
    int nodesToCheck = std::min(maxNodesToCheck_, (int)rankedNodes.size());

    for (int k = 0; k < nodesToCheck; ++k) {
      int i      = rankedNodes[k].first;
      int nodeId = routes[r1].getNodeAt(i);
      if (nodeTypeById_[nodeId] != NodeType::CUSTOMER)
        continue;

      // Compute removal savings once per node — reused for all (r2, j)
      int prev_n = routes[r1].getNodeAt(i - 1);
      int next_n = routes[r1].getNodeAt(i + 1);
      double removalSavings = instance->getDistance(prev_n, nodeId) +
                              instance->getDistance(nodeId, next_n) -
                              instance->getDistance(prev_n, next_n);

      for (int r2 : neighborLists[r1]) {
        auto candidatePositions =
            findBestInsertionPositions_KNN(routes[r2], nodeId, 4);

        for (size_t j : candidatePositions) {
          if (r1 == r2 && (j == i || j == i + 1))
            continue;

          if (!routes[r2].canPossiblyInsert(nodeId, j, (r1 == r2) ? nodeId : -1)) {
            continue;
          }

          move.reset();
          move.type =
              (r1 == r2) ? MoveType::INTRA_RELOCATE : MoveType::INTER_RELOCATE;
          move.routeIdx1 = r1;
          move.nodeIdx1  = i;
          move.routeIdx2 = r2;
          move.nodeIdx2  = j;
          move.cachedRemovalSavings = removalSavings;

          MoveEvaluation preEval = evaluateRelocateDelta(solution, move);

          if (!preEval.isFeasible || preEval.distanceDelta >= -1e-9) {
            continue;
          }

          evaluateMove(solution, move, weights);

          if (move.eval.isFeasible &&
              move.eval.objectiveDelta < -1e-9) {
            applyMove(solution, move);
            return true;
          } else if (!move.eval.isFeasible &&
                     preEval.distanceDelta < -1.0) {  // [FIX-2] Relaxed from -5.0 → -1.0
            auto &routes = solution.getRoutes();
            Route testRouteFrom = routes[r1];
            Route testRouteTo   = routes[r2];

            if (tryRelocateWithEnergyBoost(testRouteFrom, i, testRouteTo, j, 30.0)) {
              routes[r1] = testRouteFrom;
              routes[r2] = testRouteTo;
              solution.evaluateRoutes();
              searchContext_.markDirty(r1, r2);
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

bool LocalSearch::searchTwoOpt(Solution &solution,
                               const LocalSearchWeights &weights) {
  MoveDescriptor move; // [CHANGE-5] local — was member move
  auto &routes = solution.getRoutes();

  for (int r = 0; r < (int)routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();
    int n = nodes.size();

    if (n < 4)
      continue;

    for (int i = 1; i < n - 2; ++i) {
      for (int j = i + 1; j < n - 1; ++j) {
        int prev  = nodes[i - 1];
        int nodeI = nodes[i];
        int nodeJ = nodes[j];
        int next  = nodes[j + 1];

        double oldDist = instance->getDistance(prev, nodeI) +
                         instance->getDistance(nodeJ, next);
        double newDist = instance->getDistance(prev, nodeJ) +
                         instance->getDistance(nodeI, next);

        if (newDist >= oldDist - 1e-6)
          continue;

        // ⭐ Granular Filter
        if (instance->getDistance(prev, nodeJ) >= distanceThreshold_ &&
            instance->getDistance(nodeI, next) >= distanceThreshold_) {
          continue;
        }

        move.reset();
        move.type     = MoveType::INTRA_TWO_OPT;
        move.routeIdx1 = r;
        move.nodeIdx1  = i;
        move.routeIdx2 = r;
        move.nodeIdx2  = j;

        {
          bool segHasStation = false;
          for (int k = i; k <= j && !segHasStation; ++k)
            if (nodeTypeById_[nodes[k]] == NodeType::STATION)
              segHasStation = true;
          move.stationId = segHasStation ? 1 : 0;
        }

        evaluateMove(solution, move, weights);

        if (move.eval.isFeasible &&
            move.eval.objectiveDelta < -1e-9) {
          applyMove(solution, move);
          return true;
        }
      }
    }
  }

  return false;
}

// ============================================================================
// Inter-route 2-Opt: Cross-route edge reconnection
// [OPT-3] Build new routes via direct vector splice instead of O(N²)
//         repeated removeNode/addNode calls.
// ============================================================================
bool LocalSearch::searchInterTwoOpt(Solution &solution,
                                    const LocalSearchWeights &weights,
                                    SearchContext &ctx) {
  auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  if (numRoutes < 2)
    return false;

  if (!ctx.isValid || (int)ctx.centroids.size() != numRoutes)
    return false;

  for (int r1 = 0; r1 < numRoutes; ++r1) {
    const auto &nodes1 = routes[r1].getNodes();
    int n1 = (int)nodes1.size();
    if (n1 < 3)
      continue;

    for (int r2 : ctx.neighborLists[r1]) {
      if (r2 <= r1)
        continue;

      const auto &nodes2 = routes[r2].getNodes();
      int n2 = (int)nodes2.size();
      if (n2 < 3)
        continue;

      for (int i = 1; i < n1 - 1; ++i) {
        for (int j = 1; j < n2 - 1; ++j) {
          double oldEdges = instance->getDistance(nodes1[i], nodes1[i + 1]) +
                            instance->getDistance(nodes2[j], nodes2[j + 1]);
          double newEdges = instance->getDistance(nodes1[i], nodes2[j + 1]) +
                            instance->getDistance(nodes2[j], nodes1[i + 1]);

          if (newEdges >= oldEdges - 0.5)
            continue;

          // [OPT-3] Build new route sequences directly from node vectors
          // newR1 = nodes1[0..i] + nodes2[j+1..n2-2] + depot_end
          // newR2 = nodes2[0..j] + nodes1[i+1..n1-2] + depot_end
          // Avoids O(N²) repeated removeNode/addNode

          // Build node sequences
          std::vector<int> seq1, seq2;
          seq1.reserve(i + 1 + (n2 - j - 2) + 1);
          seq2.reserve(j + 1 + (n1 - i - 2) + 1);

          for (int k = 0; k <= i; ++k)
            seq1.push_back(nodes1[k]);
          for (int k = j + 1; k <= n2 - 2; ++k)
            seq1.push_back(nodes2[k]);
          seq1.push_back(nodes1[n1 - 1]); // trailing depot

          for (int k = 0; k <= j; ++k)
            seq2.push_back(nodes2[k]);
          for (int k = i + 1; k <= n1 - 2; ++k)
            seq2.push_back(nodes1[k]);
          seq2.push_back(nodes2[n2 - 1]); // trailing depot

          // Build Route objects from sequences
          Route newR1 = routes[r1];
          newR1.clear();
          for (size_t k = 1; k + 1 < seq1.size(); ++k)
            newR1.addNode(seq1[k], k);

          Route newR2 = routes[r2];
          newR2.clear();
          for (size_t k = 1; k + 1 < seq2.size(); ++k)
            newR2.addNode(seq2[k], k);

          newR1.evaluate();
          newR2.evaluate();

          if (!newR1.isFeasible() || !newR2.isFeasible())
            continue;

          double oldDist = routes[r1].getTotalDistance() + routes[r2].getTotalDistance();
          double newDist = newR1.getTotalDistance() + newR2.getTotalDistance();

          if (newDist < oldDist - 1e-9) {
            routes[r1] = newR1;
            routes[r2] = newR2;
            solution.evaluateRoutes();
            ctx.markDirty(r1, r2);
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool LocalSearch::searchSwap(Solution &solution,
                             const LocalSearchWeights &weights,
                             SearchContext &ctx) {
  MoveDescriptor move; // [CHANGE-5] local — was member move
  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  if (!ctx.isValid || ctx.centroids.size() != numRoutes) {
    return false;
  }

  // INTRA-ROUTE SWAP
  for (int r = 0; r < numRoutes; ++r) {
    const auto &nodes = routes[r].getNodes();
    int n = nodes.size();
    if (n < 4)
      continue;

    for (int i = 1; i < n - 2; ++i) {
      // [OPT-2] Use flat cache instead of getNodeById()->getType()
      if (nodeTypeById_[nodes[i]] != NodeType::CUSTOMER)
        continue;

      for (int j = i + 2; j < n - 1; ++j) {
        // [OPT-2]
        if (nodeTypeById_[nodes[j]] != NodeType::CUSTOMER)
          continue;

        int prev_i = nodes[i - 1], next_i = nodes[i + 1];
        int prev_j = nodes[j - 1], next_j = nodes[j + 1];
        double oldDist = instance->getDistance(prev_i, nodes[i]) +
                         instance->getDistance(nodes[i], next_i) +
                         instance->getDistance(prev_j, nodes[j]) +
                         instance->getDistance(nodes[j], next_j);
        double newDist = instance->getDistance(prev_i, nodes[j]) +
                         instance->getDistance(nodes[j], next_i) +
                         instance->getDistance(prev_j, nodes[i]) +
                         instance->getDistance(nodes[i], next_j);

        if (newDist >= oldDist - 1e-6)
          continue;

        if (!routes[r].canPossiblyInsert(nodes[j], i, nodes[i]) ||
            !routes[r].canPossiblyInsert(nodes[i], j, nodes[j])) {
          continue;
        }

        move.reset();
        move.type      = MoveType::INTRA_SWAP;
        move.routeIdx1 = r;
        move.nodeIdx1  = i;
        move.routeIdx2 = r;
        move.nodeIdx2  = j;

        evaluateMove(solution, move, weights);

        if (move.eval.isFeasible &&
            move.eval.objectiveDelta < -1e-9) {
          applyMove(solution, move);
          return true;
        }
      }
    }
  }

  // INTER-ROUTE SWAP
  for (int r1 = 0; r1 < numRoutes; ++r1) {
    for (int r2 : ctx.neighborLists[r1]) {
      if (r2 <= r1)
        continue;

      const auto &nodes1 = routes[r1].getNodes();
      const auto &nodes2 = routes[r2].getNodes();

      const auto &ranked1 = this->getCachedRanking(r1, routes[r1], ctx);
      const auto &ranked2 = this->getCachedRanking(r2, routes[r2], ctx);

      int attempts1 = std::min(maxSwapAttempts_, (int)ranked1.size());
      int attempts2 = std::min(maxSwapAttempts_, (int)ranked2.size());

      for (int k1 = 0; k1 < attempts1; ++k1) {
        int i = ranked1[k1].first;
        // [OPT-2]
        if (nodeTypeById_[nodes1[i]] != NodeType::CUSTOMER)
          continue;

        for (int k2 = 0; k2 < attempts2; ++k2) {
          int j = ranked2[k2].first;
          // [OPT-2]
          if (nodeTypeById_[nodes2[j]] != NodeType::CUSTOMER)
            continue;

          int nodeId1 = nodes1[i];
          int nodeId2 = nodes2[j];
          if (instance->getDistance(nodeId1, nodeId2) >= distanceThreshold_)
            continue;

          int prev1 = nodes1[i - 1], next1 = nodes1[i + 1];
          int prev2 = nodes2[j - 1], next2 = nodes2[j + 1];

          double oldDist = instance->getDistance(prev1, nodeId1) +
                           instance->getDistance(nodeId1, next1) +
                           instance->getDistance(prev2, nodeId2) +
                           instance->getDistance(nodeId2, next2);
          double newDist = instance->getDistance(prev1, nodeId2) +
                           instance->getDistance(nodeId2, next1) +
                           instance->getDistance(prev2, nodeId1) +
                           instance->getDistance(nodeId1, next2);

          if (newDist >= oldDist - 1e-9)
            continue;

          // TW feasibility check using twNext_ (precomputed matrix)
          if (!twNext_[prev1][nodeId2] || !twNext_[nodeId2][next1] ||
              !twNext_[prev2][nodeId1] || !twNext_[nodeId1][next2])
            continue;

          if (!routes[r1].canPossiblyInsert(nodeId2, i, nodeId1) ||
              !routes[r2].canPossiblyInsert(nodeId1, j, nodeId2)) {
            continue;
          }

          move.reset();
          move.type      = MoveType::INTER_SWAP;
          move.routeIdx1 = r1;
          move.nodeIdx1  = i;
          move.routeIdx2 = r2;
          move.nodeIdx2  = j;

          evaluateMove(solution, move, weights);

          if (move.eval.isFeasible &&
              move.eval.objectiveDelta < -1e-9) {
            applyMove(solution, move);
            return true;
          }
        }
      }
    }
  }

  return false;
}

bool LocalSearch::searchOrOpt(Solution &solution,
                              const LocalSearchWeights &weights) {
  MoveDescriptor move; // [CHANGE-5] local — was member move
  static const int SEGMENT_LENGTHS[] = {1, 2, 3};

  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  if (!searchContext_.isValid ||
      (int)searchContext_.neighborLists.size() != numRoutes) {
    return false;
  }
  const auto &neighborLists = searchContext_.neighborLists;

  for (int segLen : SEGMENT_LENGTHS) {
    for (int r1 = 0; r1 < numRoutes; ++r1) {
      if ((int)routes[r1].size() < 2 + segLen)
        continue;

      for (int i = 1; i <= (int)routes[r1].size() - 1 - segLen; ++i) {
        bool segmentIsCustomers = true;
        for (int k = 0; k < segLen; ++k) {
          if (nodeTypeById_[routes[r1].getNodeAt(i + k)] != NodeType::CUSTOMER) {
            segmentIsCustomers = false;
            break;
          }
        }
        if (!segmentIsCustomers)
          continue;

        int firstNodeId = routes[r1].getNodeAt(i);
        int lastNodeId  = routes[r1].getNodeAt(i + segLen - 1);

        int prev1 = routes[r1].getNodeAt(i - 1);
        int next1 = routes[r1].getNodeAt(i + segLen);
        double removalSavings = instance->getDistance(prev1, firstNodeId) +
                                instance->getDistance(lastNodeId, next1) -
                                instance->getDistance(prev1, next1);

        for (int r2 : neighborLists[r1]) {
          auto candidatePositions =
              findBestInsertionPositions_KNN(routes[r2], firstNodeId, 3);

          for (int j : candidatePositions) {
            if (r1 == r2 && (j >= i && j <= i + segLen)) {
              continue;
            }

            int prev2 = routes[r2].getNodeAt(j - 1);
            int next2 = routes[r2].getNodeAt(j);

            double insertionCost = instance->getDistance(prev2, firstNodeId) +
                                   instance->getDistance(lastNodeId, next2) -
                                   instance->getDistance(prev2, next2);

            if (r1 == r2) {
              if (j == i || j == i + segLen)
                continue;
            }

            if (insertionCost >= removalSavings - 1e-9) {
              continue;
            }

            move.reset();
            move.type        = MoveType::INTER_OR_OPT;
            move.routeIdx1   = r1;
            move.nodeIdx1    = i;
            move.routeIdx2   = r2;
            move.nodeIdx2    = j;
            move.segmentLength = segLen;

            evaluateMove(solution, move, weights);

            if (move.eval.isFeasible &&
                move.eval.objectiveDelta < -1e-9) {
              applyMove(solution, move);
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

// ============================================================================
// Phase 2: Charging Optimization
// ============================================================================
bool LocalSearch::runChargingOptimization(Solution &solution) {
  bool improved = false;

  if (removeRedundantStations(solution))
    improved = true;

  if (repositionStations(solution))
    improved = true;

  if (optimizeChargingAmounts(solution))
    improved = true;

  if (searchStationSwap(solution))
    improved = true;

  return improved;
}

bool LocalSearch::searchCrossExchange(Solution &solution,
                                      const LocalSearchWeights &weights,
                                      SearchContext &ctx) {
  MoveDescriptor move; // [CHANGE-5] local — was member move
  static const int SEGMENT_LENGTHS[] = {1, 2, 3};
  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  if (!ctx.isValid || (int)ctx.neighborLists.size() != numRoutes) {
    return false;
  }
  const auto &neighborLists = ctx.neighborLists;

  for (int len1 : SEGMENT_LENGTHS) {
    for (int len2 : SEGMENT_LENGTHS) {
      if (len1 == 1 && len2 == 1)
        continue;

      for (int r1 = 0; r1 < numRoutes; ++r1) {
        if ((int)routes[r1].size() < 2 + len1)
          continue;

        for (int r2 : neighborLists[r1]) {
          if (r2 <= r1)
            continue;
          if ((int)routes[r2].size() < 2 + len2)
            continue;

          for (int i = 1; i <= (int)routes[r1].size() - 1 - len1; ++i) {
            // [OPT-2] Use nodeTypeById_ instead of getNodeById()->getType()
            bool seg1Ok = true;
            for (int k = 0; k < len1; ++k) {
              if (nodeTypeById_[routes[r1].getNodeAt(i + k)] != NodeType::CUSTOMER) {
                seg1Ok = false;
                break;
              }
            }
            if (!seg1Ok)
              continue;

            int first1 = routes[r1].getNodeAt(i);
            int last1  = routes[r1].getNodeAt(i + len1 - 1);
            int prev1  = routes[r1].getNodeAt(i - 1);
            int next1  = routes[r1].getNodeAt(i + len1);

            double rem1 = instance->getDistance(prev1, first1) +
                          instance->getDistance(last1, next1) -
                          instance->getDistance(prev1, next1);

            for (int j = 1; j <= (int)routes[r2].size() - 1 - len2; ++j) {
              // [OPT-2]
              bool seg2Ok = true;
              for (int k = 0; k < len2; ++k) {
                if (nodeTypeById_[routes[r2].getNodeAt(j + k)] != NodeType::CUSTOMER) {
                  seg2Ok = false;
                  break;
                }
              }
              if (!seg2Ok)
                continue;

              int first2 = routes[r2].getNodeAt(j);
              int last2  = routes[r2].getNodeAt(j + len2 - 1);
              int prev2  = routes[r2].getNodeAt(j - 1);
              int next2  = routes[r2].getNodeAt(j + len2);

              if (instance->getDistance(first1, first2) >= distanceThreshold_)
                continue;

              // [OPT-1] Use flat demandById_ instead of getNodeById()->getDemand()
              double demand1 = 0.0, demand2 = 0.0;
              for (int k = 0; k < len1; ++k)
                demand1 += demandById_[routes[r1].getNodeAt(i + k)];
              for (int k = 0; k < len2; ++k)
                demand2 += demandById_[routes[r2].getNodeAt(j + k)];

              if (routes[r1].getTotalDemand() - demand1 + demand2 >
                  routes[r1].getVehicle()->getCapacity())
                continue;
              if (routes[r2].getTotalDemand() - demand2 + demand1 >
                  routes[r2].getVehicle()->getCapacity())
                continue;

              double rem2 = instance->getDistance(prev2, first2) +
                            instance->getDistance(last2, next2) -
                            instance->getDistance(prev2, next2);

              double ins1 = instance->getDistance(prev1, first2) +
                            instance->getDistance(last2, next1) -
                            instance->getDistance(prev1, next1);
              double ins2 = instance->getDistance(prev2, first1) +
                            instance->getDistance(last1, next2) -
                            instance->getDistance(prev2, next2);

              double distanceDelta = (ins1 + ins2) - (rem1 + rem2);
              if (distanceDelta >= -1e-9)
                continue;

              move.reset();
              move.type          = MoveType::INTER_CROSS_EXCHANGE;
              move.routeIdx1     = r1;
              move.nodeIdx1      = i;
              move.routeIdx2     = r2;
              move.nodeIdx2      = j;
              move.segmentLength  = len1;
              move.segmentLength2 = len2;

              evaluateMove(solution, move, weights);

              if (move.eval.isFeasible &&
                  move.eval.objectiveDelta < -1e-9) {
                applyMove(solution, move);
                return true;
              }
            }
          }
        }
      }
    }
  }
  return false;
}

bool LocalSearch::searchStationRemoval(Solution &solution,
                                       MoveDescriptor &outBestMove,
                                       const LocalSearchWeights &weights) {
  outBestMove.reset();
  outBestMove.eval.objectiveDelta = -1e-5;
  bool found = false;

  auto &routes = solution.getRoutes();

  for (int r = 0; r < (int)routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();

    std::vector<int> stationPositions;
    for (int i = 1; i < (int)nodes.size() - 1; ++i) {
      if (nodeTypeById_[nodes[i]] == NodeType::STATION) {
        stationPositions.push_back(i);
      }
    }

    for (int stationPos : stationPositions) {
      Route routeCopy = routes[r];
      routeCopy.removeNode(stationPos);
      routeCopy.evaluate();

      if (!routeCopy.isFeasible())
        continue;

      double oldDist = routes[r].getTotalDistance();
      double newDist = routeCopy.getTotalDistance();
      double delta   = newDist - oldDist;

      if (delta < outBestMove.eval.objectiveDelta) {
        outBestMove.type            = MoveType::STATION_REMOVE;
        outBestMove.routeIdx1       = r;
        outBestMove.nodeIdx1        = stationPos;
        outBestMove.routeIdx2       = -1;
        outBestMove.nodeIdx2        = -1;
        outBestMove.eval.isFeasible  = true;
        outBestMove.eval.distanceDelta = delta;
        outBestMove.eval.objectiveDelta = delta;
        found = true;
      }
    }
  }

  return found;
}

bool LocalSearch::repositionStations(Solution &solution) {
  auto &routes = solution.getRoutes();

  for (int r = 0; r < (int)routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();

    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      int currentStationId = nodes[i];
      // [OPT-2]
      if (nodeTypeById_[currentStationId] != NodeType::STATION)
        continue;

      int prevNodeId = nodes[i - 1];
      int nextNodeId = nodes[i + 1];
      double currentDetour =
          instance->getDistance(prevNodeId, currentStationId) +
          instance->getDistance(currentStationId, nextNodeId);

      int    bestStationId = -1;
      double bestDetour    = currentDetour;

      for (int altStationId : stationIds) {
        if (altStationId == currentStationId)
          continue;

        double altDetour = instance->getDistance(prevNodeId, altStationId) +
                           instance->getDistance(altStationId, nextNodeId);

        if (altDetour < bestDetour - 1e-6) {
          auto altStation = instance->getNodeById(altStationId);
          const auto &prevState = routes[r].getStates()[i - 1];
          double arrivalTime = prevState.departureTime +
                               instance->getTime(prevNodeId, altStationId);

          if (arrivalTime <= altStation->getDueDate()) {
            bestDetour    = altDetour;
            bestStationId = altStationId;
          }
        }
      }

      if (bestStationId != -1) {
        Route testRoute = routes[r];
        testRoute.removeNode(i);
        testRoute.addNode(bestStationId, i);
        testRoute.evaluate();

        if (testRoute.isFeasible() &&
            testRoute.getTotalDistance() < routes[r].getTotalDistance()) {
          routes[r] = testRoute;
          return true;
        }
      }
    }
  }

  return false;
}

bool LocalSearch::searchStationSwap(Solution &solution) {
  auto &routes = solution.getRoutes();
  bool improved = false;
  const double EPSILON = 1e-9;

  for (auto &route : routes) {
    if (!route.isFeasible())
      continue;

    const auto &nodes = route.getNodes();
    if (nodes.size() <= 3)
      continue;

    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      int nodeId = nodes[i];
      // [OPT-2]
      if (nodeTypeById_[nodeId] != NodeType::STATION)
        continue;

      int prevId = nodes[i - 1];
      int nextId = nodes[i + 1];
      double currentDetour = instance->getDistance(prevId, nodeId) +
                             instance->getDistance(nodeId, nextId);

      int    bestReplacement = -1;
      double bestImprovement = 0.0;

      for (int candidateId : stationIds) {
        if (candidateId == nodeId)
          continue;

        double candidateDetour = instance->getDistance(prevId, candidateId) +
                                 instance->getDistance(candidateId, nextId);
        if (candidateDetour >= currentDetour - EPSILON)
          continue;

        Route testRoute = route;
        testRoute.removeNode(i);
        testRoute.addNode(candidateId, i);
        testRoute.evaluate();

        if (!testRoute.isFeasible())
          continue;

        double distImprovement =
            route.getTotalDistance() - testRoute.getTotalDistance();

        if (distImprovement > bestImprovement) {
          bestImprovement = distImprovement;
          bestReplacement = candidateId;
        }
      }

      if (bestReplacement != -1 && bestImprovement > EPSILON) {
        route.removeNode(i);
        route.addNode(bestReplacement, i);
        route.evaluate();
        improved = true;
        break;
      }
    }
  }

  return improved;
}

bool LocalSearch::optimizeChargingAmounts(Solution &solution) {
  auto &routes = solution.getRoutes();
  bool improved = false;

  for (int r = 0; r < (int)routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();
    const auto &states = routes[r].getStates();

    std::vector<std::pair<size_t, double>> stationInfos;
    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      if (nodeTypeById_[nodes[i]] == NodeType::STATION) {
        double chargeAmount =
            (i < states.size()) ? states[i].chargeAmount : 0.0;
        stationInfos.push_back({i, chargeAmount});
      }
    }

    // Strategy 1: Remove stations co-located with depot
    if (!stationInfos.empty()) {
      for (auto it = stationInfos.begin(); it != stationInfos.end();) {
        size_t pos       = it->first;
        int stationId    = nodes[pos];

        bool isAtDepotPosition = (pos == 1);

        auto stationNode = instance->getNodeById(stationId);
        auto depotNode   = instance->getNodeById(0);
        bool sameAsDepot =
            (std::abs(stationNode->getX() - depotNode->getX()) < 0.01 &&
             std::abs(stationNode->getY() - depotNode->getY()) < 0.01);

        if (sameAsDepot) {
          Route testRoute = routes[r];
          testRoute.removeNode(pos);
          testRoute.evaluate();

          if (testRoute.isFeasible()) {
            routes[r] = testRoute;
            improved  = true;
            break;
          }
        }
        ++it;
      }
    }

    if (improved)
      continue;

    // Strategy 2: Remove near-zero charge stations
    const auto &currentStates = routes[r].getStates();
    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      if (nodeTypeById_[nodes[i]] == NodeType::STATION) {
        double chargeAmount =
            (i < currentStates.size()) ? currentStates[i].chargeAmount : 0.0;

        if (chargeAmount < 1.0) {
          Route testRoute = routes[r];
          testRoute.removeNode(i);
          testRoute.evaluate();

          if (testRoute.isFeasible() &&
              testRoute.getTotalDistance() < routes[r].getTotalDistance()) {
            routes[r] = testRoute;
            improved  = true;
            break;
          }
        }
      }
    }

    if (improved)
      continue;

    // Strategy 3: Remove consecutive stations (keep one)
    const auto &currentNodes = routes[r].getNodes();
    std::vector<size_t> stationPositions;
    for (size_t i = 1; i < currentNodes.size() - 1; ++i) {
      if (nodeTypeById_[currentNodes[i]] == NodeType::STATION) {
        stationPositions.push_back(i);
      }
    }

    if (stationPositions.size() >= 2) {
      for (size_t idx = 0; idx < stationPositions.size() - 1; ++idx) {
        size_t pos1 = stationPositions[idx];
        size_t pos2 = stationPositions[idx + 1];

        if (pos2 - pos1 <= 3) {
          Route testRoute = routes[r];
          testRoute.removeNode(pos1);
          testRoute.evaluate();

          if (testRoute.isFeasible()) {
            routes[r] = testRoute;
            improved  = true;
            break;
          }

          testRoute = routes[r];
          testRoute.removeNode(pos2);
          testRoute.evaluate();

          if (testRoute.isFeasible()) {
            routes[r] = testRoute;
            improved  = true;
            break;
          }
        }
      }
    }

    // Strategy 4: Try removing any station that improves distance
    if (!improved && stationPositions.size() > 0) {
      for (size_t pos : stationPositions) {
        Route testRoute = routes[r];
        testRoute.removeNode(pos);
        testRoute.evaluate();

        if (testRoute.isFeasible() &&
            testRoute.getTotalDistance() < routes[r].getTotalDistance()) {
          routes[r] = testRoute;
          improved  = true;
          break;
        }
      }
    }
  }

  return improved;
}

bool LocalSearch::runVehicleReduction(Solution &solution) {
  if (solution.getNumRoutes() < 2)
    return false;

  // Order matters:
  // 1. ejectionChain   — strongest, handles energy-infeasible moves via chain
  // 2. tryEliminate    — direct insertion of smallest route into others
  // 3. smartMerge      — reconstruct best pair (replaces ElectricityFree)
  //
  // ElectricityFreeVehicleReduction was a subset of smartMerge (same pair
  // selection + reconstruction). Merged here to avoid redundant work.

  if (ejectionChain(solution))
    return true;

  if (tryEliminateSmallestRoute(solution))
    return true;

  if (runSmartMultiRouteMerge(solution))
    return true;

  return false;
}

// ============================================================================
// tryEliminateSmallestRoute
// [OPT-4] swap-and-pop for customer pool removal (O(1) vs O(N))
// ============================================================================
bool LocalSearch::tryEliminateSmallestRoute(Solution &solution) {
  auto &routes = solution.getRoutes();
  int numRoutes = routes.size();
  if (numRoutes < 2)
    return false;

  int    smallestIdx  = -1;
  size_t minCustomers = std::numeric_limits<size_t>::max();

  for (int r = 0; r < numRoutes; ++r) {
    size_t custCount = routes[r].getCustomers().size();
    if (custCount > 0 && custCount < minCustomers) {
      minCustomers = custCount;
      smallestIdx  = r;
    }
  }

  if (smallestIdx == -1 || minCustomers > 15)
    return false;

  std::vector<int> customersToMove = routes[smallestIdx].getCustomers();

  std::vector<Route> newRoutes;
  for (int r = 0; r < numRoutes; ++r) {
    if (r != smallestIdx) {
      newRoutes.push_back(routes[r]);
    }
  }

  struct InsertCandidate {
    int custId    = -1;
    int routeIdx  = -1;
    size_t pos    = 0;
    double cost   = 1e18;
    int stationId = -1;
    bool statBefore = true;
  };

  std::vector<int> unplaced = customersToMove;

  // Sort by tightest TW first
  std::sort(unplaced.begin(), unplaced.end(), [&](int a, int b) {
    auto ca = instance->getNodeById(a);
    auto cb = instance->getNodeById(b);
    double twA = ca->getDueDate() - ca->getReadyTime();
    double twB = cb->getDueDate() - cb->getReadyTime();
    return twA < twB;
  });

  auto findBestForCustomer = [&](int custId) -> InsertCandidate {
    InsertCandidate best;
    best.custId = custId;

    auto custNode = instance->getNodeById(custId);
    double twWindow = custNode->getDueDate() - custNode->getReadyTime();
    bool isTightTW  = (twWindow <= 30.0);

    for (int r = 0; r < (int)newRoutes.size(); ++r) {
      const auto &nodes  = newRoutes[r].getNodes();
      const auto &states = newRoutes[r].getStates();

      for (size_t pos = 1; pos < nodes.size(); ++pos) {
        // Option A: Direct
        if (newRoutes[r].canPossiblyInsert(custId, pos)) {
          InsertionResult res = newRoutes[r].checkInsertionCost(custId, pos);
          if (res.isFeasible) {
            double cost = res.deltaDistance;
            if (isTightTW && pos <= states.size()) {
              double travelToPrev = instance->getTime(nodes[pos - 1], custId);
              double arrivalAtCust = states[pos - 1].departureTime + travelToPrev;
              double slack = custNode->getDueDate() - arrivalAtCust;
              if (slack < 0) continue;
              double waitPenalty =
                  std::max(0.0, custNode->getReadyTime() - arrivalAtCust);
              cost += waitPenalty * 0.3;
            }
            if (cost < best.cost)
              best = {custId, r, pos, cost, -1, true};
          }
        }

        // Options B & C: Station-assisted, top-3 stations by detour
        int prevId    = nodes[pos - 1];
        int nextId    = nodes[pos];
        double directDist = instance->getDistance(prevId, nextId);

        struct SC { int id; double det; };
        std::vector<SC> sc;
        sc.reserve(stationIds.size());
        for (int sid : stationIds)
          sc.push_back({sid, instance->getDistance(prevId, sid) +
                               instance->getDistance(sid, nextId) - directDist});
        int topK = std::min(3, (int)sc.size());
        std::partial_sort(sc.begin(), sc.begin() + topK, sc.end(),
                          [](const SC &a, const SC &b) { return a.det < b.det; });

        for (int k = 0; k < topK; ++k) {
          int sid = sc[k].id;
          // B: [station, customer]
          {
            Route copy = newRoutes[r];
            copy.addNode(custId, pos);
            copy.addNode(sid, pos);
            copy.evaluate();
            if (copy.isFeasible()) {
              double dc = copy.getTotalDistance() - newRoutes[r].getTotalDistance();
              if (dc < best.cost)
                best = {custId, r, pos, dc, sid, true};
            }
          }
          // C: [customer, station]
          {
            Route copy = newRoutes[r];
            copy.addNode(custId, pos);
            copy.addNode(sid, pos + 1);
            copy.evaluate();
            if (copy.isFeasible()) {
              double dc = copy.getTotalDistance() - newRoutes[r].getTotalDistance();
              if (dc < best.cost)
                best = {custId, r, pos, dc, sid, false};
            }
          }
        }
      }
    }
    return best;
  };

  auto applyCandidate = [&](const InsertCandidate &c) {
    if (c.stationId == -1) {
      newRoutes[c.routeIdx].addNode(c.custId, c.pos);
    } else if (c.statBefore) {
      newRoutes[c.routeIdx].addNode(c.custId, c.pos);
      newRoutes[c.routeIdx].addNode(c.stationId, c.pos);
    } else {
      newRoutes[c.routeIdx].addNode(c.custId, c.pos);
      newRoutes[c.routeIdx].addNode(c.stationId, c.pos + 1);
    }
    newRoutes[c.routeIdx].evaluate();
  };

  // Regret-2 Insertion
  while (!unplaced.empty()) {
    int    bestIdx   = -1;
    double maxRegret = -1e18;
    InsertCandidate bestCand;

    for (int idx = 0; idx < (int)unplaced.size(); ++idx) {
      int cId = unplaced[idx];
      InsertCandidate rank1, rank2;
      rank1.custId = cId;
      rank2.custId = cId;
      rank2.cost   = 1e18;

      for (int r = 0; r < (int)newRoutes.size(); ++r) {
        for (size_t pos = 1; pos < newRoutes[r].size(); ++pos) {
          if (!newRoutes[r].canPossiblyInsert(cId, pos))
            continue;
          InsertionResult res = newRoutes[r].checkInsertionCost(cId, pos);
          if (!res.isFeasible) continue;
          if (res.deltaDistance < rank1.cost) {
            rank2 = rank1;
            rank1 = {cId, r, pos, res.deltaDistance, -1, true};
          } else if (res.deltaDistance < rank2.cost) {
            rank2 = {cId, r, pos, res.deltaDistance, -1, true};
          }
        }
      }

      if (rank1.routeIdx == -1) continue;

      double regret = (rank2.routeIdx == -1) ? 1e15 : (rank2.cost - rank1.cost);
      if (regret > maxRegret) {
        maxRegret = regret;
        bestIdx   = idx;
        bestCand  = rank1;
      }
    }

    if (bestIdx == -1)
      break;

    applyCandidate(bestCand);
    // [OPT-4] swap-and-pop O(1) instead of erase+remove O(N)
    unplaced[bestIdx] = unplaced.back();
    unplaced.pop_back();
  }

  // Station-assisted pass for remaining unplaced
  if (!unplaced.empty()) {
    std::vector<int> stillUnplaced;
    for (int cId : unplaced) {
      InsertCandidate c = findBestForCustomer(cId);
      if (c.routeIdx != -1)
        applyCandidate(c);
      else
        stillUnplaced.push_back(cId);
    }
    unplaced = stillUnplaced;
  }

  if (!unplaced.empty())
    return false;

  for (auto &route : newRoutes) {
    route.evaluate();
    if (!route.isFeasible())
      return false;
  }

  while (solution.getNumRoutes() > 0)
    solution.removeRoute(0);
  for (auto &route : newRoutes)
    solution.addRoute(route);

  solution.evaluateRoutes();
  return true;
}

// ============================================================================
// ejectionChain — Strategy 3 Vehicle Reduction
// [OPT-4] swap-and-pop for pool removal; [OPT-8] single getNodeById call
// ============================================================================
bool LocalSearch::ejectionChain(Solution &solution) {
  auto &routes = solution.getRoutes();
  int numRoutes = static_cast<int>(routes.size());
  if (numRoutes < 2)
    return false;

  int    smallestIdx  = -1;
  size_t minCustomers = std::numeric_limits<size_t>::max();
  for (int r = 0; r < numRoutes; ++r) {
    size_t cnt = routes[r].getCustomers().size();
    if (cnt > 0 && cnt < minCustomers) {
      minCustomers = cnt;
      smallestIdx  = r;
    }
  }
  if (smallestIdx == -1 || minCustomers > 20)
    return false;

  const double vehCap = instance->getVehicleCapacity();

  std::vector<int> pool = routes[smallestIdx].getCustomers();

  std::vector<Route> workRoutes;
  workRoutes.reserve(numRoutes - 1);
  for (int r = 0; r < numRoutes; ++r)
    if (r != smallestIdx)
      workRoutes.push_back(routes[r]);

  struct PlaceResult {
    int    r    = -1;
    size_t pos  = 0;
    double cost = 1e18;
  };

  // [OPT-8] Single getNodeById call per customer; use demandById_ for demand
  auto findDirect = [&](int cId, const std::vector<Route> &wr) -> PlaceResult {
    PlaceResult best;
    double dem      = demandById_[cId]; // [OPT-1]
    auto custNode   = instance->getNodeById(cId); // single call
    double twWindow = custNode->getDueDate() - custNode->getReadyTime();
    bool isTightTW  = (twWindow <= 30.0);

    for (int r = 0; r < (int)wr.size(); ++r) {
      if (wr[r].getTotalDemand() + dem > vehCap)
        continue;
      const auto &states = wr[r].getStates();
      for (size_t pos = 1; pos < wr[r].size(); ++pos) {
        if (!wr[r].canPossiblyInsert(cId, pos))
          continue;
        InsertionResult res = wr[r].checkInsertionCost(cId, pos);
        if (!res.isFeasible) continue;
        double cost = res.deltaDistance;
        if (isTightTW && pos <= states.size()) {
          double travelTime    = instance->getTime(wr[r].getNodeAt(pos - 1), cId);
          double arrivalAtCust = states[pos - 1].departureTime + travelTime;
          double waitPenalty   =
              std::max(0.0, custNode->getReadyTime() - arrivalAtCust);
          cost += waitPenalty * 0.3;
        }
        if (cost < best.cost)
          best = {r, pos, cost};
      }
    }
    return best;
  };

  // Station-assisted insertion helper
  auto applyStation = [&](int cId, std::vector<Route> &wr) -> bool {
    double dem = demandById_[cId]; // [OPT-1]
    for (int r = 0; r < (int)wr.size(); ++r) {
      if (wr[r].getTotalDemand() + dem > vehCap)
        continue;
      const auto &nodes = wr[r].getNodes();
      for (size_t pos = 1; pos < nodes.size(); ++pos) {
        int prevId = nodes[pos - 1], nextId = nodes[pos];
        double direct = instance->getDistance(prevId, nextId);
        struct SC { int id; double det; };
        std::vector<SC> sc;
        for (int sid : stationIds)
          sc.push_back({sid, instance->getDistance(prevId, sid) +
                               instance->getDistance(sid, nextId) - direct});
        int topK = std::min(3, (int)sc.size());
        std::partial_sort(sc.begin(), sc.begin() + topK, sc.end(),
                          [](const SC &a, const SC &b) { return a.det < b.det; });
        for (int k = 0; k < topK; ++k) {
          int sid = sc[k].id;
          for (bool before : {true, false}) {
            Route copy = wr[r];
            copy.addNode(cId, pos);
            copy.addNode(sid, before ? pos : pos + 1);
            copy.evaluate();
            if (copy.isFeasible()) {
              wr[r] = copy;
              return true;
            }
          }
        }
      }
    }
    return false;
  };

  std::vector<int> unplaced = pool;

  // ── Stage A: Regret-2 station-aware insertion ────────────────────────────
  // [FIX-3] When direct insertion is infeasible, probe top-3 stations and
  // add their detour cost so regret scores reflect true energy-aware cost.
  bool progress = true;
  while (!unplaced.empty() && progress) {
    progress = false;
    int bestIdx = -1;
    double maxRegret = -1e18;
    PlaceResult bestPlace;

    for (int idx = 0; idx < (int)unplaced.size(); ++idx) {
      int cId   = unplaced[idx];
      double dem = demandById_[cId];
      PlaceResult rank1, rank2;
      rank2.cost = 1e18;

      for (int r = 0; r < (int)workRoutes.size(); ++r) {
        if (workRoutes[r].getTotalDemand() + dem > vehCap)
          continue;
        const auto &wnodes = workRoutes[r].getNodes();

        for (size_t pos = 1; pos < workRoutes[r].size(); ++pos) {
          double effectiveCost = 1e18;

          // --- Direct insertion ---
          if (workRoutes[r].canPossiblyInsert(cId, pos)) {
            InsertionResult res = workRoutes[r].checkInsertionCost(cId, pos);
            if (res.isFeasible)
              effectiveCost = res.deltaDistance;
          }

          // --- Station-assisted fallback (top-3 by detour) ---
          // [SPEED-5] Cheap distance pre-filter before expensive Route copy
          if (effectiveCost >= 1e17) {
            int prevId    = wnodes[pos - 1];
            int nextId    = wnodes[pos];
            double direct = instance->getDistance(prevId, nextId);

            struct SC { int id; double det; };
            // [SPEED-5b] thread_local buffer for station candidates
            static thread_local std::vector<SC> sc;
            sc.clear();
            sc.reserve(stationIds.size());
            for (int sid : stationIds) {
              double det = instance->getDistance(prevId, sid) +
                           instance->getDistance(sid, nextId) - direct;
              // Pre-filter: skip stations with detour > 3x direct dist
              if (det < direct * 3.0 + 30.0)
                sc.push_back({sid, det});
            }

            int topK = std::min(3, (int)sc.size());
            if (topK > 0) {
              std::partial_sort(sc.begin(), sc.begin() + topK, sc.end(),
                                [](const SC &a, const SC &b){ return a.det < b.det; });

              for (int k = 0; k < topK; ++k) {
                int sid = sc[k].id;
                for (bool before : {true, false}) {
                  Route copy = workRoutes[r];
                  copy.addNode(cId, pos);
                  copy.addNode(sid, before ? pos : pos + 1);
                  copy.evaluate();
                  if (copy.isFeasible()) {
                    double dc = copy.getTotalDistance() - workRoutes[r].getTotalDistance();
                    if (dc < effectiveCost)
                      effectiveCost = dc;
                  }
                }
              }
            }
          }

          if (effectiveCost >= 1e17) continue;

          if (effectiveCost < rank1.cost) {
            rank2 = rank1;
            rank1 = {r, pos, effectiveCost};
          } else if (effectiveCost < rank2.cost) {
            rank2 = {r, pos, effectiveCost};
          }
        }
      }
      if (rank1.r == -1) continue;

      double regret = (rank2.r == -1) ? 1e15 : (rank2.cost - rank1.cost);
      if (regret > maxRegret) {
        maxRegret = regret;
        bestIdx   = idx;
        bestPlace = rank1;
      }
    }

    if (bestIdx == -1) break;
    workRoutes[bestPlace.r].addNode(unplaced[bestIdx], bestPlace.pos);
    workRoutes[bestPlace.r].evaluate();
    // [OPT-4] swap-and-pop
    unplaced[bestIdx] = unplaced.back();
    unplaced.pop_back();
    progress = true;
  }

  // ── Stage B: Ejection depth-1 ─────────────────────────────────────────────
  std::unordered_map<int, int> ejectPenalty;

  if (!unplaced.empty()) {
    std::vector<int> stillUnplaced;
    for (int xId : unplaced) {
      double demX = demandById_[xId]; // [OPT-1]
      bool placed = false;

      for (int r = 0; r < (int)workRoutes.size() && !placed; ++r) {
        if (workRoutes[r].getTotalDemand() + demX > vehCap)
          continue;

        int    bestY     = -1, bestYPos = -1;
        double bestYScore = -1e18;
        const auto &rnodes = workRoutes[r].getNodes();
        for (int i = 1; i < (int)rnodes.size() - 1; ++i) {
          int yId = rnodes[i];
          if (nodeTypeById_[yId] != NodeType::CUSTOMER) continue;
          double demY    = demandById_[yId]; // [OPT-1]
          auto custY     = instance->getNodeById(yId);
          int receivable = 0;
          for (int r2 = 0; r2 < (int)workRoutes.size(); ++r2) {
            if (r2 == r) continue;
            if (workRoutes[r2].getTotalDemand() + demY <= vehCap)
              receivable++;
          }
          double twWidth = custY->getDueDate() - custY->getReadyTime();
          double twScore = twWidth / 100.0;
          double scoreY  = receivable + twScore - ejectPenalty[yId] * 1000.0;
          if (scoreY > bestYScore) {
            bestYScore = scoreY;
            bestY      = yId;
            bestYPos   = i;
          }
        }
        if (bestY == -1) continue;

        std::vector<Route> tryCopy = workRoutes;
        tryCopy[r].removeNode(static_cast<size_t>(bestYPos));
        tryCopy[r].evaluate();

        PlaceResult xPlace;
        for (size_t pos = 1; pos < tryCopy[r].size(); ++pos) {
          if (!tryCopy[r].canPossiblyInsert(xId, pos)) continue;
          InsertionResult res = tryCopy[r].checkInsertionCost(xId, pos);
          if (res.isFeasible && res.deltaDistance < xPlace.cost)
            xPlace = {r, pos, res.deltaDistance};
        }
        if (xPlace.r == -1) continue;

        tryCopy[r].addNode(xId, xPlace.pos);
        tryCopy[r].evaluate();
        if (!tryCopy[r].isFeasible()) continue;

        PlaceResult yPlace = findDirect(bestY, tryCopy);
        if (yPlace.r != -1) {
          tryCopy[yPlace.r].addNode(bestY, yPlace.pos);
          tryCopy[yPlace.r].evaluate();
          if (!tryCopy[yPlace.r].isFeasible()) continue;
        } else {
          if (!applyStation(bestY, tryCopy)) continue;
        }

        bool allOk = true;
        for (auto &wr : tryCopy) {
          wr.evaluate();
          if (!wr.isFeasible()) { allOk = false; break; }
        }

        if (!allOk) {
          ejectPenalty[bestY]++;
          continue;
        }

        workRoutes = tryCopy;
        placed     = true;
      }

      if (!placed) stillUnplaced.push_back(xId);
    }
    unplaced = stillUnplaced;
  }

  // ── Stage B2: Ejection depth-2 (capped at top-5 Y/Z) ──────────────────
  if (!unplaced.empty()) {
    std::vector<int> stillUnplaced;
    for (int xId : unplaced) {
      bool placed = false;
      for (int r = 0; r < (int)workRoutes.size() && !placed; ++r) {
        struct YCand { int yId, yPos; double savings; };
        std::vector<YCand> yCands;
        const auto &rnodes = workRoutes[r].getNodes();
        for (int i = 1; i < (int)rnodes.size() - 1; ++i) {
          int yId = rnodes[i];
          if (nodeTypeById_[yId] != NodeType::CUSTOMER) continue;
          int prevId = rnodes[i - 1], nextId = rnodes[i + 1];
          double savings = instance->getDistance(prevId, yId) +
                           instance->getDistance(yId, nextId) -
                           instance->getDistance(prevId, nextId);
          yCands.push_back({yId, i, savings});
        }
        int maxY = std::min(5, (int)yCands.size());
        std::partial_sort(yCands.begin(), yCands.begin() + maxY, yCands.end(),
                          [](const YCand &a, const YCand &b) {
                            return a.savings > b.savings;
                          });

        for (int yi = 0; yi < maxY && !placed; ++yi) {
          int yId  = yCands[yi].yId;
          int yPos = yCands[yi].yPos;

          std::vector<Route> copy1 = workRoutes;
          copy1[r].removeNode(yPos);
          copy1[r].evaluate();

          bool xInserted = false;
          for (size_t pos = 1; pos < copy1[r].size() && !xInserted; ++pos) {
            if (!copy1[r].canPossiblyInsert(xId, pos)) continue;
            InsertionResult res = copy1[r].checkInsertionCost(xId, pos);
            if (!res.isFeasible) continue;
            copy1[r].addNode(xId, pos);
            copy1[r].evaluate();
            if (!copy1[r].isFeasible()) { copy1[r].removeNode(pos); continue; }
            xInserted = true;
          }
          if (!xInserted) continue;

          for (int r2 = 0; r2 < (int)copy1.size() && !placed; ++r2) {
            if (r2 == r) continue;
            PlaceResult yp = findDirect(yId, copy1);
            if (yp.r != -1) {
              copy1[yp.r].addNode(yId, yp.pos);
              copy1[yp.r].evaluate();
              if (copy1[yp.r].isFeasible()) {
                bool allOk = true;
                for (auto &wr : copy1) if (!wr.isFeasible()) { allOk = false; break; }
                if (allOk) { workRoutes = copy1; placed = true; }
              }
            }
            if (!placed) {
              const auto &r2nodes = copy1[r2].getNodes();
              struct ZCand { int zId, zPos; double savings; };
              std::vector<ZCand> zCands;
              for (int j = 1; j < (int)r2nodes.size() - 1; ++j) {
                int zId = r2nodes[j];
                if (nodeTypeById_[zId] != NodeType::CUSTOMER) continue;
                int pz = r2nodes[j-1], nz = r2nodes[j+1];
                double s = instance->getDistance(pz, zId) +
                           instance->getDistance(zId, nz) -
                           instance->getDistance(pz, nz);
                zCands.push_back({zId, j, s});
              }
              int maxZ = std::min(5, (int)zCands.size());
              std::partial_sort(zCands.begin(), zCands.begin() + maxZ,
                                zCands.end(), [](const ZCand &a, const ZCand &b) {
                                  return a.savings > b.savings; });

              for (int zi = 0; zi < maxZ && !placed; ++zi) {
                int zId  = zCands[zi].zId;
                int zPos = zCands[zi].zPos;
                std::vector<Route> copy2 = copy1;
                copy2[r2].removeNode(zPos);
                copy2[r2].evaluate();
                for (size_t pos2 = 1; pos2 < copy2[r2].size(); ++pos2) {
                  if (!copy2[r2].canPossiblyInsert(yId, pos2)) continue;
                  InsertionResult ry = copy2[r2].checkInsertionCost(yId, pos2);
                  if (!ry.isFeasible) continue;
                  copy2[r2].addNode(yId, pos2);
                  copy2[r2].evaluate();
                  if (!copy2[r2].isFeasible()) continue;
                  PlaceResult zp = findDirect(zId, copy2);
                  if (zp.r == -1) applyStation(zId, copy2);
                  else {
                    copy2[zp.r].addNode(zId, zp.pos);
                    copy2[zp.r].evaluate();
                  }
                  bool allOk = true;
                  for (auto &wr : copy2) {
                    wr.evaluate();
                    if (!wr.isFeasible()) { allOk = false; break; }
                  }
                  if (allOk) { workRoutes = copy2; placed = true; break; }
                }
              }
            }
          }
        }
      }
      if (!placed) stillUnplaced.push_back(xId);
    }
    unplaced = stillUnplaced;
  }

  // ── Stage C: Station-assisted for remaining unplaced ────────────────────
  if (!unplaced.empty()) {
    std::vector<int> finalUnplaced;
    for (int cId : unplaced) {
      if (!applyStation(cId, workRoutes))
        finalUnplaced.push_back(cId);
    }
    unplaced = finalUnplaced;
  }

  if (!unplaced.empty())
    return false;

  for (auto &wr : workRoutes) {
    wr.evaluate();
    if (!wr.isFeasible()) return false;
  }

  while (solution.getNumRoutes() > 0)
    solution.removeRoute(0);
  for (auto &wr : workRoutes)
    if (!wr.getCustomers().empty())
      solution.addRoute(wr);
  solution.evaluateRoutes();
  return true;
}

// ============================================================================
// getTopKInsertionPositions
// [OPT-6] thread_local candidate buffer to avoid per-call heap allocation
// ============================================================================
std::vector<size_t> LocalSearch::getTopKInsertionPositions(const Route &route,
                                                           int nodeId,
                                                           int topK) const {
  struct Candidate {
    size_t position;
    double cost;
    bool operator<(const Candidate &other) const { return cost < other.cost; }
  };

  // [OPT-6] Reuse allocation across calls
  static thread_local std::vector<Candidate> candidates;
  candidates.clear();

  const auto &nodes      = route.getNodes();
  auto        targetNode = instance->getNodeById(nodeId);
  const auto &states     = route.getStates();

  for (size_t pos = 1; pos < nodes.size(); ++pos) {
    int prevNodeId = nodes[pos - 1];

    double distBefore = instance->getDistance(prevNodeId, nodes[pos]);
    double distAfter  = instance->getDistance(prevNodeId, nodeId) +
                        instance->getDistance(nodeId, nodes[pos]);
    double detour = distAfter - distBefore;

    double timePenalty = 0.0;
    if (pos > states.size()) continue;

    double arrivalTime =
        states[pos - 1].departureTime + instance->getTime(prevNodeId, nodeId);

    if (arrivalTime > targetNode->getDueDate()) {
      timePenalty = 10000.0;
    } else if (arrivalTime < targetNode->getReadyTime()) {
      timePenalty = (targetNode->getReadyTime() - arrivalTime) * 0.5;
    }

    candidates.push_back({pos, detour + timePenalty});
  }

  int k = std::min((size_t)topK, candidates.size());
  std::partial_sort(candidates.begin(), candidates.begin() + k, candidates.end());

  std::vector<size_t> result;
  result.reserve(k);
  for (int i = 0; i < k; ++i)
    result.push_back(candidates[i].position);

  return result;
}

// [OPT-7] Use std::hypot instead of sqrt(pow+pow)
double LocalSearch::calculateEuclideanDistance(const RouteCentroid &c1,
                                               const RouteCentroid &c2) const {
  return std::hypot(c1.x - c2.x, c1.y - c2.y);
}

// ============================================================================
// Electricity-Free Vehicle Reduction
// ============================================================================

bool LocalSearch::runSmartMultiRouteMerge(Solution &solution) {
  auto &routes = solution.getRoutes();
  int numRoutes = routes.size();
  if (numRoutes < 2)
    return false;

  double bestMergeScore = -1e9;
  int r1_idx = -1, r2_idx = -1;

  for (int i = 0; i < numRoutes; ++i) {
    for (int j = i + 1; j < numRoutes; ++j) {
      int sizeSum =
          routes[i].getCustomers().size() + routes[j].getCustomers().size();
      if (sizeSum > 30 || sizeSum == 0) continue;

      double dist = calculateEuclideanDistance(computeCentroid(routes[i]),
                                               computeCentroid(routes[j]));
      double score = (1000.0 - sizeSum) - dist;
      if (score > bestMergeScore) {
        bestMergeScore = score;
        r1_idx         = i;
        r2_idx         = j;
      }
    }
  }

  if (r1_idx == -1) return false;

  std::vector<int> customer_pool;
  for (int c : routes[r1_idx].getCustomers()) customer_pool.push_back(c);
  for (int c : routes[r2_idx].getCustomers()) customer_pool.push_back(c);

  double oldTotalDist =
      routes[r1_idx].getTotalDistance() + routes[r2_idx].getTotalDistance();

  std::vector<Route> newRoutes;
  auto vehicleType = routes[r1_idx].getVehicle();
  newRoutes.push_back(Route(0, vehicleType, instance));
  newRoutes.push_back(Route(0, vehicleType, instance));

  // Furthest-apart seeding
  int seed1 = -1, seed2 = -1;
  double maxDist = -1.0;
  for (size_t i = 0; i < customer_pool.size(); ++i) {
    for (size_t j = i + 1; j < customer_pool.size(); ++j) {
      double d = instance->getDistance(customer_pool[i], customer_pool[j]);
      if (d > maxDist) {
        maxDist = d;
        seed1   = customer_pool[i];
        seed2   = customer_pool[j];
      }
    }
  }

  if (seed1 != -1 && seed2 != -1) {
    newRoutes[0].addNode(seed1, 1);
    newRoutes[1].addNode(seed2, 1);
    // [OPT-4] swap-and-pop for both seeds
    auto removeSeed = [&](int seedId) {
      auto it = std::find(customer_pool.begin(), customer_pool.end(), seedId);
      if (it != customer_pool.end()) {
        *it = customer_pool.back();
        customer_pool.pop_back();
      }
    };
    removeSeed(seed1);
    removeSeed(seed2);
  } else if (!customer_pool.empty()) {
    newRoutes[0].addNode(customer_pool[0], 1);
    customer_pool[0] = customer_pool.back();
    customer_pool.pop_back();
  }

  struct MergeCandidate {
    int    custId   = -1;
    int    routeIdx = -1;
    size_t pos      = 0;
    double cost     = 1e18;
    int    stationId = -1;
    bool   statBefore = true;
  };

  bool construction_failed = false;

  while (!customer_pool.empty() && !construction_failed) {
    MergeCandidate globalBest;

    for (int cust_id : customer_pool) {
      double demand  = demandById_[cust_id]; // [OPT-1]
      int nearStat   = findNearestStation(cust_id);

      for (int r_idx = 0; r_idx < 2; ++r_idx) {
        if (newRoutes[r_idx].getTotalDemand() + demand >
            newRoutes[r_idx].getVehicle()->getCapacity())
          continue;

        for (size_t pos = 1; pos < newRoutes[r_idx].size(); ++pos) {
          // Option A: direct
          InsertionResult res = newRoutes[r_idx].checkInsertionCost(cust_id, pos);
          if (res.isFeasible && res.deltaDistance < globalBest.cost) {
            globalBest = {cust_id, r_idx, pos, res.deltaDistance, -1, true};
          }

          // Options B & C: station-assisted
          if (nearStat != -1) {
            {
              Route copy = newRoutes[r_idx];
              copy.addNode(cust_id, pos);
              copy.addNode(nearStat, pos);
              copy.evaluate();
              if (copy.isFeasible()) {
                double c = copy.getTotalDistance() - newRoutes[r_idx].getTotalDistance();
                if (c < globalBest.cost)
                  globalBest = {cust_id, r_idx, pos, c, nearStat, true};
              }
            }
            {
              Route copy2 = newRoutes[r_idx];
              copy2.addNode(cust_id, pos);
              copy2.addNode(nearStat, pos + 1);
              copy2.evaluate();
              if (copy2.isFeasible()) {
                double c = copy2.getTotalDistance() - newRoutes[r_idx].getTotalDistance();
                if (c < globalBest.cost)
                  globalBest = {cust_id, r_idx, pos, c, nearStat, false};
              }
            }
          }
        }
      }
    }

    if (globalBest.routeIdx == -1) {
      construction_failed = true;
      break;
    }

    if (globalBest.stationId == -1) {
      newRoutes[globalBest.routeIdx].addNode(globalBest.custId, globalBest.pos);
    } else if (globalBest.statBefore) {
      newRoutes[globalBest.routeIdx].addNode(globalBest.custId, globalBest.pos);
      newRoutes[globalBest.routeIdx].addNode(globalBest.stationId, globalBest.pos);
    } else {
      newRoutes[globalBest.routeIdx].addNode(globalBest.custId, globalBest.pos);
      newRoutes[globalBest.routeIdx].addNode(globalBest.stationId, globalBest.pos + 1);
    }
    newRoutes[globalBest.routeIdx].evaluate();

    // [OPT-4] swap-and-pop
    auto it = std::find(customer_pool.begin(), customer_pool.end(), globalBest.custId);
    if (it != customer_pool.end()) {
      *it = customer_pool.back();
      customer_pool.pop_back();
    }
  }

  if (construction_failed) return false;

  double newTotalDist = 0;
  std::vector<Route> finalRoutes;

  for (auto &r : newRoutes) {
    r.evaluate();
    if (!r.isFeasible()) return false;
    if (!r.getCustomers().empty()) {
      finalRoutes.push_back(r);
      newTotalDist += r.getTotalDistance();
    }
  }

  if (finalRoutes.empty()) return false;

  if (finalRoutes.size() < 2 ||
      (finalRoutes.size() == 2 && newTotalDist < oldTotalDist * 0.997)) {
    solution.removeRoute(std::max(r1_idx, r2_idx));
    solution.removeRoute(std::min(r1_idx, r2_idx));
    for (const auto &r : finalRoutes)
      solution.addRoute(r);
    return true;
  }

  return false;
}

LocalSearch::RouteCentroid
LocalSearch::computeCentroid(const Route &route) const {
  double x = route.getCentroidX();
  double y = route.getCentroidY();
  if (x == 0.0 && y == 0.0) {
    auto depot = instance->getNodeById(0);
    return {depot->getX(), depot->getY()};
  }
  return {x, y};
}

std::vector<LocalSearch::RouteCentroid>
LocalSearch::computeAllCentroids(const Solution &solution) {
  std::vector<RouteCentroid> centroids;
  centroids.reserve(solution.getRoutes().size());
  for (const auto &route : solution.getRoutes()) {
    centroids.push_back(computeCentroid(route));
  }
  return centroids;
}

bool LocalSearch::areRoutesClose(const RouteCentroid &c1,
                                 const RouteCentroid &c2,
                                 double threshold) const {
  double dx = c1.x - c2.x;
  double dy = c1.y - c2.y;
  return (dx * dx + dy * dy) < (threshold * threshold);
}

void LocalSearch::updateSearchContext(const Solution &solution) {
  if (searchContext_.isValid) return;

  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  searchContext_.centroids = computeAllCentroids(solution);

  searchContext_.neighborLists.clear();
  searchContext_.neighborLists.resize(numRoutes);

  // [SPEED-4] Cap neighbor list: rank by distance, keep top-8 closest routes.
  // Prevents O(R²) neighbor explosion when R is large (e.g. 20+ routes).
  static constexpr int MAX_ROUTE_NEIGHBORS = 8;

  for (int r1 = 0; r1 < numRoutes; ++r1) {
    // Always include self
    searchContext_.neighborLists[r1].push_back(r1);

    // Rank other routes by centroid distance
    struct RN { int idx; double dist2; };
    std::vector<RN> ranked;
    ranked.reserve(numRoutes - 1);
    for (int r2 = 0; r2 < numRoutes; ++r2) {
      if (r2 == r1) continue;
      double dx = searchContext_.centroids[r1].x - searchContext_.centroids[r2].x;
      double dy = searchContext_.centroids[r1].y - searchContext_.centroids[r2].y;
      ranked.push_back({r2, dx*dx + dy*dy});
    }
    int keep = std::min(MAX_ROUTE_NEIGHBORS, (int)ranked.size());
    std::partial_sort(ranked.begin(), ranked.begin() + keep, ranked.end(),
                      [](const RN &a, const RN &b){ return a.dist2 < b.dist2; });
    for (int k = 0; k < keep; ++k)
      searchContext_.neighborLists[r1].push_back(ranked[k].idx);
  }

  searchContext_.removalRankings.assign(numRoutes, {});
  searchContext_.rankingDirty.assign(numRoutes, true);

  searchContext_.isValid = true;
}

const std::vector<std::pair<int, double>> &
LocalSearch::getCachedRanking(int routeIdx, const Route &route,
                              SearchContext &ctx) {
  if (ctx.rankingDirty[routeIdx]) {
    ctx.removalRankings[routeIdx] = rankNodesByRemovalSavings(route);
    ctx.rankingDirty[routeIdx]    = false;
  }
  return ctx.removalRankings[routeIdx];
}

std::vector<std::pair<int, double>>
LocalSearch::rankNodesByRemovalSavings(const Route &route) {
  std::vector<std::pair<int, double>> rankings;
  const auto &nodes = route.getNodes();
  if (nodes.size() <= 2) return rankings;

  rankings.reserve(nodes.size() - 2);
  for (size_t i = 1; i < nodes.size() - 1; ++i) {
    int prevNodeId = nodes[i - 1];
    int currNodeId = nodes[i];
    int nextNodeId = nodes[i + 1];

    double oldDist = instance->getDistance(prevNodeId, currNodeId) +
                     instance->getDistance(currNodeId, nextNodeId);
    double newDist = instance->getDistance(prevNodeId, nextNodeId);
    double savings = oldDist - newDist;
    rankings.push_back({(int)i, savings});
  }

  std::sort(rankings.begin(), rankings.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });

  return rankings;
}

std::vector<size_t> LocalSearch::findBestInsertionPositions(const Route &route,
                                                            int nodeId,
                                                            int topK) const {
  return findBestInsertionPositions_TimeAware(route, nodeId, topK);
}

// ============================================================================
// findBestInsertionPositions_TimeAware
// [OPT-6] thread_local candidate buffer — zero heap allocation per call
// ============================================================================
std::vector<size_t>
LocalSearch::findBestInsertionPositions_TimeAware(const Route &route,
                                                  int nodeId, int topK) const {
  struct Candidate {
    size_t position;
    double cost;
    bool operator<(const Candidate &o) const { return cost < o.cost; }
  };

  // [OPT-6] Reuse buffer across calls
  static thread_local std::vector<Candidate> candidates;
  candidates.clear();

  const auto &nodes  = route.getNodes();
  const auto &states = route.getStates();

  auto customerNode =
      std::static_pointer_cast<Customer>(instance->getNodeById(nodeId));

  for (size_t pos = 1; pos < nodes.size(); ++pos) {
    int prev = nodes[pos - 1];

    double distBefore = instance->getDistance(prev, nodes[pos]);
    double distAfter  = instance->getDistance(prev, nodeId) +
                        instance->getDistance(nodeId, nodes[pos]);
    double detour = distAfter - distBefore;

    double arrivalTime =
        states[pos - 1].departureTime + instance->getTime(prev, nodeId);

    double twPenalty = 0.0;
    if (arrivalTime > customerNode->getDueDate()) {
      twPenalty = 1000.0;
    } else if (arrivalTime < customerNode->getReadyTime()) {
      twPenalty = 0.5;
    } else {
      double slack = customerNode->getDueDate() - arrivalTime;
      if (slack < 10.0) {
        twPenalty = (10.0 - slack) / 10.0;
      }
    }

    candidates.push_back({pos, detour * (1.0 + twPenalty)});
  }

  int k = std::min(topK, (int)candidates.size());
  std::partial_sort(candidates.begin(), candidates.begin() + k, candidates.end());

  std::vector<size_t> result;
  result.reserve(k);
  for (int i = 0; i < k; ++i)
    result.push_back(candidates[i].position);

  return result;
}

void LocalSearch::preprocessKNN() {
  auto customers = instance->getCustomers();

  for (const auto &customer : customers) {
    std::vector<std::pair<double, int>> distances;
    distances.reserve(customers.size() - 1);

    for (const auto &other : customers) {
      if (customer->getId() != other->getId()) {
        double dist = instance->getDistance(customer->getId(), other->getId());
        distances.push_back({dist, other->getId()});
      }
    }

    int k = std::min(K_NEIGHBORS, (int)distances.size());
    std::partial_sort(distances.begin(), distances.begin() + k, distances.end());

    std::vector<int> nearestK;
    nearestK.reserve(k);
    for (int i = 0; i < k; ++i)
      nearestK.push_back(distances[i].second);

    knnCache_[customer->getId()] = nearestK;
  }
}

void LocalSearch::preprocessGranularity() {
  auto customers = instance->getCustomers();
  int n = customers.size();

  if (n < 2) {
    avgDistance_      = 0.0;
    distanceThreshold_ = std::numeric_limits<double>::max();
    return;
  }

  double totalDistance = 0.0;
  int pairCount = 0;

  for (size_t i = 0; i < customers.size(); ++i) {
    for (size_t j = i + 1; j < customers.size(); ++j) {
      totalDistance +=
          instance->getDistance(customers[i]->getId(), customers[j]->getId());
      pairCount++;
    }
  }

  avgDistance_       = totalDistance / pairCount;
  distanceThreshold_ = GRANULARITY_FACTOR * avgDistance_;
}

// ============================================================================
// findBestInsertionPositions_KNN
// [OPT-5] unordered_set<int> for O(1) neighbor lookup + vector<bool> for
//         O(1) position dedup. Was O(n*K) with std::set + inner linear scan.
// ============================================================================
std::vector<size_t>
LocalSearch::findBestInsertionPositions_KNN(const Route &route, int nodeId,
                                            int topK) const {
  auto it = knnCache_.find(nodeId);
  if (it == knnCache_.end()) {
    return findBestInsertionPositions_TimeAware(route, nodeId, topK);
  }

  const auto &nearestNeighbors = it->second;
  const auto &nodes            = route.getNodes();
  const int   routeSize        = (int)nodes.size();

  // [SPEED-2] thread_local set — zero heap alloc per call in hot loop
  static thread_local std::unordered_set<int> neighborSet;
  neighborSet.clear();
  neighborSet.insert(nearestNeighbors.begin(), nearestNeighbors.end());

  // [SPEED-3] thread_local isCandidate — zero heap alloc, just assign + clear
  static thread_local std::vector<bool> isCandidate;
  isCandidate.assign(routeSize + 1, false);
  for (int pos = 0; pos < routeSize; ++pos) {
    if (neighborSet.count(nodes[pos])) {
      if (pos > 0)           isCandidate[pos]     = true;
      if (pos + 1 < routeSize) isCandidate[pos + 1] = true;
    }
  }

  // Collect valid candidate positions
  struct Candidate {
    size_t position;
    double cost;
    bool operator<(const Candidate &o) const { return cost < o.cost; }
  };

  // [OPT-6] thread_local buffer
  static thread_local std::vector<Candidate> candidates;
  candidates.clear();

  const auto &states = route.getStates();

  // [SPEED-1] Cache TW values once — avoids hashtable lookup per position
  auto customerNode  =
      std::static_pointer_cast<Customer>(instance->getNodeById(nodeId));
  const double twReady = customerNode->getReadyTime();
  const double twDue   = customerNode->getDueDate();

  bool foundAny = false;
  for (int pos = 1; pos < routeSize; ++pos) {
    if (!isCandidate[pos]) continue;
    foundAny = true;

    int prev = nodes[pos - 1];
    double distBefore = instance->getDistance(prev, nodes[pos]);
    double distAfter  = instance->getDistance(prev, nodeId) +
                        instance->getDistance(nodeId, nodes[pos]);
    double detour = distAfter - distBefore;

    double arrivalTime =
        states[pos - 1].departureTime + instance->getTime(prev, nodeId);
    double twPenalty = 0.0;
    if      (arrivalTime > twDue)   twPenalty = 1000.0;
    else if (arrivalTime < twReady) twPenalty = 0.5;

    candidates.push_back({(size_t)pos, detour * (1.0 + twPenalty)});
  }

  if (!foundAny) {
    return findBestInsertionPositions_TimeAware(route, nodeId, topK);
  }

  if (candidates.empty()) return {};

  int k = std::min(topK, (int)candidates.size());
  std::partial_sort(candidates.begin(), candidates.begin() + k, candidates.end());

  std::vector<size_t> result;
  result.reserve(k);
  for (int i = 0; i < k; ++i)
    result.push_back(candidates[i].position);

  return result;
}

MoveEvaluation LocalSearch::evaluateRelocateDelta(const Solution &solution,
                                                  const MoveDescriptor &move) {
  MoveEvaluation result;
  result.isFeasible = false;

  const auto &routes    = solution.getRoutes();
  const auto &r1        = routes[move.routeIdx1];
  const bool isIntraMove = (move.routeIdx1 == move.routeIdx2);
  const auto &r2        = isIntraMove ? r1 : routes[move.routeIdx2];

  int nodeId = r1.getNodeAt(move.nodeIdx1);

  if (!r2.canPossiblyInsert(nodeId, move.nodeIdx2)) {
    return result;
  }

  double distanceDelta = 0.0;

  if (move.cachedRemovalSavings != 0.0) {
    distanceDelta -= move.cachedRemovalSavings;
  } else {
    int r1_prev = r1.getNodeAt(move.nodeIdx1 - 1);
    int r1_next = r1.getNodeAt(move.nodeIdx1 + 1);
    distanceDelta -= (instance->getDistance(r1_prev, nodeId) +
                      instance->getDistance(nodeId, r1_next));
    distanceDelta += instance->getDistance(r1_prev, r1_next);
  }

  if (isIntraMove) {
    int insertPos = move.nodeIdx2;
    if (move.nodeIdx1 < move.nodeIdx2) insertPos--;
    const auto &r1_nodes = r1.getNodes();
    int r2_prev = r1_nodes[insertPos - 1];
    int r2_next = r1_nodes[insertPos];
    distanceDelta -= instance->getDistance(r2_prev, r2_next);
    distanceDelta += (instance->getDistance(r2_prev, nodeId) +
                      instance->getDistance(nodeId, r2_next));
  } else {
    int r2_prev = r2.getNodeAt(move.nodeIdx2 - 1);
    int r2_next = r2.getNodeAt(move.nodeIdx2);
    distanceDelta -= instance->getDistance(r2_prev, r2_next);
    distanceDelta += (instance->getDistance(r2_prev, nodeId) +
                      instance->getDistance(nodeId, r2_next));
  }

  result.isFeasible    = true;
  result.distanceDelta  = distanceDelta;
  result.objectiveDelta = distanceDelta;

  return result;
}

int LocalSearch::findNearestStation(int nodeId) const {
  if (stationIds.empty()) return -1;

  int    bestStationId = -1;
  double minDistance   = std::numeric_limits<double>::max();

  for (int stationId : stationIds) {
    double distance = instance->getDistance(nodeId, stationId);
    if (distance < minDistance) {
      minDistance   = distance;
      bestStationId = stationId;
    }
  }
  return bestStationId;
}

// ============================================================================
// ENERGY BOOST HELPERS
// ============================================================================

int LocalSearch::findPrecedingStation(const Route &route,
                                      size_t insertPos) const {
  const auto &nodes = route.getNodes();
  for (int i = static_cast<int>(insertPos) - 1; i >= 1; --i) {
    if (nodeTypeById_[nodes[i]] == NodeType::STATION) {
      return i;
    }
  }
  return -1;
}

double LocalSearch::calculateEnergyGap(const Route &route,
                                       size_t position) const {
  const auto &minReq  = route.getMinBatteryReq();
  const auto &states  = route.getStates();

  if (position >= minReq.size() || position >= states.size()) {
    return 0.0;
  }

  double gap = minReq[position] - states[position].remainingBattery;
  return std::max(0.0, gap);
}

bool LocalSearch::tryRelocateWithEnergyBoost(Route &routeFrom, int fromNodeIdx,
                                             Route &routeTo, size_t insertPos,
                                             double maxExtraTimeAllowed) {
  Route backupFrom = routeFrom;
  Route backupTo   = routeTo;

  int nodeId = routeFrom.getNodeAt(fromNodeIdx);
  if (nodeId < 0) return false;

  routeFrom.removeNode(fromNodeIdx);
  routeTo.addNode(nodeId, insertPos);

  routeTo.evaluate();
  routeFrom.evaluate();

  if (routeTo.isFeasible() && routeFrom.isFeasible()) {
    return true;
  }

  if (!routeTo.isFeasible()) {
    // [FIX-1] Multi-station relocation: try top-K stations ranked by detour cost
    // instead of only the single nearest station.
    routeTo = backupTo;

    // Build candidate list: rank all stations by detour through insertion edge
    struct StationCandidate { int id; double detour; };
    std::vector<StationCandidate> candidates;
    candidates.reserve(stationIds.size());

    const auto &toNodes = backupTo.getNodes();
    int prevNode = (insertPos > 0 && insertPos <= toNodes.size())
                       ? toNodes[insertPos - 1] : -1;
    int nextNode = (insertPos < toNodes.size())
                       ? toNodes[insertPos] : -1;
    double directDist = (prevNode >= 0 && nextNode >= 0)
                            ? instance->getDistance(prevNode, nextNode) : 0.0;

    for (int sid : stationIds) {
      // Skip station already adjacent to insertPos
      if (sid == prevNode || sid == nextNode) continue;

      double detour = (prevNode >= 0 ? instance->getDistance(prevNode, sid) : 0.0) +
                      (nextNode >= 0 ? instance->getDistance(sid, nextNode) : 0.0) -
                      directDist;
      candidates.push_back({sid, detour});
    }

    constexpr int TOP_K = 5;
    int topK = std::min(TOP_K, (int)candidates.size());
    std::partial_sort(candidates.begin(), candidates.begin() + topK,
                      candidates.end(),
                      [](const StationCandidate &a, const StationCandidate &b) {
                        return a.detour < b.detour;
                      });

    // Try each candidate station in two orderings: [S, C] and [C, S]
    Route bestRoute = backupTo;  // initialize with valid Route (no default ctor)
    double bestTimeDiff = maxExtraTimeAllowed + 1.0;
    bool found = false;

    for (int k = 0; k < topK; ++k) {
      int sid = candidates[k].id;

      // Ordering A: depot → ... → S → customer → ...
      {
        Route testRoute = backupTo;
        testRoute.addNode(sid, insertPos);
        testRoute.addNode(nodeId, insertPos + 1);
        testRoute.evaluate();
        if (testRoute.isFeasible()) {
          double timeDiff = testRoute.getTotalTime() - backupTo.getTotalTime();
          if (timeDiff < bestTimeDiff) {
            bestTimeDiff = timeDiff;
            bestRoute    = testRoute;
            found        = true;
          }
        }
      }

      // Ordering B: depot → ... → customer → S → ...
      {
        Route testRoute = backupTo;
        testRoute.addNode(nodeId, insertPos);
        testRoute.addNode(sid, insertPos + 1);
        testRoute.evaluate();
        if (testRoute.isFeasible()) {
          double timeDiff = testRoute.getTotalTime() - backupTo.getTotalTime();
          if (timeDiff < bestTimeDiff) {
            bestTimeDiff = timeDiff;
            bestRoute    = testRoute;
            found        = true;
          }
        }
      }
    }

    if (found) {
      routeTo = bestRoute;
      routeFrom.evaluate();
      return routeFrom.isFeasible();
    }
  }

  routeFrom = backupFrom;
  routeTo   = backupTo;
  return false;
}

int LocalSearch::removeRedundantStations(Route &route) {
  const double EPSILON = 1e-9;
  int removalCount = 0;

  bool stationRemoved = true;
  while (stationRemoved) {
    stationRemoved = false;
    route.evaluate();

    if (!route.isFeasible()) break;

    const auto &nodes         = route.getNodes();
    const auto &states        = route.getStates();
    const auto &minBatteryReq = route.getMinBatteryReq();

    for (int i = static_cast<int>(nodes.size()) - 2; i > 0; --i) {
      int nodeId = nodes[i];
      // [OPT-2]
      if (nodeTypeById_[nodeId] != NodeType::STATION) continue;

      int prevNodeId = nodes[i - 1];
      int nextNodeId = nodes[i + 1];

      double distToSkip  = instance->getDistance(prevNodeId, nextNodeId);
      double energyToSkip = distToSkip * route.getVehicle()->getEnergyConsumptionRate();

      double batteryAtPrev = states[i - 1].remainingBattery;

      if (batteryAtPrev < energyToSkip - EPSILON) continue;

      double batteryAfterSkip = batteryAtPrev - energyToSkip;
      double minBatteryAtNext = minBatteryReq[i + 1];

      if (batteryAfterSkip >= minBatteryAtNext - EPSILON) {
        route.removeNode(i);
        removalCount++;
        stationRemoved = true;
        break;
      }
    }
  }

  return removalCount;
}

bool LocalSearch::removeRedundantStations(Solution &solution) {
  bool anyImproved = false;
  for (auto &route : solution.getRoutes()) {
    if (route.size() <= 2) continue;
    int removed = removeRedundantStations(route);
    if (removed > 0) {
      anyImproved = true;
    }
  }
  return anyImproved;
}

void LocalSearch::preprocessTWNext() {
  int maxId = 0;
  for (const auto &node : instance->getNodes()) {
    if (node->getId() > maxId) maxId = node->getId();
  }

  int numNodes = maxId + 1;
  twNext_.assign(numNodes, std::vector<bool>(numNodes, false));

  for (const auto &nodeU : instance->getNodes()) {
    int u = nodeU->getId();
    for (const auto &nodeV : instance->getNodes()) {
      int v = nodeV->getId();
      if (u == v) {
        twNext_[u][v] = true;
        continue;
      }

      double timeUtoV = instance->getTime(u, v);
      double earliestArrival =
          nodeU->getReadyTime() + nodeU->getServiceTime() + timeUtoV;

      if (earliestArrival <= nodeV->getDueDate() + 1e-9) {
        twNext_[u][v] = true;
      }
    }
  }
}