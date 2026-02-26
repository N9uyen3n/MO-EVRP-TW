#include "../../include/alns/LocalSearch.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Station.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>

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
}

// ============================================================================
// Algorithm: 3-Phase Hybrid Local Search
// ============================================================================
// Input:  Solution S (feasible)
// Output: Improved Solution S'
//
// Phase 1 — Distance Optimization (Variable Neighborhood Descent)
//   Cycle through {Relocate, Swap, Or-Opt, 2-Opt} with restart-on-improve
// Phase 2 — Charging Optimization (every CHARGING_FREQUENCY iterations)
//   Remove redundant stations, reposition, swap, optimize amounts
// Phase 3 — Vehicle Reduction (every VEHICLE_REDUCTION_FREQUENCY iterations)
//   Multi-route merge, smallest route elimination
// ============================================================================
void LocalSearch::run(Solution &solution) {
  for (int iter = 0; iter < MAX_LS_ITERATIONS; ++iter) {
    bool improved = false;

    // Update granular neighborhood data (centroids + neighbor lists)
    updateSearchContext(solution);

    // --- Phase 1: Distance Optimization (VND) ---
    if (runDistanceOptimization(solution)) {
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

    // --- Phase 3: Vehicle Reduction (periodic or when stagnated) ---
    if ((iter % VEHICLE_REDUCTION_FREQUENCY == 0 || !improved) &&
        solution.getRoutes().size() > 1) {
      if (runVehicleReduction(solution)) {
        improved = true;
        noImprovementCount_ = 0;
        searchContext_.invalidate();
      }
    }

    // --- Adaptive Neighborhood Sizing ---
    if (improved) {
      solution.evaluateRoutes();
      noImprovementCount_ = 0;
      // Intensification: shrink neighborhood on success
      maxNodesToCheck_ = std::max(MIN_NODES_TO_CHECK, maxNodesToCheck_ - 1);
      maxSwapAttempts_ = std::max(MIN_SWAP_ATTEMPTS, maxSwapAttempts_ - 1);
    } else {
      noImprovementCount_++;
      // Diversification: expand neighborhood on failure
      maxNodesToCheck_ = std::min(MAX_NODES_TO_CHECK, maxNodesToCheck_ + 1);
      maxSwapAttempts_ = std::min(MAX_SWAP_ATTEMPTS, maxSwapAttempts_ + 1);

      // Early termination
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

  // --- TỐI ƯU MỚI: FAST CHECK TRƯỚC KHI COPY (CHO INTER-RELOCATE) ---
  if (move.type == MoveType::INTER_RELOCATE) {
    int nodeId = routes[move.routeIdx1].getNodeAt(move.nodeIdx1);
    const auto &r2 = routes[move.routeIdx2];

    // Tier 2 Check: Use the existing exact check before copying
    InsertionResult result = r2.checkInsertionCost(nodeId, move.nodeIdx2);

    if (!result.isFeasible) {
      return; // Bỏ qua ngay, không cần copy route, không tốn bộ nhớ
    }

    // Nếu feasible, ta có thể tính delta một cách tương đối
    const auto &r1 = routes[move.routeIdx1];
    int prev_node = r1.getNodeAt(move.nodeIdx1 - 1);
    int next_node = r1.getNodeAt(move.nodeIdx1 + 1);
    double distance_removed = instance->getDistance(prev_node, nodeId) +
                              instance->getDistance(nodeId, next_node) -
                              instance->getDistance(prev_node, next_node);

    move.eval.isFeasible = true;
    move.eval.distanceDelta = result.deltaDistance - distance_removed;
    move.eval.objectiveDelta = move.eval.distanceDelta * weights.dist;
    // NOTE: This is an approximation of the objective change.
    // For a "First Improvement" strategy, this is often sufficient.
    // If "Best Improvement" were needed, we would still need the full copy.
    return;
  }
  // --- KẾT THÚC TỐI ƯU ---

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

      // Quan trọng: Xóa node từ cuối lên đầu để không làm thay đổi chỉ số
      for (int i = move.segmentLength - 1; i >= 0; --i) {
        r1_copy.removeNode(move.nodeIdx1 + i);
      }

      if (twoRoutes) {
        for (int i = 0; i < move.segmentLength; ++i) {
          r2_copy.addNode(segment[i], move.nodeIdx2 + i);
        }
      } else { // Cùng một route
        int targetIdx = move.nodeIdx2;
        // Điều chỉnh chỉ số nếu di chuyển về phía sau trong cùng một route
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
        return; // Sanity check
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
  default:
    // Should not happen
    break;
  }
}

// ============================================================================
// Phase 1: Distance Optimization — Variable Neighborhood Descent (VND)
// ============================================================================
// Operators are applied in order: Relocate → Swap → Or-Opt → 2-Opt
// If any operator improves the solution, restart from Relocate.
// Stop when no operator can find improvement (local optimum reached).
// ============================================================================
bool LocalSearch::runDistanceOptimization(Solution &solution) {
  LocalSearchWeights weights;
  weights.dist = 1.0;
  MoveDescriptor bestMove;
  bool anyImproved = false;

  // VND: cycle through neighborhoods, restart on improvement
  int k = 0; // neighborhood index: 0=Relocate, 1=Swap, 2=Or-Opt, 3=2-Opt
  while (k < 4) {
    bool improved = false;

    switch (k) {
    case 0: // N1: Relocate (Intra + Inter)
      if (searchRelocate(solution, bestMove, weights, searchContext_)) {
        improved = true;
      }
      break;
    case 1: // N2: Swap (Intra + Inter)
      if (searchSwap(solution, bestMove, weights, searchContext_)) {
        improved = true;
      }
      break;
    case 2: // N3: Or-Opt (segment relocation)
      if (searchOrOpt(solution, bestMove, weights)) {
        improved = true;
      }
      break;
    case 3: // N4: 2-Opt (intra-route edge reversal)
      if (searchTwoOpt(solution, bestMove, weights)) {
        improved = true; // searchTwoOpt now applies move internally (First
                         // Improvement)
      }
      break;
    }

    if (improved) {
      anyImproved = true;
      k = 0; // Restart from first neighborhood (VND rule)
    } else {
      k++; // Move to next neighborhood
    }
  }

  return anyImproved;
}

bool LocalSearch::searchRelocate(Solution &solution,
                                 MoveDescriptor &outBestMove,
                                 const LocalSearchWeights &weights,
                                 const SearchContext &ctx) {
  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  // ⭐ SAFETY: Validate context before use
  if (!ctx.isValid || ctx.centroids.size() != numRoutes ||
      ctx.neighborLists.size() != numRoutes) {
    // Fallback: context is invalid, skip this search
    return false;
  }

  // ⭐ OPTIMIZATION: Use cached centroids and neighbor lists from context
  const auto &centroids = ctx.centroids;
  const auto &neighborLists = ctx.neighborLists;

  // ⭐ SMD: Use member variable instead of local
  for (int r1 = 0; r1 < numRoutes; ++r1) {
    if (routes[r1].size() <= 2)
      continue;

    auto rankedNodes = rankNodesByRemovalSavings(routes[r1]);

    int nodesToCheck = std::min(maxNodesToCheck_, (int)rankedNodes.size());

    for (int k = 0; k < nodesToCheck; ++k) {
      int i = rankedNodes[k].first;
      int nodeId = routes[r1].getNodeAt(i);
      if (instance->getNodeById(nodeId)->getType() != NodeType::CUSTOMER)
        continue;

      for (int r2 : neighborLists[r1]) {

        // ⭐ Use KNN to find candidate positions
        auto candidatePositions =
            findBestInsertionPositions_KNN(routes[r2], nodeId, 3);

        for (size_t j : candidatePositions) {
          if (r1 == r2 && (j == i || j == i + 1))
            continue;

          // --- TỐI ƯU MỚI: PRE-DELTA CHECK ---
          // Create a temporary move descriptor to pass to the delta evaluation
          MoveDescriptor tempMove;
          tempMove.type =
              (r1 == r2) ? MoveType::INTRA_RELOCATE : MoveType::INTER_RELOCATE;
          tempMove.routeIdx1 = r1;
          tempMove.nodeIdx1 = i;
          tempMove.routeIdx2 = r2;
          tempMove.nodeIdx2 = j;

          // Use the lightweight delta evaluation as a filter.
          // This function now includes the canPossiblyInsert check.
          MoveEvaluation preEval = evaluateRelocateDelta(solution, tempMove);

          // If the move is not possibly feasible or it increases distance, skip
          // it. A non-negative delta means no improvement or worsening, so we
          // skip.
          if (!preEval.isFeasible || preEval.distanceDelta >= -1e-9) {
            continue;
          }

          // If the pre-check passes, then we perform the full, expensive
          // evaluation. For INTER_RELOCATE, evaluateMove now has its own fast
          // path, but this delta check is still valuable for INTRA_RELOCATE and
          // as a distance filter.
          activeMove = tempMove; // Copy the valid move data to the active move
          evaluateMove(solution, activeMove, weights);

          if (activeMove.eval.isFeasible &&
              activeMove.eval.objectiveDelta < -1e-9) {
            // ⭐ First Improvement: Apply immediately
            applyMove(solution, activeMove);
            return true;
          } else if (!activeMove.eval.isFeasible &&
                     preEval.distanceDelta < -5.0) {
            // 🔋 ENERGY BOOST: Move was good for distance but failed
            // feasibility Try station-assisted insertion
            auto &routes = solution.getRoutes();
            Route testRouteFrom = routes[r1];
            Route testRouteTo = routes[r2];

            if (tryRelocateWithEnergyBoost(testRouteFrom, i, testRouteTo, j,
                                           30.0)) {
              // Success! Apply the boosted routes
              routes[r1] = testRouteFrom;
              routes[r2] = testRouteTo;
              solution.evaluateRoutes();
              searchContext_.invalidate();
              return true;
            }
          }
        }
      }
    }
  }
  return false; // No improvement found
}

bool LocalSearch::searchTwoOpt(Solution &solution, MoveDescriptor &outBestMove,
                               const LocalSearchWeights &weights) {
  auto &routes = solution.getRoutes();

  // 2-Opt chỉ áp dụng cho INTRA-ROUTE (đảo ngược segment trong cùng route)
  for (int r = 0; r < (int)routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();
    int n = nodes.size();

    // Cần ít nhất 4 nodes: depot_start - node1 - node2 - depot_end
    if (n < 4)
      continue;

    // Thử tất cả các cặp (i, j) với i < j
    for (int i = 1; i < n - 2; ++i) {
      for (int j = i + 1; j < n - 1; ++j) {
        // Quick check: Chỉ evaluate nếu có potential savings
        int prev = nodes[i - 1];
        int nodeI = nodes[i];
        int nodeJ = nodes[j];
        int next = nodes[j + 1];

        double oldDist = instance->getDistance(prev, nodeI) +
                         instance->getDistance(nodeJ, next);
        double newDist = instance->getDistance(prev, nodeJ) +
                         instance->getDistance(nodeI, next);

        // Quick reject nếu không có savings
        if (newDist >= oldDist - 1e-6)
          continue;

        // Tạo move descriptor
        activeMove.reset();
        activeMove.type = MoveType::INTRA_TWO_OPT;
        activeMove.routeIdx1 = r;
        activeMove.nodeIdx1 = i;
        activeMove.routeIdx2 = r;
        activeMove.nodeIdx2 = j;

        // Full evaluation
        evaluateMove(solution, activeMove, weights);

        if (activeMove.eval.isFeasible &&
            activeMove.eval.objectiveDelta < -1e-9) {
          // ⭐ First Improvement: Apply immediately and return
          applyMove(solution, activeMove);
          return true;
        }
      }
    }
  }

  return false;
}
bool LocalSearch::searchSwap(Solution &solution, MoveDescriptor &outBestMove,
                             const LocalSearchWeights &weights,
                             const SearchContext &ctx) {
  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  // ⭐ SAFETY: Validate context before use
  if (!ctx.isValid || ctx.centroids.size() != numRoutes) {
    // Fallback: context is invalid, skip this search
    return false;
  }

  // ⭐ OPTIMIZATION: Use cached centroids from context
  const auto &centroids = ctx.centroids;

  // ⭐ SMD: Use member variable instead of local
  // INTRA-ROUTE SWAP
  for (int r = 0; r < numRoutes; ++r) {
    const auto &nodes = routes[r].getNodes();
    int n = nodes.size();
    if (n < 4)
      continue;

    for (int i = 1; i < n - 2; ++i) {
      if (instance->getNodeById(nodes[i])->getType() != NodeType::CUSTOMER)
        continue;

      for (int j = i + 2; j < n - 1; ++j) {
        if (instance->getNodeById(nodes[j])->getType() != NodeType::CUSTOMER)
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

        // Tier 3: Check if swap is feasible (capacity, energy, time window)
        // We need to check if we can insert node[j] at position i (removing
        // node[i]) and insert node[i] at position j (removing node[j])
        if (!routes[r].canPossiblyInsert(nodes[j], i, nodes[i]) ||
            !routes[r].canPossiblyInsert(nodes[i], j, nodes[j])) {
          continue;
        }

        activeMove.reset(); // ⭐ SMD: Reuse member
        activeMove.type = MoveType::INTRA_SWAP;
        activeMove.routeIdx1 = r;
        activeMove.nodeIdx1 = i;
        activeMove.routeIdx2 = r;
        activeMove.nodeIdx2 = j;

        evaluateMove(solution, activeMove, weights);

        if (activeMove.eval.isFeasible &&
            activeMove.eval.objectiveDelta < -1e-9) {
          // ⭐ OPTIMIZATION: Early exit for INTRA-SWAP (First Improvement)
          applyMove(solution, activeMove);
          return true;
        }
      }
    }
  }

  // INTER-ROUTE SWAP
  for (int r1 = 0; r1 < numRoutes; ++r1) {
    for (int r2 = r1 + 1; r2 < numRoutes; ++r2) {
      // ⭐ GRANULAR FILTER (Route-level): Only consider close routes
      if (!areRoutesClose(centroids[r1], centroids[r2], distanceThreshold_))
        continue;

      const auto &nodes1 = routes[r1].getNodes();
      const auto &nodes2 = routes[r2].getNodes();

      auto ranked1 = rankNodesByRemovalSavings(routes[r1]);
      auto ranked2 = rankNodesByRemovalSavings(routes[r2]);

      int attempts1 = std::min(maxSwapAttempts_, (int)ranked1.size());
      int attempts2 = std::min(maxSwapAttempts_, (int)ranked2.size());

      for (int k1 = 0; k1 < attempts1; ++k1) {
        int i = ranked1[k1].first;
        if (instance->getNodeById(nodes1[i])->getType() != NodeType::CUSTOMER)
          continue;

        for (int k2 = 0; k2 < attempts2; ++k2) {
          int j = ranked2[k2].first;
          if (instance->getNodeById(nodes2[j])->getType() != NodeType::CUSTOMER)
            continue;

          // ⭐ GRANULAR FILTER (Node-level): Only swap if customers are close
          int nodeId1 = nodes1[i];
          int nodeId2 = nodes2[j];
          double distance = instance->getDistance(nodeId1, nodeId2);
          if (distance >= distanceThreshold_) {
            continue; // Skip distant customer pairs
          }

          if (!routes[r1].canPossiblyInsert(nodeId2, i, nodeId1) ||
              !routes[r2].canPossiblyInsert(nodeId1, j, nodeId2)) {
            continue;
          }

          activeMove.reset(); // ⭐ SMD: Reuse member
          activeMove.type = MoveType::INTER_SWAP;
          activeMove.routeIdx1 = r1;
          activeMove.nodeIdx1 = i;
          activeMove.routeIdx2 = r2;
          activeMove.nodeIdx2 = j;

          evaluateMove(solution, activeMove, weights);

          if (activeMove.eval.isFeasible &&
              activeMove.eval.objectiveDelta < -1e-9) {
            // ⭐ First Improvement: Apply immediately
            applyMove(solution, activeMove);
            return true;
          }
        }
      }
    }
  }

  return false; // No improvement found
}

bool LocalSearch::searchOrOpt(Solution &solution, MoveDescriptor &outBestMove,
                              const LocalSearchWeights &weights) {
  const int SEGMENT_LENGTH = 2;
  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  // ⭐ SAFETY: Validate context for granular filtering
  if (!searchContext_.isValid ||
      (int)searchContext_.neighborLists.size() != numRoutes) {
    return false;
  }
  const auto &neighborLists = searchContext_.neighborLists;

  for (int r1 = 0; r1 < numRoutes; ++r1) {
    // Một đoạn dài 2 cần ít nhất 2 khách hàng + 2 depot = 4 nodes
    if (routes[r1].size() < 2 + SEGMENT_LENGTH)
      continue;

    // Lặp qua tất cả các vị trí bắt đầu của đoạn
    for (int i = 1; i <= (int)routes[r1].size() - 1 - SEGMENT_LENGTH; ++i) {

      // --- Lọc: Kiểm tra đoạn có phải toàn khách hàng không ---
      bool segmentIsCustomers = true;
      for (int k = 0; k < SEGMENT_LENGTH; ++k) {
        if (instance->getNodeById(routes[r1].getNodeAt(i + k))->getType() !=
            NodeType::CUSTOMER) {
          segmentIsCustomers = false;
          break;
        }
      }
      if (!segmentIsCustomers)
        continue;

      int firstNodeId = routes[r1].getNodeAt(i);

      // ⭐ OPTIMIZATION: Use granular neighbor lists instead of all routes
      for (int r2 : neighborLists[r1]) {
        // Use KNN to find candidate positions based on the first node of the
        // segment
        auto candidatePositions =
            findBestInsertionPositions_KNN(routes[r2], firstNodeId, 3);

        for (int j : candidatePositions) {

          // --- Lọc: Không chèn lại vào vị trí cũ ---
          if (r1 == r2 && (j >= i && j <= i + SEGMENT_LENGTH)) {
            continue;
          }

          activeMove.reset();
          activeMove.type =
              (r1 == r2) ? MoveType::INTER_OR_OPT
                         : MoveType::INTER_OR_OPT; // Or-Opt uses INTER_OR_OPT
                                                   // type for both
          activeMove.routeIdx1 = r1;
          activeMove.nodeIdx1 = i;
          activeMove.routeIdx2 = r2;
          activeMove.nodeIdx2 = j;
          activeMove.segmentLength = SEGMENT_LENGTH;

          evaluateMove(solution, activeMove, weights);

          if (activeMove.eval.isFeasible &&
              activeMove.eval.objectiveDelta < -1e-9) {
            // First Improvement: Áp dụng ngay
            applyMove(solution, activeMove);
            return true;
          }
        }
      }
    }
  }
  return false; // Không tìm thấy cải thiện
}
// ============================================================================
// Phase 2: Charging Optimization
// ============================================================================
// Strategy 1: Remove redundant stations (backward pass feasibility check)
// Strategy 2: Reposition stations (swap with nearest alternative)
// Strategy 3: Optimize charging amounts (charge only minimum needed)
// Strategy 4: Station swap (centroid-based replacement)
// ============================================================================
bool LocalSearch::runChargingOptimization(Solution &solution) {
  bool improved = false;

  // Strategy 1: Remove redundant charging stations
  if (removeRedundantStations(solution)) {
    improved = true;
  }

  // Strategy 2: Reposition stations to closer alternatives
  if (repositionStations(solution)) {
    improved = true;
  }

  // Strategy 3: Optimize charging amounts (partial charging)
  if (optimizeChargingAmounts(solution)) {
    improved = true;
  }

  // Strategy 4: Replace suboptimal stations with centroid-optimal ones
  if (searchStationSwap(solution)) {
    improved = true;
  }

  return improved;
}

bool LocalSearch::searchStationRemoval(Solution &solution,
                                       MoveDescriptor &outBestMove,
                                       const LocalSearchWeights &weights) {
  outBestMove.reset();
  outBestMove.eval.objectiveDelta = -1e-5;
  bool found = false;

  auto &routes = solution.getRoutes();

  // Duyệt tất cả các routes để tìm stations có thể loại bỏ
  for (int r = 0; r < routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();

    // Tìm tất cả stations trong route
    std::vector<int> stationPositions;
    for (int i = 1; i < nodes.size() - 1; ++i) {
      if (instance->getNodeById(nodes[i])->getType() == NodeType::STATION) {
        stationPositions.push_back(i);
      }
    }

    // Thử loại bỏ từng station
    for (int stationPos : stationPositions) {
      // Tạo route copy và xóa station
      Route routeCopy = routes[r];
      routeCopy.removeNode(stationPos);
      routeCopy.evaluate();

      // Check feasibility
      if (!routeCopy.isFeasible())
        continue;

      // Tính delta
      double oldDist = routes[r].getTotalDistance();
      double newDist = routeCopy.getTotalDistance();
      double delta = newDist - oldDist;

      // Station removal nên làm giảm distance (vì bỏ detour)
      if (delta < outBestMove.eval.objectiveDelta) {
        outBestMove.type = MoveType::STATION_REMOVE;
        outBestMove.routeIdx1 = r;
        outBestMove.nodeIdx1 = stationPos;
        outBestMove.routeIdx2 = -1;
        outBestMove.nodeIdx2 = -1;
        outBestMove.eval.isFeasible = true;
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

  for (int r = 0; r < routes.size(); ++r) {
    // Lấy nodes ở đây để đảm bảo luôn làm việc với phiên bản mới nhất
    const auto &nodes = routes[r].getNodes();

    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      int currentStationId = nodes[i];
      if (instance->getNodeById(currentStationId)->getType() !=
          NodeType::STATION)
        continue;

      int prevNodeId = nodes[i - 1];
      int nextNodeId = nodes[i + 1];
      double currentDetour =
          instance->getDistance(prevNodeId, currentStationId) +
          instance->getDistance(currentStationId, nextNodeId);

      int bestStationId = -1;
      double bestDetour = currentDetour;

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
            bestDetour = altDetour;
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
          return true; // <-- SỬA LỖI: First Improvement, return ngay
        }
      }
    }
  }

  return false; // Không tìm thấy cải thiện nào
}

// ⭐ NEW: Station Swap Local Search
// Purpose: Replace suboptimal stations with centroid-optimal alternatives
// This addresses the "Nearest Station Trap" where routes get locked to wrong
// stations
bool LocalSearch::searchStationSwap(Solution &solution) {
  auto &routes = solution.getRoutes();
  bool improved = false;
  const double EPSILON = 1e-9;

  for (auto &route : routes) {
    route.evaluate();
    if (!route.isFeasible())
      continue;

    const auto &nodes = route.getNodes();
    if (nodes.size() <= 3)
      continue; // Need at least depot + 1 customer + depot

    // 1. Calculate route centroid (center of mass of customers)
    double centroidX = 0.0, centroidY = 0.0;
    int customerCount = 0;

    for (int nodeId : nodes) {
      auto node = instance->getNodeById(nodeId);
      if (node->getType() == NodeType::CUSTOMER) {
        centroidX += node->getX();
        centroidY += node->getY();
        customerCount++;
      }
    }

    if (customerCount == 0)
      continue;
    centroidX /= customerCount;
    centroidY /= customerCount;

    // 2. Find existing stations in this route and check if replaceable
    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      int nodeId = nodes[i];
      auto node = instance->getNodeById(nodeId);

      if (node->getType() != NodeType::STATION)
        continue;

      auto currentStation = std::static_pointer_cast<Station>(node);

      // Calculate current station's distance to centroid
      double dx = currentStation->getX() - centroidX;
      double dy = currentStation->getY() - centroidY;
      double currentCentroidDist = std::sqrt(dx * dx + dy * dy);

      // 3. Try replacing with each other station
      int bestReplacement = -1;
      double bestImprovement = 0.0;

      for (int candidateId : stationIds) {
        if (candidateId == nodeId)
          continue; // Skip same station

        auto candidateStation = std::static_pointer_cast<Station>(
            instance->getNodeById(candidateId));

        // Quick filter: Only consider stations closer to centroid
        double cdx = candidateStation->getX() - centroidX;
        double cdy = candidateStation->getY() - centroidY;
        double candidateCentroidDist = std::sqrt(cdx * cdx + cdy * cdy);

        if (candidateCentroidDist >= currentCentroidDist - EPSILON)
          continue;

        // Simulate replacement: Remove current station, add candidate
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

      // 4. Apply best replacement if found
      if (bestReplacement != -1 && bestImprovement > EPSILON) {
        route.removeNode(i);
        route.addNode(bestReplacement, i);
        route.evaluate();
        improved = true;
        break; // Re-scan route from beginning after modification
      }
    }
  }

  return improved;
}

bool LocalSearch::optimizeChargingAmounts(Solution &solution) {
  // Strategy: Detect and remove redundant charging stations
  // Since Route::evaluate() already optimizes charging amounts automatically,
  // this function focuses on removing unnecessary stations

  auto &routes = solution.getRoutes();
  bool improved = false;

  for (int r = 0; r < routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();
    const auto &states = routes[r].getStates();

    // Find all stations in route with their properties
    std::vector<std::pair<size_t, double>> stationInfos; // pos, chargeAmount
    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      if (instance->getNodeById(nodes[i])->getType() == NodeType::STATION) {
        double chargeAmount =
            (i < states.size()) ? states[i].chargeAmount : 0.0;
        stationInfos.push_back({i, chargeAmount});
      }
    }

    // NEW Strategy 1: Remove stations right after depot (position 1) or at
    // depot location
    if (!stationInfos.empty()) {
      for (auto it = stationInfos.begin(); it != stationInfos.end();) {
        size_t pos = it->first;
        int stationId = nodes[pos];

        // Check if station is at position 1 (right after depot)
        bool isAtDepotPosition = (pos == 1);

        // Check if station has same coordinates as depot
        auto stationNode = instance->getNodeById(stationId);
        auto depotNode = instance->getNodeById(0);
        bool sameAsDepot =
            (std::abs(stationNode->getX() - depotNode->getX()) < 0.01 &&
             std::abs(stationNode->getY() - depotNode->getY()) < 0.01);

        if (isAtDepotPosition || sameAsDepot) {
          Route testRoute = routes[r];
          testRoute.removeNode(pos);
          testRoute.evaluate();

          if (testRoute.isFeasible()) {
            routes[r] = testRoute;
            improved = true;
            break; // Re-evaluate from start
          }
        }
        ++it;
      }
    }

    if (improved)
      continue;

    // NEW Strategy 2: Remove stations with zero or very low charge amount
    const auto &currentStates = routes[r].getStates();
    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      if (instance->getNodeById(nodes[i])->getType() == NodeType::STATION) {
        double chargeAmount =
            (i < currentStates.size()) ? currentStates[i].chargeAmount : 0.0;

        if (chargeAmount < 1.0) { // Station barely charges anything
          Route testRoute = routes[r];
          testRoute.removeNode(i);
          testRoute.evaluate();

          if (testRoute.isFeasible() &&
              testRoute.getTotalDistance() < routes[r].getTotalDistance()) {
            routes[r] = testRoute;
            improved = true;
            break;
          }
        }
      }
    }

    if (improved)
      continue;

    // Strategy 3: Remove consecutive stations (keep only one)
    std::vector<size_t> stationPositions;
    for (size_t i = 1; i < nodes.size() - 1; ++i) {
      if (instance->getNodeById(nodes[i])->getType() == NodeType::STATION) {
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
            improved = true;
            break;
          }

          testRoute = routes[r];
          testRoute.removeNode(pos2);
          testRoute.evaluate();

          if (testRoute.isFeasible()) {
            routes[r] = testRoute;
            improved = true;
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
          improved = true;
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

  // Strategy 1: Try smart merger
  if (runSmartMultiRouteMerge(solution)) {
    return true;
  }

  // Strategy 2: Try eliminating smallest route by distributing its customers
  if (tryEliminateSmallestRoute(solution)) {
    return true;
  }

  return false;
}

// NEW: Aggressive route elimination - tries to remove the smallest route
bool LocalSearch::tryEliminateSmallestRoute(Solution &solution) {
  auto &routes = solution.getRoutes();
  int numRoutes = routes.size();
  if (numRoutes < 2)
    return false;

  // Find the smallest route (by customer count)
  int smallestIdx = -1;
  size_t minCustomers = std::numeric_limits<size_t>::max();

  for (int r = 0; r < numRoutes; ++r) {
    size_t custCount = routes[r].getCustomers().size();
    if (custCount > 0 && custCount < minCustomers) {
      minCustomers = custCount;
      smallestIdx = r;
    }
  }

  if (smallestIdx == -1 || minCustomers > 15)
    return false; 
                  

  // std::cout << "[EliminateRoute] Trying to eliminate Route " <<
  // routes[smallestIdx].getId()
  //           << " with " << minCustomers << " customers..." << std::endl;

  // Get all customers from the smallest route
  std::vector<int> customersToMove = routes[smallestIdx].getCustomers();

  // Create a working copy of the solution without the smallest route
  std::vector<Route> newRoutes;
  for (int r = 0; r < numRoutes; ++r) {
    if (r != smallestIdx) {
      newRoutes.push_back(routes[r]);
    }
  }

  // Try to insert all customers into remaining routes
  std::vector<int> failedCustomers;

  for (int custId : customersToMove) {
    bool inserted = false;
    double bestCost = std::numeric_limits<double>::max();
    int bestRouteIdx = -1;
    size_t bestPos = 0;

    // Find best insertion across all remaining routes
    for (int r = 0; r < (int)newRoutes.size(); ++r) {
      // Quick capacity check
      auto custNode = instance->getNodeById(custId);
      double demand =
          std::dynamic_pointer_cast<Customer>(custNode)->getDemand();
      if (newRoutes[r].getTotalDemand() + demand >
          newRoutes[r].getVehicle()->getCapacity()) {
        continue;
      }

      // Try each insertion position
      for (size_t pos = 1; pos < newRoutes[r].size(); ++pos) {
        InsertionResult res = newRoutes[r].checkInsertionCost(custId, pos);
        if (res.isFeasible && res.deltaDistance < bestCost) {
          bestCost = res.deltaDistance;
          bestRouteIdx = r;
          bestPos = pos;
          inserted = true;
        }
      }
    }

    if (inserted && bestRouteIdx >= 0) {
      newRoutes[bestRouteIdx].addNode(custId, bestPos);
      newRoutes[bestRouteIdx].evaluate();
    } else {
      failedCustomers.push_back(custId);
    }
  }

  // If all customers were successfully placed, apply the change
  if (failedCustomers.empty()) {
    // Clear current routes and add new ones
    while (solution.getNumRoutes() > 0) {
      solution.removeRoute(0);
    }
    for (auto &route : newRoutes) {
      route.evaluate();
      if (route.isFeasible()) {
        solution.addRoute(route);
      } else {
        // std::cout << "[EliminateRoute] FAILED - Route became infeasible after
        // reconstruction." << std::endl;
        return false;
      }
    }

    solution.evaluateRoutes();
    // std::cout << "[EliminateRoute] SUCCESS! Reduced to " <<
    // solution.getNumRoutes() << " vehicles." << std::endl;
    return true;
  }

  // FALLBACK: Try station-assisted insertion for failed customers
  if (!failedCustomers.empty() && failedCustomers.size() <= 3) {
    // std::cout << "[EliminateRoute] Attempting station-assisted insertion for
    // "
    //           << failedCustomers.size() << " remaining customers..." <<
    //           std::endl;

    for (auto it = failedCustomers.begin(); it != failedCustomers.end();) {
      int custId = *it;
      bool inserted = false;

      for (int r = 0; r < (int)newRoutes.size() && !inserted; ++r) {
        // Find nearest station to customer
        int stationId = findNearestStation(custId);
        if (stationId == -1)
          continue;

        // Try inserting station first, then customer
        for (size_t pos = 1; pos < newRoutes[r].size() && !inserted; ++pos) {
          Route testRoute = newRoutes[r];
          testRoute.addNode(stationId, pos);
          testRoute.addNode(custId, pos + 1);
          testRoute.evaluate();

          if (testRoute.isFeasible()) {
            newRoutes[r] = testRoute;
            inserted = true;
          }
        }
      }

      if (inserted) {
        it = failedCustomers.erase(it);
      } else {
        ++it;
      }
    }

    // Check again if all succeeded
    if (failedCustomers.empty()) {
      while (solution.getNumRoutes() > 0) {
        solution.removeRoute(0);
      }
      for (auto &route : newRoutes) {
        route.evaluate();
        if (route.isFeasible()) {
          solution.addRoute(route);
        } else {
          return false;
        }
      }
      solution.evaluateRoutes();
      // std::cout << "[EliminateRoute] SUCCESS with station assist! Reduced to
      // "
      //           << solution.getNumRoutes() << " vehicles." << std::endl;
      return true;
    }
  }

  // std::cout << "[EliminateRoute] Failed to place " << failedCustomers.size()
  // << " customers." << std::endl;
  return false;
}

std::vector<size_t> LocalSearch::getTopKInsertionPositions(const Route &route,
                                                           int nodeId,
                                                           int topK) const {
  struct Candidate {
    size_t position;
    double cost; // Heuristic cost (dist + time penalty)
    bool operator<(const Candidate &other) const { return cost < other.cost; }
  };
  std::vector<Candidate> candidates;

  const auto &nodes = route.getNodes();

  auto targetNode = instance->getNodeById(nodeId);

  // Duyệt qua mọi vị trí chèn hợp lệ (trừ vị trí 0 - depot)
  for (size_t pos = 1; pos < nodes.size(); ++pos) {
    int prevNodeId = nodes[pos - 1];

    // 1. Khoảng cách (Distance Detour)
    double distBefore = instance->getDistance(prevNodeId, nodes[pos]);
    double distAfter = instance->getDistance(prevNodeId, nodeId) +
                       instance->getDistance(nodeId, nodes[pos]);
    double detour = distAfter - distBefore;

    // 2. Phạt thời gian (Time Penalty)
    double timePenalty = 0.0;
    // Tính giờ đến dự kiến (giả sử route đang ở trạng thái hợp lệ)
    route.evaluate(); // Make sure states are up to date
    const auto &states = route.getStates();
    if (pos > states.size())
      continue; // Should not happen

    double arrivalTime =
        states[pos - 1].departureTime + instance->getTime(prevNodeId, nodeId);

    if (arrivalTime > targetNode->getDueDate()) {
      timePenalty = 10000.0; // Trễ cửa sổ -> Rất xấu
    } else if (arrivalTime < targetNode->getReadyTime()) {
      timePenalty = (targetNode->getReadyTime() - arrivalTime) *
                    0.5; // Phải chờ -> Phạt nhẹ
    }

    double totalCost = detour + timePenalty;
    candidates.push_back({pos, totalCost});
  }

  // Sắp xếp và lấy Top-K
  std::partial_sort(candidates.begin(),
                    candidates.begin() +
                        std::min((size_t)topK, candidates.size()),
                    candidates.end());

  std::vector<size_t> result;
  for (int i = 0; i < std::min((size_t)topK, candidates.size()); ++i) {
    result.push_back(candidates[i].position);
  }

  return result;
}

double LocalSearch::calculateEuclideanDistance(const RouteCentroid &c1,
                                               const RouteCentroid &c2) const {
  return std::sqrt(std::pow(c1.x - c2.x, 2) + std::pow(c1.y - c2.y, 2));
}

bool LocalSearch::runSmartMultiRouteMerge(Solution &solution) {
  auto &routes = solution.getRoutes();
  int numRoutes = routes.size();
  if (numRoutes < 2)
    return false;

  // --- BƯỚC 1: SELECTION ---
  double bestMergeScore = -1e9;
  int r1_idx = -1, r2_idx = -1;

  for (int i = 0; i < numRoutes; ++i) {
    for (int j = i + 1; j < numRoutes; ++j) {
      int sizeSum =
          routes[i].getCustomers().size() + routes[j].getCustomers().size();
      if (sizeSum > 35 || sizeSum == 0)
        continue; // Increased from 12 to allow larger merges

      double dist = calculateEuclideanDistance(computeCentroid(routes[i]),
                                               computeCentroid(routes[j]));

      // Score ưu tiên size nhỏ, dist nhỏ
      double score = (1000.0 - sizeSum) - dist;

      if (score > bestMergeScore) {
        bestMergeScore = score;
        r1_idx = i;
        r2_idx = j;
      }
    }
  }

  if (r1_idx == -1)
    return false;

  // std::cout << "[SmartMerge] Selected R" << routes[r1_idx].getId() << " & R"
  // << routes[r2_idx].getId() << " for merge." << std::endl;

  // --- BƯỚC 2: POOLING ---
  std::vector<int> customer_pool;
  for (int c : routes[r1_idx].getCustomers())
    customer_pool.push_back(c);
  for (int c : routes[r2_idx].getCustomers())
    customer_pool.push_back(c);

  // Lưu lại tổng quãng đường ban đầu để so sánh
  double oldTotalDist =
      routes[r1_idx].getTotalDistance() + routes[r2_idx].getTotalDistance();

  // --- BƯỚC 3: RECONSTRUCTION ---
  std::vector<Route> newRoutes;
  auto vehicleType = routes[r1_idx].getVehicle();
  newRoutes.push_back(Route(0, vehicleType, instance));
  newRoutes.push_back(Route(0, vehicleType, instance));

  // 3.1. Seed Selection
  std::sort(customer_pool.begin(), customer_pool.end(), [&](int a, int b) {
    return instance->getNodeById(a)->getReadyTime() <
           instance->getNodeById(b)->getReadyTime();
  });

  if (customer_pool.size() < 2) {
    if (customer_pool.empty())
      return false;
    newRoutes[0].addNode(customer_pool[0], 1);
    customer_pool.erase(customer_pool.begin());
  } else {
    newRoutes[0].addNode(customer_pool[0], 1);
    newRoutes[1].addNode(customer_pool[1], 1);
    customer_pool.erase(customer_pool.begin(), customer_pool.begin() + 2);
  }

  // 3.2. Assignment Loop
  bool construction_failed = false;
  int fail_count = 0;
  int total_stations_added = 0;      // BIẾN ĐẾM MỚI
  const int MAX_STATIONS_TO_ADD = 5; // Giới hạn tổng số trạm sạc có thể thêm

  while (!customer_pool.empty() && !construction_failed) {
    int best_cust_id = -1;
    int best_route_idx = -1;
    size_t best_pos = -1;
    double best_cost = 1e9;

    for (int cust_id : customer_pool) {
      for (int r_idx = 0; r_idx < 2; ++r_idx) {
        std::vector<size_t> candidatePos =
            getTopKInsertionPositions(newRoutes[r_idx], cust_id, 3);

        for (size_t pos : candidatePos) {
          InsertionResult res =
              newRoutes[r_idx].checkInsertionCost(cust_id, pos);

          if (res.isFeasible && res.deltaDistance < best_cost) {
            best_cost = res.deltaDistance;
            best_cust_id = cust_id;
            best_route_idx = r_idx;
            best_pos = pos;
          }
        }
      }
    }

    if (best_cust_id != -1) {
      newRoutes[best_route_idx].addNode(best_cust_id, best_pos);
      customer_pool.erase(
          std::remove(customer_pool.begin(), customer_pool.end(), best_cust_id),
          customer_pool.end());
      fail_count = 0;
    } else {
      // --- BẢO VỆ MỚI: KIỂM TRA TỔNG SỐ TRẠM SẠC ---
      if (total_stations_added >= MAX_STATIONS_TO_ADD) {
        construction_failed = true;
        continue; // Dừng lại nếu đã thêm quá nhiều trạm mà không hiệu quả
      }

      // --- STATION ASSISTED ---
      bool inserted_station = false;
      for (int r_idx = 0; r_idx < 2; ++r_idx) {
        if (newRoutes[r_idx].getCustomers().empty())
          continue;

        int lastNode = newRoutes[r_idx].getLastNodeId();
        int stationId = findNearestStation(lastNode);

        if (stationId != -1) {
          size_t insertPos = newRoutes[r_idx].size() - 1;
          InsertionResult res =
              newRoutes[r_idx].checkInsertionCost(stationId, insertPos);

          if (res.isFeasible) {
            newRoutes[r_idx].addNode(stationId, insertPos);
            inserted_station = true;
            total_stations_added++; // Tăng biến đếm mới
            break;
          }
        }
      }

      if (!inserted_station) {
        fail_count++;
        if (fail_count >= 5)
          construction_failed = true;
      }
    }
  }

  // --- BƯỚC 4: FINALIZE & APPLY ---
  if (construction_failed)
    return false;

  double newTotalDist = 0;
  std::vector<Route> finalRoutes;

  for (auto &r : newRoutes) {
    r.evaluate();
    if (!r.isFeasible())
      return false;
    if (!r.getCustomers().empty()) {
      finalRoutes.push_back(r);
      newTotalDist += r.getTotalDistance();
    }
  }

  if (finalRoutes.size() < 2 ||
      (finalRoutes.size() == 2 && newTotalDist < oldTotalDist * 0.99)) {
    solution.removeRoute(std::max(r1_idx, r2_idx));
    solution.removeRoute(std::min(r1_idx, r2_idx));
    for (const auto &r : finalRoutes) {
      solution.addRoute(r);
    }

    // if (finalRoutes.size() < 2) {
    //     std::cout << "[SmartMerge] REDUCTION SUCCESS (Fleet reduced)!\n";
    // } else {
    //     std::cout << "[SmartMerge] OPTIMIZATION SUCCESS (Distance improved: "
    //     << (oldTotalDist - newTotalDist) << ")\n";
    // }
    return true;
  }

  return false;
}

LocalSearch::RouteCentroid
LocalSearch::computeCentroid(const Route &route) const {
  double sumX = 0, sumY = 0;
  int count = 0;
  for (int nodeId : route.getNodes()) {
    auto node = instance->getNodeById(nodeId);
    if (node->getType() == NodeType::CUSTOMER) {
      sumX += node->getX();
      sumY += node->getY();
      count++;
    }
  }
  if (count == 0) {
    auto depot = instance->getNodeById(0);
    return {depot->getX(), depot->getY()};
  }
  return {sumX / count, sumY / count};
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
  double dist = std::sqrt(std::pow(c1.x - c2.x, 2) + std::pow(c1.y - c2.y, 2));
  return dist < threshold;
}

void LocalSearch::updateSearchContext(const Solution &solution) {
  // Only recompute if context is invalid
  if (searchContext_.isValid) {
    return;
  }

  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  // Compute centroids
  searchContext_.centroids = computeAllCentroids(solution);

  // Compute neighbor lists based on centroids
  searchContext_.neighborLists.clear();
  searchContext_.neighborLists.resize(numRoutes);

  for (int r1 = 0; r1 < numRoutes; ++r1) {
    for (int r2 = 0; r2 < numRoutes; ++r2) {
      // Include self and close routes
      if (r1 == r2 ||
          areRoutesClose(searchContext_.centroids[r1],
                         searchContext_.centroids[r2], distanceThreshold_)) {
        searchContext_.neighborLists[r1].push_back(r2);
      }
    }
  }

  searchContext_.isValid = true;
}

std::vector<std::pair<int, double>>
LocalSearch::rankNodesByRemovalSavings(const Route &route) {
  std::vector<std::pair<int, double>> rankings;
  const auto &nodes = route.getNodes();
  if (nodes.size() <= 2)
    return rankings;

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

// Public wrapper
std::vector<size_t> LocalSearch::findBestInsertionPositions(const Route &route,
                                                            int nodeId,
                                                            int topK) const {
  // For now, we use the TimeAware heuristic as recommended.
  return findBestInsertionPositions_TimeAware(route, nodeId, topK);
}

// Time-Aware Heuristic Implementation
std::vector<size_t>
LocalSearch::findBestInsertionPositions_TimeAware(const Route &route,
                                                  int nodeId, int topK) const {
  struct Candidate {
    size_t position;
    double cost;
    bool operator<(const Candidate &o) const { return cost < o.cost; }
  };

  std::vector<Candidate> candidates;
  const auto &nodes = route.getNodes();
  const auto &states = route.getStates();

  auto customerNode =
      std::static_pointer_cast<Customer>(instance->getNodeById(nodeId));

  for (size_t pos = 1; pos < nodes.size(); ++pos) {
    int prev = nodes[pos - 1];

    // Distance detour
    double distBefore = instance->getDistance(prev, nodes[pos]);
    double distAfter = instance->getDistance(prev, nodeId) +
                       instance->getDistance(nodeId, nodes[pos]);
    double detour = distAfter - distBefore;

    // Time window check
    double arrivalTime =
        states[pos - 1].departureTime + instance->getTime(prev, nodeId);

    double twPenalty = 0.0;
    if (arrivalTime > customerNode->getDueDate()) {
      twPenalty = 1000.0; // Infeasible penalty
    } else if (arrivalTime < customerNode->getReadyTime()) {
      twPenalty = 0.5; // Waiting penalty
    } else {
      double slack = customerNode->getDueDate() - arrivalTime;
      if (slack < 10.0) { // Arbitrary small slack value
        twPenalty = (10.0 - slack) / 10.0;
      }
    }

    double totalCost = detour * (1.0 + twPenalty);
    candidates.push_back({pos, totalCost});
  }

  // Use partial_sort for efficiency, as we only need the top K
  std::partial_sort(candidates.begin(),
                    candidates.begin() + std::min(topK, (int)candidates.size()),
                    candidates.end());

  std::vector<size_t> result;
  for (int i = 0; i < std::min(topK, (int)candidates.size()); ++i) {
    // We can add a final feasibility check here if needed, but the heuristic
    // cost should handle it
    result.push_back(candidates[i].position);
  }

  return result;
}

void LocalSearch::preprocessKNN() {
  auto customers = instance->getCustomers();

  for (const auto &customer : customers) {
    std::vector<std::pair<double, int>> distances;

    for (const auto &other : customers) {
      if (customer->getId() != other->getId()) {
        double dist = instance->getDistance(customer->getId(), other->getId());
        distances.push_back({dist, other->getId()});
      }
    }

    // Sort and take top K
    std::partial_sort(distances.begin(),
                      distances.begin() +
                          std::min(K_NEIGHBORS, (int)distances.size()),
                      distances.end());

    std::vector<int> nearestK;
    for (int i = 0; i < std::min(K_NEIGHBORS, (int)distances.size()); ++i) {
      nearestK.push_back(distances[i].second);
    }

    knnCache_[customer->getId()] = nearestK;
  }
}

void LocalSearch::preprocessGranularity() {
  auto customers = instance->getCustomers();
  int n = customers.size();

  if (n < 2) {
    avgDistance_ = 0.0;
    distanceThreshold_ = std::numeric_limits<double>::max();
    return;
  }

  // Calculate average distance between all customer pairs
  double totalDistance = 0.0;
  int pairCount = 0;

  for (size_t i = 0; i < customers.size(); ++i) {
    for (size_t j = i + 1; j < customers.size(); ++j) {
      totalDistance +=
          instance->getDistance(customers[i]->getId(), customers[j]->getId());
      pairCount++;
    }
  }

  avgDistance_ = totalDistance / pairCount;
  distanceThreshold_ = GRANULARITY_FACTOR * avgDistance_;
}

std::vector<size_t>
LocalSearch::findBestInsertionPositions_KNN(const Route &route, int nodeId,
                                            int topK) const {
  // Get K nearest neighbors of nodeId
  auto it = knnCache_.find(nodeId);
  if (it == knnCache_.end()) {
    // Fallback to original method if node not in cache (should not happen for
    // customers)
    return findBestInsertionPositions_TimeAware(route, nodeId, topK);
  }

  const auto &nearestNeighbors = it->second;
  const auto &nodes = route.getNodes();

  // Find positions adjacent to nearest neighbors present in the route
  std::set<size_t> candidatePositions;
  for (size_t pos = 0; pos < nodes.size(); ++pos) {
    int currentNodeId = nodes[pos];
    for (int neighborId : nearestNeighbors) {
      if (currentNodeId == neighborId) {
        // Add positions before and after this neighbor
        if (pos > 0)
          candidatePositions.insert(pos);
        if (pos < nodes.size())
          candidatePositions.insert(pos + 1);
        break; // Move to next node in route
      }
    }
  }

  // If no neighbors found in route, fallback to checking all positions
  if (candidatePositions.empty()) {
    return findBestInsertionPositions_TimeAware(route, nodeId, topK);
  }

  // Evaluate only candidate positions and return top K
  struct Candidate {
    size_t position;
    double cost;
    bool operator<(const Candidate &o) const { return cost < o.cost; }
  };

  std::vector<Candidate> candidates;
  const auto &states = route.getStates();
  auto customerNode =
      std::static_pointer_cast<Customer>(instance->getNodeById(nodeId));

  for (size_t pos : candidatePositions) {
    if (pos == 0 || pos >= nodes.size())
      continue; // Depot positions are invalid

    int prev = nodes[pos - 1];
    double distBefore = instance->getDistance(prev, nodes[pos]);
    double distAfter = instance->getDistance(prev, nodeId) +
                       instance->getDistance(nodeId, nodes[pos]);
    double detour = distAfter - distBefore;

    // Time window penalty (same as TimeAware)
    double arrivalTime =
        states[pos - 1].departureTime + instance->getTime(prev, nodeId);
    double twPenalty = 0.0;
    if (arrivalTime > customerNode->getDueDate()) {
      twPenalty = 1000.0;
    } else if (arrivalTime < customerNode->getReadyTime()) {
      twPenalty = 0.5;
    }

    candidates.push_back({pos, detour * (1.0 + twPenalty)});
  }

  if (candidates.empty()) {
    return {}; // No valid insertion point found
  }

  std::partial_sort(candidates.begin(),
                    candidates.begin() + std::min(topK, (int)candidates.size()),
                    candidates.end());

  std::vector<size_t> result;
  for (int i = 0; i < std::min(topK, (int)candidates.size()); ++i) {
    result.push_back(candidates[i].position);
  }

  return result;
}

MoveEvaluation LocalSearch::evaluateRelocateDelta(const Solution &solution,
                                                  const MoveDescriptor &move) {
  MoveEvaluation result;
  result.isFeasible = false; // Default to not feasible

  const auto &routes = solution.getRoutes();
  const auto &r1 = routes[move.routeIdx1];
  const bool isIntraMove = (move.routeIdx1 == move.routeIdx2);
  const auto &r2 = isIntraMove ? r1 : routes[move.routeIdx2];

  int nodeId = r1.getNodeAt(move.nodeIdx1);

  // --- Step 1: Use the existing Tier 3 check for a quick filter ---
  if (!r2.canPossiblyInsert(nodeId, move.nodeIdx2)) {
    return result; // Not feasible, exit early.
  }

  // --- Step 2: If possibly feasible, calculate the distance delta ---
  // This is a lightweight calculation without route copying.
  double distanceDelta = 0.0;
  int r1_prev = r1.getNodeAt(move.nodeIdx1 - 1);
  int r1_next = r1.getNodeAt(move.nodeIdx1 + 1);
  distanceDelta -= (instance->getDistance(r1_prev, nodeId) +
                    instance->getDistance(nodeId, r1_next));
  distanceDelta += instance->getDistance(r1_prev, r1_next);

  if (isIntraMove) {
    int insertPos = move.nodeIdx2;
    // Adjust insertion index if moving forward in the same route
    if (move.nodeIdx1 < move.nodeIdx2) {
      insertPos--;
    }
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

  result.isFeasible =
      true; // It's POSSIBLY feasible. The full evaluateMove will confirm.
  result.distanceDelta = distanceDelta;
  result.objectiveDelta =
      distanceDelta; // For now, objective is just distance delta

  return result;
}

int LocalSearch::findNearestStation(int nodeId) const {
  if (stationIds.empty()) {
    return -1;
  }

  int bestStationId = -1;
  double minDistance = std::numeric_limits<double>::max();

  for (int stationId : stationIds) {
    double distance = instance->getDistance(nodeId, stationId);
    if (distance < minDistance) {
      minDistance = distance;
      bestStationId = stationId;
    }
  }
  return bestStationId;
}

// ============================================================================
// ENERGY BOOST HELPERS (Partial Charging Strategy)
// ============================================================================

/**
 * Find the nearest station BEFORE insertPos in the route.
 * Returns the position index, or -1 if no station found.
 */
int LocalSearch::findPrecedingStation(const Route &route,
                                      size_t insertPos) const {
  const auto &nodes = route.getNodes();
  for (int i = static_cast<int>(insertPos) - 1; i >= 1; --i) {
    if (instance->getNodeById(nodes[i])->getType() == NodeType::STATION) {
      return i;
    }
  }
  return -1;
}

/**
 * Calculate energy deficit at a position in the route.
 * Uses the minBatteryReq from Backward Pass to estimate gap.
 */
double LocalSearch::calculateEnergyGap(const Route &route,
                                       size_t position) const {
  const auto &minReq = route.getMinBatteryReq();
  const auto &states = route.getStates();

  if (position >= minReq.size() || position >= states.size()) {
    return 0.0;
  }

  // Gap = what we need - what we have
  double gap = minReq[position] - states[position].remainingBattery;
  return std::max(0.0, gap);
}

/**
 * Try to perform a relocate move with energy boost if needed.
 * If the move fails due to energy, try inserting a station before the customer.
 */
bool LocalSearch::tryRelocateWithEnergyBoost(Route &routeFrom, int fromNodeIdx,
                                             Route &routeTo, size_t insertPos,
                                             double maxExtraTimeAllowed) {
  // 1. Backup original routes
  Route backupFrom = routeFrom;
  Route backupTo = routeTo;

  // 2. Perform the relocate move
  int nodeId = routeFrom.getNodeAt(fromNodeIdx);
  if (nodeId < 0)
    return false; // Invalid node

  routeFrom.removeNode(fromNodeIdx);
  routeTo.addNode(nodeId, insertPos);

  // 3. First evaluation attempt
  routeTo.evaluate();
  routeFrom.evaluate();

  if (routeTo.isFeasible() && routeFrom.isFeasible()) {
    return true; // Move succeeded without boost
  }

  // 4. If failed, try Energy Boost via Station Insertion
  if (!routeTo.isFeasible()) {
    // Rollback to state before customer insertion
    routeTo = backupTo;

    // Find nearest station to the customer
    int stationId = findNearestStation(nodeId);

    if (stationId != -1) {
      // Strategy: Insert station first, then customer
      Route testRoute = routeTo;

      // Check if we already have this station nearby to avoid duplicates
      bool hasSameStationNearby = false;
      const auto &nodes = testRoute.getNodes();
      if (insertPos > 0 && insertPos < nodes.size()) {
        int prevNode = nodes[insertPos - 1];
        if (prevNode == stationId) {
          hasSameStationNearby = true;
        }
      }
      if (insertPos < nodes.size()) {
        int nextNode = nodes[insertPos];
        if (nextNode == stationId) {
          hasSameStationNearby = true;
        }
      }

      if (!hasSameStationNearby) {
        testRoute.addNode(stationId, insertPos);
        testRoute.addNode(nodeId, insertPos + 1);
        testRoute.evaluate();

        if (testRoute.isFeasible()) {
          // Check time constraint (heuristic)
          double timeDiff = testRoute.getTotalTime() - backupTo.getTotalTime();
          if (timeDiff <= maxExtraTimeAllowed) {
            routeTo = testRoute;
            routeFrom.evaluate(); // Re-evaluate source route
            return routeFrom.isFeasible();
          }
        }
      }
    }
  }

  // 5. Rollback on failure
  routeFrom = backupFrom;
  routeTo = backupTo;
  return false;
}

int LocalSearch::removeRedundantStations(Route &route) {
  const double EPSILON = 1e-9;
  int removalCount = 0;

  // Loop until no more stations can be removed (to handle multiple redundant
  // stations)
  bool stationRemoved = true;
  while (stationRemoved) {
    stationRemoved = false;
    route.evaluate();

    if (!route.isFeasible()) {
      break;
    }

    const auto &nodes = route.getNodes();
    const auto &states = route.getStates();
    const auto &minBatteryReq = route.getMinBatteryReq();

    // Iterate backwards to avoid index shifting issues
    // range: [size-2, 1] (skip depot at 0 and size-1)
    for (int i = static_cast<int>(nodes.size()) - 2; i > 0; --i) {
      int nodeId = nodes[i];
      auto node = instance->getNodeById(nodeId);

      if (node->getType() != NodeType::STATION) {
        continue;
      }

      // Check Logic
      int prevNodeId = nodes[i - 1];
      int nextNodeId = nodes[i + 1];

      double distToSkip = instance->getDistance(prevNodeId, nextNodeId);
      double energyToSkip =
          distToSkip * route.getVehicle()->getEnergyConsumptionRate();

      double batteryAtPrev = states[i - 1].remainingBattery;

      // 1. Can reach next node?
      if (batteryAtPrev < energyToSkip - EPSILON) {
        continue;
      }

      // 2. Enough for downstream?
      double batteryAfterSkip = batteryAtPrev - energyToSkip;
      double minBatteryAtNext = minBatteryReq[i + 1];

      if (batteryAfterSkip >= minBatteryAtNext - EPSILON) {
        route.removeNode(i);
        removalCount++;
        stationRemoved = true;
        break; // Restart loop to safely handle indices and new battery states
      }
    }
  }

  return removalCount;
}

bool LocalSearch::removeRedundantStations(Solution &solution) {
  bool anyImproved = false;
  for (auto &route : solution.getRoutes()) {
    if (route.size() <= 2)
      continue;
    int removed = removeRedundantStations(route);
    if (removed > 0) {
      // route.evaluate() is called inside removeRedundantStations(route)
      anyImproved = true;
    }
  }
  return anyImproved;
}