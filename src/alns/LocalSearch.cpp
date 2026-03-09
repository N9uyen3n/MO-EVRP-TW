#include "../../include/alns/LocalSearch.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Station.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>

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
}

// ============================================================================
// Algorithm: 4-Phase Hybrid Local Search
// ============================================================================
// Input:  Solution S (feasible)
// Output: Improved Solution S'
//
// Phase 1 — Distance Optimization (Variable Neighborhood Descent)
//   Cycle through {Relocate, Swap, Or-Opt, 2-Opt} with restart-on-improve
// Phase 2 — Electricity-Free Vehicle Reduction (NEW)
//   Strip stations, merge routes in VRP-TW space (ignore battery),
//   then re-insert stations optimally → enables proactive charging patterns
// Phase 3 — Charging Optimization (every CHARGING_FREQUENCY iterations)
//   Remove redundant stations, reposition, swap, optimize amounts
// Phase 4 — Classic Vehicle Reduction (fallback, every 4 phase2 calls)
//   Multi-route merge, smallest route elimination, ejection chain
// ============================================================================
void LocalSearch::run(Solution &solution) {
  // ⭐ VITAL: Must invalidate context from previous ALNS iterations
  // because solution's routes have been structurally changed by Ruin &
  // Recreate.
  searchContext_.invalidate();

  // ⭐ FIX BUG: Reset stagnation counter at the start of each LS call.
  // noImprovementCount_ is a member variable — without this reset, a counter
  // near EARLY_STOP_THRESHOLD from a previous ALNS iteration causes LS to
  // terminate after just 1 iteration on the new (completely different) solution.
  // This was masking all improvements from VEH-Improve breakthroughs onward.
  noImprovementCount_ = 0;

  int phase2Calls = 0; // Electricity-free reduction counter
  int phase4Calls = 0; // Classic reduction counter
  for (int iter = 0; iter < MAX_LS_ITERATIONS; ++iter) {
    bool improved = false;

    // Update granular neighborhood data (centroids + neighbor lists)
    updateSearchContext(solution);

    // --- Phase 1: Distance Optimization (VND) ---
    if (runDistanceOptimization(solution)) {
      improved = true;
      searchContext_.invalidate();
    }

    // --- Phase 2: Electricity-Free Vehicle Reduction (NEW) ---
    // Bỏ qua electricity constraint, merge routes trong VRP-TW space,
    // sau đó chèn stations tối ưu để restore feasibility.
    // Chạy trước Charging Optimization để charging có nền tảng route tốt hơn.
    if (solution.getRoutes().size() > 1) {
      if (++phase2Calls % 2 == 0) {
        if (runElectricityFreeVehicleReduction(solution)) {
          improved = true;
          noImprovementCount_ = 0;
          searchContext_.invalidate();
        }
      }
    }

    // --- Phase 3: Charging Optimization (periodic) ---
    // Chạy sau Vehicle Reduction để tối ưu stations trên routes đã được merge.
    if (iter % CHARGING_FREQUENCY == 0) {
      if (runChargingOptimization(solution)) {
        improved = true;
        searchContext_.invalidate();
      }
    }

    // --- Phase 4: Classic Vehicle Reduction (fallback) ---
    // Chạy khi electricity-free pass không giảm được xe.
    if (solution.getRoutes().size() > 1) {
      if (++phase4Calls % 3 == 0) {
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

  // ================================================================
  // ⭐ FAST PATH: Distance pre-check TRƯỚC KHI COPY route
  // Reject sớm ~80% moves mà không cần tốn chi phí copy
  // ================================================================

  // --- INTER_RELOCATE: Fast path đầy đủ (checkInsertionCost, không copy) ---
  if (move.type == MoveType::INTER_RELOCATE) {
    int nodeId = routes[move.routeIdx1].getNodeAt(move.nodeIdx1);
    const auto &r2 = routes[move.routeIdx2];

    InsertionResult result = r2.checkInsertionCost(nodeId, move.nodeIdx2);
    if (!result.isFeasible) {
      return;
    }

    // ⭐ FIX: Reuse cachedRemovalSavings from searchRelocate if available.
    // evaluateRelocateDelta already computed this; don't recalculate 3
    // getDistance() calls. Falls back to direct computation if not cached.
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

    // Removal savings (3 getDistance calls)
    int prev = r1.getNodeAt(move.nodeIdx1 - 1);
    int next = r1.getNodeAt(move.nodeIdx1 + 1);
    double removalSavings = instance->getDistance(prev, nodeId) +
                            instance->getDistance(nodeId, next) -
                            instance->getDistance(prev, next);

    // Insertion cost at new position (adjusted for removal)
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

    // ⭐ Reject nếu distance không cải thiện → skip copy route
    if (distDelta >= -1e-9)
      return;

    // Distance tốt → fall through để copy route và full evaluate bên dưới
    // (chỉ ~20% moves sẽ tới đây)
  }

  // INTRA_SWAP: searchSwap đã filter distance trước khi gọi evaluateMove
  // → không cần check lại ở đây (tránh tốn 8 getDistance() thừa)

  // --- INTRA_TWO_OPT: Skip copy nếu segment không chứa station ---
  if (move.type == MoveType::INTRA_TWO_OPT) {
    const auto &r1 = routes[move.routeIdx1];
    const auto &nodes = r1.getNodes();

    // Fix 10: Dùng cờ hasStation từ searchTwoOpt (lưu tạm trong stationId) để bỏ qua loop
    bool hasStation = (move.stationId == 1);

    if (!hasStation) {
      // Không có station → distance delta đã tính ở searchTwoOpt là đủ chính
      // xác Chỉ cần verify time window bằng full copy Nhưng vẫn phải distance
      // pre-check ở đây
      int prev = nodes[move.nodeIdx1 - 1];
      int nodeI = nodes[move.nodeIdx1];
      int nodeJ = nodes[move.nodeIdx2];
      int next_node = nodes[move.nodeIdx2 + 1];

      double oldDist = instance->getDistance(prev, nodeI) +
                       instance->getDistance(nodeJ, next_node);
      double newDist = instance->getDistance(prev, nodeJ) +
                       instance->getDistance(nodeI, next_node);

      if (newDist >= oldDist - 1e-9)
        return;
    }
    // Fall through to copy route for full feasibility check
  }

  // ================================================================
  // KẾT THÚC FAST PATH — Chỉ moves có distance improvement tốt mới tới đây
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
    case MoveType::INTER_CROSS_EXCHANGE: {
      if (!twoRoutes)
        return; // Cross-exchange chỉ hỗ trợ 2 routes
      int len1 = move.segmentLength;
      int len2 = move.segmentLength2;

      // Trích xuất segments
      std::vector<int> seg1, seg2;
      seg1.reserve(len1);
      seg2.reserve(len2);
      for (int i = 0; i < len1; ++i)
        seg1.push_back(r1_copy.getNodeAt(move.nodeIdx1 + i));
      for (int i = 0; i < len2; ++i)
        seg2.push_back(r2_copy.getNodeAt(move.nodeIdx2 + i));

      // Xóa từ dưới lên trên để không làm thay đổi các index phía trước
      for (int i = len1 - 1; i >= 0; --i)
        r1_copy.removeNode(move.nodeIdx1 + i);
      for (int i = len2 - 1; i >= 0; --i)
        r2_copy.removeNode(move.nodeIdx2 + i);

      // Chèn tráo đổi: đoạn 2 vào route 1, đoạn 1 vào route 2
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
    case MoveType::INTER_TWO_OPT:
      // Inter-2-Opt is applied directly in searchInterTwoOpt (no route copies
      // needed here) This case is a no-op fallback
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
    // Applied directly in searchInterTwoOpt; nothing to do here
    break;
  default:
    // Should not happen
    break;
  }

  // ⭐ Invalidate cache ranking for modified routes
  searchContext_.markDirty(move.routeIdx1, move.routeIdx2);
  solution.markDirty(); // ⭐ Ensure Solution knows its routes were modified
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
  // N0=Relocate, N1=Swap, N2=Or-Opt, N3=Intra-2-Opt, N4=Inter-2-Opt,
  // N5=CrossExchange
  int k = 0;
  while (k < 6) {
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
    case 2: // N3: Or-Opt segments {1,2,3}
      if (searchOrOpt(solution, bestMove, weights)) {
        improved = true;
      }
      break;
    case 3: // N4: Intra-2-Opt (intra-route edge reversal)
      if (searchTwoOpt(solution, bestMove, weights)) {
        improved = true;
      }
      break;
    case 4: // N5: Inter-2-Opt (cross-route edge reconnection)
      if (searchInterTwoOpt(solution, bestMove, weights, searchContext_)) {
        improved = true;
      }
      break;
    case 5: // N6: Cross-Exchange (Tráo chéo cụm khách hàng giữa 2 xe)
      if (searchCrossExchange(solution, bestMove, weights, searchContext_)) {
        improved = true;
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
                                 SearchContext &ctx) {
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

    const auto &rankedNodes = this->getCachedRanking(r1, routes[r1], ctx);

    int nodesToCheck = std::min(maxNodesToCheck_, (int)rankedNodes.size());

    for (int k = 0; k < nodesToCheck; ++k) {
      int i = rankedNodes[k].first;
      int nodeId = routes[r1].getNodeAt(i);
      if (nodeTypeById_[nodeId] != NodeType::CUSTOMER)
        continue;

      // ⭐ SMD: Tính removal savings 1 lần duy nhất per node, reuse cho tất cả
      // (r2, j)
      int prev_n = routes[r1].getNodeAt(i - 1);
      int next_n = routes[r1].getNodeAt(i + 1);
      double removalSavings = instance->getDistance(prev_n, nodeId) +
                              instance->getDistance(nodeId, next_n) -
                              instance->getDistance(prev_n, next_n);

      for (int r2 : neighborLists[r1]) {

        // ⭐ Use KNN to find candidate positions (4 candidates for wider
        // search)
        auto candidatePositions =
            findBestInsertionPositions_KNN(routes[r2], nodeId, 4);

        for (size_t j : candidatePositions) {
          if (r1 == r2 && (j == i || j == i + 1))
            continue;

          // Tier 3 Filter: Fast feasibility check before any delta evaluation
          // For INTRA_RELOCATE, we are inserting nodeId at j, and removing it from i.
          // For INTER_RELOCATE, we are inserting nodeId at j, removing nothing from r2.
          if (!routes[r2].canPossiblyInsert(nodeId, j, (r1 == r2) ? nodeId : -1)) {
            continue;
          }

          // --- TỐI ƯU MỚI: PRE-DELTA CHECK ---
          activeMove.reset();
          activeMove.type =
              (r1 == r2) ? MoveType::INTRA_RELOCATE : MoveType::INTER_RELOCATE;
          activeMove.routeIdx1 = r1;
          activeMove.nodeIdx1 = i;
          activeMove.routeIdx2 = r2;
          activeMove.nodeIdx2 = j;
          activeMove.cachedRemovalSavings =
              removalSavings; // ⭐ Reuse cached value

          // Use the lightweight delta evaluation as a filter.
          MoveEvaluation preEval = evaluateRelocateDelta(solution, activeMove);

          // If the move is not possibly feasible or it increases distance, skip
          if (!preEval.isFeasible || preEval.distanceDelta >= -1e-9) {
            continue;
          }

          // If the pre-check passes, perform the full, expensive evaluation.
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
              searchContext_.markDirty(r1, r2);
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

        // ⭐ KNN / Granular Filter: At least one new edge must be "short"
        if (instance->getDistance(prev, nodeJ) >= distanceThreshold_ &&
            instance->getDistance(nodeI, next) >= distanceThreshold_) {
          continue;
        }

        // Tạo move descriptor
        activeMove.reset();
        activeMove.type = MoveType::INTRA_TWO_OPT;
        activeMove.routeIdx1 = r;
        activeMove.nodeIdx1 = i;
        activeMove.routeIdx2 = r;
        activeMove.nodeIdx2 = j;

        // ⭐ Fix Bug: Set hasStation flag (dùng stationId field làm cờ tạm)
        // evaluateMove dòng 189 đọc: hasStation = (move.stationId == 1)
        // Nếu không set → stationId = -1 (default) → hasStation luôn false
        // → bỏ qua full copy khi segment có station → accept infeasible moves
        {
          bool segHasStation = false;
          for (int k = i; k <= j && !segHasStation; ++k)
            if (nodeTypeById_[nodes[k]] == NodeType::STATION)
              segHasStation = true;
          activeMove.stationId = segHasStation ? 1 : 0;
        }

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

// ============================================================================
// Inter-route 2-Opt: Cross-route edge reconnection
// ============================================================================
// Với mỗi cặp route (r1, r2) và cặp cạnh (i → i+1) trong r1 và (j → j+1) trong
// r2, thử kết nối lại: r1[0..i] + r2[j+1..end] và r2[0..j] + r1[i+1..end]
// (Tương đương hoán đổi suffix giữa 2 route.)
// ============================================================================
bool LocalSearch::searchInterTwoOpt(Solution &solution,
                                    MoveDescriptor &outBestMove,
                                    const LocalSearchWeights &weights,
                                    SearchContext &ctx) {
  auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  if (numRoutes < 2)
    return false;

  // ⭐ SAFETY: Validate context
  if (!ctx.isValid || (int)ctx.centroids.size() != numRoutes)
    return false;

  const auto &centroids = ctx.centroids;

  for (int r1 = 0; r1 < numRoutes; ++r1) {
    const auto &nodes1 = routes[r1].getNodes();
    int n1 = (int)nodes1.size();
    if (n1 < 3)
      continue; // Need at least depot + 1 customer + depot

      // ⭐ OPTIMIZATION: Use granular neighbor lists to avoid O(R^2)
      for (int r2 : ctx.neighborLists[r1]) {
        if (r2 <= r1) continue; // Avoid redundant pairs since neighbor list is symmetric

        const auto &nodes2 = routes[r2].getNodes();
        int n2 = (int)nodes2.size();
        if (n2 < 3)
          continue;

      // Try all edge pairs (i, j): edge (i → i+1) from r1, edge (j → j+1) from
      // r2 i ranges from 1..n1-2: skip cut at depot_start (i=0) and depot_end
      // (i=n1-1) j ranges from 1..n2-2: same
      for (int i = 1; i < n1 - 1; ++i) {
        for (int j = 1; j < n2 - 1; ++j) {
          // Quick distance check: does reconnection save distance?
          // Old edges: (nodes1[i] → nodes1[i+1]) + (nodes2[j] → nodes2[j+1])
          // New edges: (nodes1[i] → nodes2[j+1]) + (nodes2[j] → nodes1[i+1])
          double oldEdges = instance->getDistance(nodes1[i], nodes1[i + 1]) +
                            instance->getDistance(nodes2[j], nodes2[j + 1]);
          double newEdges = instance->getDistance(nodes1[i], nodes2[j + 1]) +
                            instance->getDistance(nodes2[j], nodes1[i + 1]);

          // Strong filter: Only attempt full copy and eval if edge exchange
          // saves at least 0.5 distance
          if (newEdges >= oldEdges - 0.5)
            continue;

          // Build the two new routes by splicing:
          // newR1 = nodes1[0..i] + nodes2[j+1..n2-2] + depot_end
          // newR2 = nodes2[0..j] + nodes1[i+1..n1-2] + depot_end
          Route newR1 = routes[r1];
          Route newR2 = routes[r2];

          // Trim newR1: keep [0..i] + trailing depot → target size = i + 2
          // Remove from position i+1 (before trailing depot) one by one
          while ((int)newR1.size() > i + 2) {
            newR1.removeNode(i + 1); // always remove the node at position i+1
          }
          // Append suffix of r2: nodes2[j+1 .. n2-2] (skip both depots of r2)
          for (int k = j + 1; k <= n2 - 2; ++k) {
            newR1.addNode(nodes2[k],
                          newR1.size() - 1); // insert before trailing depot
          }

          // Trim newR2: keep [0..j] + trailing depot → target size = j + 2
          while ((int)newR2.size() > j + 2) {
            newR2.removeNode(j + 1);
          }
          // Append suffix of r1: nodes1[i+1 .. n1-2] (skip both depots of r1)
          for (int k = i + 1; k <= n1 - 2; ++k) {
            newR2.addNode(nodes1[k], newR2.size() - 1);
          }

          newR1.evaluate();
          newR2.evaluate();

          if (!newR1.isFeasible() || !newR2.isFeasible())
            continue;

          double oldDist =
              routes[r1].getTotalDistance() + routes[r2].getTotalDistance();
          double newDist = newR1.getTotalDistance() + newR2.getTotalDistance();

          if (newDist < oldDist - 1e-9) {
            // Apply the move directly
            routes[r1] = newR1;
            routes[r2] = newR2;
            solution.evaluateRoutes();

            // ⭐ MUST mark dirty because we bypass applyMove!
            ctx.markDirty(r1, r2);

            return true;
          }
        }
      }
    }
  }
  return false;
}

bool LocalSearch::searchSwap(Solution &solution, MoveDescriptor &outBestMove,
                             const LocalSearchWeights &weights,
                             SearchContext &ctx) {
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
      if (nodeTypeById_[nodes[i]] != NodeType::CUSTOMER)
        continue;

      for (int j = i + 2; j < n - 1; ++j) {
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
    // ⭐ OPTIMIZATION: Use granular neighbor lists instead of O(R^2) all routes
    for (int r2 : ctx.neighborLists[r1]) {
      if (r2 <= r1)
        continue; // Avoid redundant pairs since neighbor list is symmetric

      // Route-level distance check is no longer needed since neighborLists
      // already filters it

      const auto &nodes1 = routes[r1].getNodes();
      const auto &nodes2 = routes[r2].getNodes();

      const auto &ranked1 = this->getCachedRanking(r1, routes[r1], ctx);
      const auto &ranked2 = this->getCachedRanking(r2, routes[r2], ctx);

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
          if (instance->getDistance(nodeId1, nodeId2) >= distanceThreshold_)
            continue;

          // ⭐ OPTIMIZATION: O(1) Distance Pre-check BEFORE expensive O(N)
          // checks
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
            continue; // Skip if no distance improvement (huge time saver)

          // ⭐ PRUNE: TW Feasibility — check đủ 4 cạnh sau swap:
          // Trước swap: prev1→nodeId1→next1,  prev2→nodeId2→next2
          // Sau  swap:  prev1→nodeId2→next1,  prev2→nodeId1→next2
          // Cần check cả "node mới có vào được vị trí đó không" (prev→node)
          // lẫn "node mới có kịp successor không" (node→next)
          if (!twNext_[prev1][nodeId2] || !twNext_[nodeId2][next1] ||
              !twNext_[prev2][nodeId1] || !twNext_[nodeId1][next2])
            continue;

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
  // Or-Opt thử 3 loại segment: độ dài 1, 2, và 3
  static const int SEGMENT_LENGTHS[] = {1, 2, 3};

  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  // ⭐ SAFETY: Validate context for granular filtering
  if (!searchContext_.isValid ||
      (int)searchContext_.neighborLists.size() != numRoutes) {
    return false;
  }
  const auto &neighborLists = searchContext_.neighborLists;

  for (int segLen : SEGMENT_LENGTHS) {
    for (int r1 = 0; r1 < numRoutes; ++r1) {
      // Cần ít nhất segLen khách hàng + 2 depot
      if ((int)routes[r1].size() < 2 + segLen)
        continue;

      for (int i = 1; i <= (int)routes[r1].size() - 1 - segLen; ++i) {

        // --- Lọc: Kiểm tra đoạn có phải toàn khách hàng không ---
        bool segmentIsCustomers = true;
        for (int k = 0; k < segLen; ++k) {
          if (nodeTypeById_[routes[r1].getNodeAt(i + k)] !=
              NodeType::CUSTOMER) {
            segmentIsCustomers = false;
            break;
          }
        }
        if (!segmentIsCustomers)
          continue;

        int firstNodeId = routes[r1].getNodeAt(i);
        int lastNodeId = routes[r1].getNodeAt(i + segLen - 1);

        // ⭐ OPTIMIZATION: Tính O(1) removal savings 1 lần cho nguyên đoạn
        int prev1 = routes[r1].getNodeAt(i - 1);
        int next1 = routes[r1].getNodeAt(i + segLen);
        double removalSavings = instance->getDistance(prev1, firstNodeId) +
                                instance->getDistance(lastNodeId, next1) -
                                instance->getDistance(prev1, next1);

        // ⭐ OPTIMIZATION: Use granular neighbor lists instead of all routes
        for (int r2 : neighborLists[r1]) {
          // Use KNN to find candidate positions based on the first node of
          // segment KNN = 3 for Or-Opt since segment evaluation is expensive
          // and we test 3 lengths
          auto candidatePositions =
              findBestInsertionPositions_KNN(routes[r2], firstNodeId, 3);

          for (int j : candidatePositions) {

            // --- Lọc: Không chèn lại vào vị trí cũ ---
            if (r1 == r2 && (j >= i && j <= i + segLen)) {
              continue;
            }

            // ⭐ OPTIMIZATION: O(1) Distance Pre-check BEFORE expensive O(N)
            // evaluation
            int prev2 = routes[r2].getNodeAt(j - 1);
            int next2 = routes[r2].getNodeAt(j);

            double insertionCost = instance->getDistance(prev2, firstNodeId) +
                                   instance->getDistance(lastNodeId, next2) -
                                   instance->getDistance(prev2, next2);

            // Đối với chèn lại nội bộ cùng 1 Route (Intra-OrOpt) gần kề
            if (r1 == r2) {
              if (j == i || j == i + segLen)
                continue; // Cùng vị trí không đổi
            }

            if (insertionCost >= removalSavings - 1e-9) {
              continue; // Không cải thiện khoảng cách, bỏ qua không tốn tiền
                        // copy Route
            }

            activeMove.reset();
            activeMove.type = MoveType::INTER_OR_OPT;
            activeMove.routeIdx1 = r1;
            activeMove.nodeIdx1 = i;
            activeMove.routeIdx2 = r2;
            activeMove.nodeIdx2 = j;
            activeMove.segmentLength = segLen;

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

bool LocalSearch::searchCrossExchange(Solution &solution,
                                      MoveDescriptor &outBestMove,
                                      const LocalSearchWeights &weights,
                                      SearchContext &ctx) {
  static const int SEGMENT_LENGTHS[] = {1, 2, 3};
  const auto &routes = solution.getRoutes();
  int numRoutes = routes.size();

  if (!ctx.isValid || (int)ctx.neighborLists.size() != numRoutes) {
    return false;
  }
  const auto &neighborLists = ctx.neighborLists;

  for (int len1 : SEGMENT_LENGTHS) {
    for (int len2 : SEGMENT_LENGTHS) {
      // Bỏ qua (1,1) vì nó tương đương INTER_SWAP đã chạy triệt để trước đó
      if (len1 == 1 && len2 == 1)
        continue;

      for (int r1 = 0; r1 < numRoutes; ++r1) {
        if ((int)routes[r1].size() < 2 + len1)
          continue;

        for (int r2 : neighborLists[r1]) {
          if (r2 <= r1)
            continue; // Cross Exchange đối xứng (r1,r2) giống (r2,r1) nhưng đảo
                      // len
          // Tuy nhiên ta duyệt mọi cặp (len1, len2), nên (r1 ngắn=len1, r2
          // dài=len2) sẽ được cover thành (r2 ngắn=len1, r1 dài=len2) ở vòng
          // lặp len khác. Nên việc ép r2 > r1 là an toàn và đủ.
          if ((int)routes[r2].size() < 2 + len2)
            continue;

          for (int i = 1; i <= (int)routes[r1].size() - 1 - len1; ++i) {
            // Kiểm tra đoạn 1 có phải toàn Customer không
            bool seg1Ok = true;
            for (int k = 0; k < len1; ++k) {
              if (instance->getNodeById(routes[r1].getNodeAt(i + k))
                      ->getType() != NodeType::CUSTOMER) {
                seg1Ok = false;
                break;
              }
            }
            if (!seg1Ok)
              continue;

            int first1 = routes[r1].getNodeAt(i);
            int last1 = routes[r1].getNodeAt(i + len1 - 1);
            int prev1 = routes[r1].getNodeAt(i - 1);
            int next1 = routes[r1].getNodeAt(i + len1);

            double rem1 = instance->getDistance(prev1, first1) +
                          instance->getDistance(last1, next1) -
                          instance->getDistance(prev1, next1);

            for (int j = 1; j <= (int)routes[r2].size() - 1 - len2; ++j) {
              // Kiểm tra đoạn 2
              bool seg2Ok = true;
              for (int k = 0; k < len2; ++k) {
                if (instance->getNodeById(routes[r2].getNodeAt(j + k))
                        ->getType() != NodeType::CUSTOMER) {
                  seg2Ok = false;
                  break;
                }
              }
              if (!seg2Ok)
                continue;

              int first2 = routes[r2].getNodeAt(j);
              int last2 = routes[r2].getNodeAt(j + len2 - 1);
              int prev2 = routes[r2].getNodeAt(j - 1);
              int next2 = routes[r2].getNodeAt(j + len2);

              // 🌟 Node-level filter: Các cụm phải gần nhau
              if (instance->getDistance(first1, first2) >= distanceThreshold_)
                continue;

              // 🌟 Capacity check (Pre-check O(1))
              double demand1 = 0.0, demand2 = 0.0;
              for (int k = 0; k < len1; ++k)
                demand1 +=
                    std::static_pointer_cast<Customer>(
                        instance->getNodeById(routes[r1].getNodeAt(i + k)))
                        ->getDemand();
              for (int k = 0; k < len2; ++k)
                demand2 +=
                    std::static_pointer_cast<Customer>(
                        instance->getNodeById(routes[r2].getNodeAt(j + k)))
                        ->getDemand();

              if (routes[r1].getTotalDemand() - demand1 + demand2 >
                  routes[r1].getVehicle()->getCapacity())
                continue;
              if (routes[r2].getTotalDemand() - demand2 + demand1 >
                  routes[r2].getVehicle()->getCapacity())
                continue;

              double rem2 = instance->getDistance(prev2, first2) +
                            instance->getDistance(last2, next2) -
                            instance->getDistance(prev2, next2);

              // 🌟 OPTIMIZATION: O(1) Distance Pre-check — lá chắn tuyệt đối
              // chặn evaluate()
              double ins1 = instance->getDistance(prev1, first2) +
                            instance->getDistance(last2, next1) -
                            instance->getDistance(prev1, next1);
              double ins2 = instance->getDistance(prev2, first1) +
                            instance->getDistance(last1, next2) -
                            instance->getDistance(prev2, next2);

              double distanceDelta = (ins1 + ins2) - (rem1 + rem2);
              if (distanceDelta >= -1e-9)
                continue; // Phải cải thiện distance mới xử lý

              activeMove.reset();
              activeMove.type = MoveType::INTER_CROSS_EXCHANGE;
              activeMove.routeIdx1 = r1;
              activeMove.nodeIdx1 = i;
              activeMove.routeIdx2 = r2;
              activeMove.nodeIdx2 = j;
              activeMove.segmentLength = len1;
              activeMove.segmentLength2 = len2;

              evaluateMove(solution, activeMove, weights);

              if (activeMove.eval.isFeasible &&
                  activeMove.eval.objectiveDelta < -1e-9) {
                // First Improvement
                applyMove(solution, activeMove);
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

  // Duyệt tất cả các routes để tìm stations có thể loại bỏ
  for (int r = 0; r < routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();

    // Tìm tất cả stations trong route
    std::vector<int> stationPositions;
    for (int i = 1; i < nodes.size() - 1; ++i) {
      if (nodeTypeById_[nodes[i]] == NodeType::STATION) {
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
      if (nodeTypeById_[currentStationId] !=
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

// ⭐ Station Swap Local Search
// Purpose: Replace suboptimal stations with better alternatives at the same
// route position. Filter is based on actual detour cost at the station's
// position (prev→station→next), NOT centroid distance.
// Centroid distance was a proxy that filtered out valid candidates when stations
// are not uniformly distributed — replaced with route-level detour comparison.
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
      if (instance->getNodeById(nodeId)->getType() != NodeType::STATION)
        continue;

      // Compute current station detour cost at this position (prev→st→next)
      int prevId = nodes[i - 1];
      int nextId = nodes[i + 1];
      double currentDetour = instance->getDistance(prevId, nodeId) +
                             instance->getDistance(nodeId, nextId);

      // 3. Try replacing with each other station
      int bestReplacement = -1;
      double bestImprovement = 0.0;

      for (int candidateId : stationIds) {
        if (candidateId == nodeId)
          continue;

        // ⭐ FIX: Filter by actual detour cost at THIS position, not centroid
        // distance. Candidate must have lower detour cost than current station.
        double candidateDetour = instance->getDistance(prevId, candidateId) +
                                 instance->getDistance(candidateId, nextId);
        if (candidateDetour >= currentDetour - EPSILON)
          continue; // Not cheaper at this position — skip

        // Simulate replacement
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
      if (nodeTypeById_[nodes[i]] == NodeType::STATION) {
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

        // 🚀 Chỉ remove Unconditionally nếu Trạm Sạc nằm NGAY TRÊN Depot
        // (sameAsDepot). Vị trí pos=1 đứng độc lập có thể critical cho battery
        // của cả chuyến đi dài, verify isFeasible() là chưa đủ.
        if (sameAsDepot) {
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
      if (nodeTypeById_[nodes[i]] == NodeType::STATION) {
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
    // FIX STALE NODES REFERENCE: Strategy 2 might have removed nodes, so
    // `nodes` reference from top is stale
    const auto &currentNodes = routes[r].getNodes();
    std::vector<size_t> stationPositions;
    for (size_t i = 1; i < currentNodes.size() - 1; ++i) {
      if (nodeTypeById_[currentNodes[i]] ==
          NodeType::STATION) {
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

  // Strategy 3: Ejection Chain — khi depth=1 không đủ cho instance R với TW
  // chặt
  if (ejectionChain(solution)) {
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

  // =========================================================================
  // Station-Aware Globally-Best Insertion
  //
  // Với mỗi customer chưa nhét được, thử đồng thời:
  //   Option A: Direct insertion (checkInsertionCost)
  //   Option B: Station BEFORE customer  → [stat, cust]
  //   Option C: Station AFTER customer   → [cust, stat]
  // Chọn globally best (route, pos, option) rồi apply.
  // Lý do "globally best" thay vì greedy per-customer:
  //   Tránh trường hợp customer dễ chiếm slot tốt, block customer khó hơn.
  // =========================================================================

  struct InsertCandidate {
    int custId = -1;
    int routeIdx = -1;
    size_t pos = 0;
    double cost = 1e18;
    int stationId = -1;     // -1 = direct, else station-assisted
    bool statBefore = true; // true = [stat,cust], false = [cust,stat]
  };

  std::vector<int> unplaced = customersToMove;

  // Sau line 1686 "std::vector<int> unplaced = customersToMove;"
  // Sort theo ready time để TW-tight customers được xử lý trước
  std::sort(unplaced.begin(), unplaced.end(), [&](int a, int b) {
    auto ca = instance->getNodeById(a);
    auto cb = instance->getNodeById(b);
    double twA = ca->getDueDate() - ca->getReadyTime();
    double twB = cb->getDueDate() - cb->getReadyTime();
    return twA < twB; // Tightest TW first
  });

  // ── Helper: tìm best insertion cho 1 customer (direct + station-assisted) ─
  // Dùng top-3 stations by detour thay vì nearest-1.
  // ⭐ TW-AWARE COST: Với tight-TW customers (window ≤ 30), thêm waiting
  // penalty vào cost. Giúp R103 (window=10), RC202 (cross-cluster TW), RC205
  // (zero-window) tìm được temporal slot đúng thay vì slot cheap nhất.
  auto findBestForCustomer = [&](int custId) -> InsertCandidate {
    InsertCandidate best;
    best.custId = custId;

    auto custNode = instance->getNodeById(custId);
    double twWindow = custNode->getDueDate() - custNode->getReadyTime();
    bool isTightTW = (twWindow <= 30.0);

    for (int r = 0; r < (int)newRoutes.size(); ++r) {
      const auto &nodes = newRoutes[r].getNodes();
      const auto &states = newRoutes[r].getStates();

      for (size_t pos = 1; pos < nodes.size(); ++pos) {

        // Option A: Direct
        if (newRoutes[r].canPossiblyInsert(custId, pos)) {
          InsertionResult res = newRoutes[r].checkInsertionCost(custId, pos);
          if (res.isFeasible) {
            double cost = res.deltaDistance;
            // ⭐ Temporal fit bonus for tight-TW customers
            if (isTightTW && pos <= states.size()) {
              double travelToPrev = instance->getTime(nodes[pos - 1], custId);
              double arrivalAtCust =
                  states[pos - 1].departureTime + travelToPrev;
              double slack = custNode->getDueDate() - arrivalAtCust;
              if (slack < 0)
                continue; // hard reject (should not happen if isFeasible)
              // Penalize waiting time — prefer slots where we arrive close to
              // readyTime
              double waitPenalty =
                  std::max(0.0, custNode->getReadyTime() - arrivalAtCust);
              cost += waitPenalty * 0.3;
            }
            if (cost < best.cost)
              best = {custId, r, pos, cost, -1, true};
          }
        }

        // Options B & C: Station-assisted, top-3 stations by detour
        int prevId = nodes[pos - 1];
        int nextId = nodes[pos];
        double directDist = instance->getDistance(prevId, nextId);

        struct SC {
          int id;
          double det;
        };
        std::vector<SC> sc;
        sc.reserve(stationIds.size());
        for (int sid : stationIds)
          sc.push_back({sid, instance->getDistance(prevId, sid) +
                                 instance->getDistance(sid, nextId) -
                                 directDist});
        int topK = std::min(3, (int)sc.size());
        std::partial_sort(
            sc.begin(), sc.begin() + topK, sc.end(),
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
              double dc =
                  copy.getTotalDistance() - newRoutes[r].getTotalDistance();
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
              double dc =
                  copy.getTotalDistance() - newRoutes[r].getTotalDistance();
              if (dc < best.cost)
                best = {custId, r, pos, dc, sid, false};
            }
          }
        }
      }
    }
    return best;
  };

  // ── Apply InsertCandidate ─────────────────────────────────────────────────
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

  // ── Regret-2 Insertion ────────────────────────────────────────────────────
  // Mỗi iteration: tính regret = cost(rank2) - cost(rank1) cho từng customer.
  // Insert customer có regret CAO NHẤT trước — nếu trì hoãn sẽ mất nhiều nhất.
  // Khác với "cheapest first": tránh customer dễ (TW rộng) chiếm slot của
  // customer khó (TW hẹp).
  while (!unplaced.empty()) {
    int bestIdx = -1;
    double maxRegret = -1e18;
    InsertCandidate bestCand;

    for (int idx = 0; idx < (int)unplaced.size(); ++idx) {
      int cId = unplaced[idx];
      // Collect top-2 direct insertions for regret calculation
      InsertCandidate rank1, rank2;
      rank1.custId = cId;
      rank2.custId = cId;
      rank2.cost = 1e18;

      for (int r = 0; r < (int)newRoutes.size(); ++r) {
        for (size_t pos = 1; pos < newRoutes[r].size(); ++pos) {
          if (!newRoutes[r].canPossiblyInsert(cId, pos))
            continue;
          InsertionResult res = newRoutes[r].checkInsertionCost(cId, pos);
          if (!res.isFeasible)
            continue;
          if (res.deltaDistance < rank1.cost) {
            rank2 = rank1;
            rank1 = {cId, r, pos, res.deltaDistance, -1, true};
          } else if (res.deltaDistance < rank2.cost) {
            rank2 = {cId, r, pos, res.deltaDistance, -1, true};
          }
        }
      }

      if (rank1.routeIdx == -1)
        continue; // không thể direct-insert

      double regret = (rank2.routeIdx == -1) ? 1e15 : (rank2.cost - rank1.cost);
      if (regret > maxRegret) {
        maxRegret = regret;
        bestIdx = idx;
        bestCand = rank1;
      }
    }

    if (bestIdx == -1)
      break; // không còn direct insertion nào → sang station pass

    applyCandidate(bestCand);
    unplaced.erase(unplaced.begin() + bestIdx);
  }

  // ── Station-assisted pass cho unplaced còn lại ───────────────────────────
  // (customers mà direct insertion thất bại, thường do energy constraint)
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
    return false; // Some customers couldn't be placed → rollback (no changes
                  // applied to solution yet)

  // Validate all newRoutes before committing
  for (auto &route : newRoutes) {
    route.evaluate();
    if (!route.isFeasible())
      return false;
  }

  // Commit: replace solution routes
  while (solution.getNumRoutes() > 0)
    solution.removeRoute(0);
  for (auto &route : newRoutes)
    solution.addRoute(route);

  solution.evaluateRoutes();
  return true;
}

// ============================================================================
// ejectionChain — Strategy 3 Vehicle Reduction
//
// Xử lý TOÀN BỘ smallestRoute như 1 pool. Mục tiêu: redistribute tất cả
// customers trong pool vào V-1 routes còn lại, không để lại customer nào.
//
// Stage A: Regret-2 direct insertion vào workRoutes (không eject).
// Stage B: Ejection depth-1 cho unplaced: eject Y từ route R, insert X vào R,
//          insert Y vào route khác (direct hoặc station-assisted).
// Stage C: Station-assisted insertion cho unplaced còn lại.
//
// Commit CHỈ KHI pool hoàn toàn rỗng (tất cả customers đã placed).
// ============================================================================
bool LocalSearch::ejectionChain(Solution &solution) {
  auto &routes = solution.getRoutes();
  int numRoutes = static_cast<int>(routes.size());
  if (numRoutes < 2)
    return false;

  // Tìm smallest route (≤ 20 customers)
  int smallestIdx = -1;
  size_t minCustomers = std::numeric_limits<size_t>::max();
  for (int r = 0; r < numRoutes; ++r) {
    size_t cnt = routes[r].getCustomers().size();
    if (cnt > 0 && cnt < minCustomers) {
      minCustomers = cnt;
      smallestIdx = r;
    }
  }
  if (smallestIdx == -1 || minCustomers > 20)
    return false;

  const double vehCap = instance->getVehicleCapacity();

  // Pool = tất cả customers từ smallestRoute
  std::vector<int> pool = routes[smallestIdx].getCustomers();

  // workRoutes = tất cả routes TRỪ smallestRoute
  std::vector<Route> workRoutes;
  workRoutes.reserve(numRoutes - 1);
  for (int r = 0; r < numRoutes; ++r)
    if (r != smallestIdx)
      workRoutes.push_back(routes[r]);

  // ── Helper: direct insertion cost của custId vào workRoutes ──────────────
  // ⭐ TW-AWARE: Với tight-TW customers, thêm waiting penalty vào cost để
  // ưu tiên temporal-compatible slots thay vì cheapest-distance slots.
  // Áp dụng cùng pattern với findBestForCustomer trong tryEliminateSmallestRoute.
  struct PlaceResult {
    int r = -1;
    size_t pos = 0;
    double cost = 1e18;
  };

  auto findDirect = [&](int cId, const std::vector<Route> &wr) -> PlaceResult {
    PlaceResult best;
    double dem = instance->getNodeById(cId)->getDemand();
    auto custNode = instance->getNodeById(cId);
    double twWindow = custNode->getDueDate() - custNode->getReadyTime();
    bool isTightTW = (twWindow <= 30.0);

    for (int r = 0; r < (int)wr.size(); ++r) {
      if (wr[r].getTotalDemand() + dem > vehCap)
        continue;
      const auto &states = wr[r].getStates();
      for (size_t pos = 1; pos < wr[r].size(); ++pos) {
        if (!wr[r].canPossiblyInsert(cId, pos))
          continue;
        InsertionResult res = wr[r].checkInsertionCost(cId, pos);
        if (!res.isFeasible)
          continue;
        double cost = res.deltaDistance;
        if (isTightTW && pos <= states.size()) {
          double travelTime =
              instance->getTime(wr[r].getNodeAt(pos - 1), cId);
          double arrivalAtCust = states[pos - 1].departureTime + travelTime;
          double waitPenalty =
              std::max(0.0, custNode->getReadyTime() - arrivalAtCust);
          cost += waitPenalty * 0.3;
        }
        if (cost < best.cost)
          best = {r, pos, cost};
      }
    }
    return best;
  };

  // ── Helper: station-assisted insertion ───────────────────────────────────
  auto applyStation = [&](int cId, std::vector<Route> &wr) -> bool {
    double dem = instance->getNodeById(cId)->getDemand();
    for (int r = 0; r < (int)wr.size(); ++r) {
      if (wr[r].getTotalDemand() + dem > vehCap)
        continue;
      const auto &nodes = wr[r].getNodes();
      for (size_t pos = 1; pos < nodes.size(); ++pos) {
        int prevId = nodes[pos - 1], nextId = nodes[pos];
        double direct = instance->getDistance(prevId, nextId);
        struct SC {
          int id;
          double det;
        };
        std::vector<SC> sc;
        for (int sid : stationIds)
          sc.push_back({sid, instance->getDistance(prevId, sid) +
                                 instance->getDistance(sid, nextId) - direct});
        int topK = std::min(3, (int)sc.size());
        std::partial_sort(
            sc.begin(), sc.begin() + topK, sc.end(),
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

  // ── Stage A: Regret-2 direct insertion ───────────────────────────────────
  bool progress = true;
  while (!unplaced.empty() && progress) {
    progress = false;
    int bestIdx = -1;
    double maxRegret = -1e18;
    PlaceResult bestPlace;

    for (int idx = 0; idx < (int)unplaced.size(); ++idx) {
      int cId = unplaced[idx];
      double dem = instance->getNodeById(cId)->getDemand();
      PlaceResult rank1, rank2;
      rank2.cost = 1e18;

      for (int r = 0; r < (int)workRoutes.size(); ++r) {
        if (workRoutes[r].getTotalDemand() + dem > vehCap)
          continue;
        for (size_t pos = 1; pos < workRoutes[r].size(); ++pos) {
          if (!workRoutes[r].canPossiblyInsert(cId, pos))
            continue;
          InsertionResult res = workRoutes[r].checkInsertionCost(cId, pos);
          if (!res.isFeasible)
            continue;
          if (res.deltaDistance < rank1.cost) {
            rank2 = rank1;
            rank1 = {r, pos, res.deltaDistance};
          } else if (res.deltaDistance < rank2.cost) {
            rank2 = {r, pos, res.deltaDistance};
          }
        }
      }
      if (rank1.r == -1)
        continue;

      double regret = (rank2.r == -1) ? 1e15 : (rank2.cost - rank1.cost);
      if (regret > maxRegret) {
        maxRegret = regret;
        bestIdx = idx;
        bestPlace = rank1;
      }
    }

    if (bestIdx == -1)
      break;
    workRoutes[bestPlace.r].addNode(unplaced[bestIdx], bestPlace.pos);
    workRoutes[bestPlace.r].evaluate();
    unplaced.erase(unplaced.begin() + bestIdx);
    progress = true;
  }

  // ── Stage B: Ejection depth-1 ─────────────────────────────────────────────
  // Với mỗi X chưa placed: thử eject Y từ route R, insert X vào R, place Y
  // elsewhere.
  std::unordered_map<int, int> ejectPenalty;

  if (!unplaced.empty()) {
    std::vector<int> stillUnplaced;
    for (int xId : unplaced) {
      double demX = instance->getNodeById(xId)->getDemand();
      bool placed = false;

      for (int r = 0; r < (int)workRoutes.size() && !placed; ++r) {
        if (workRoutes[r].getTotalDemand() + demX > vehCap)
          continue;

        // Tìm Y tốt nhất trong route r để eject
        // Y tốt = có nhiều options nhất ở routes khác (capacity) + TW rộng
        // (dễ tái insert) - penalty nếu đã fail trước đó.
        // ⭐ FIX: Thêm TW width vào score — ưu tiên eject customer TW rộng
        // (dễ place lại), giữ lại customer TW hẹp (khó place lại ở routes khác)
        int bestY = -1, bestYPos = -1;
        double bestYScore = -1e18;
        const auto &rnodes = workRoutes[r].getNodes();
        for (int i = 1; i < (int)rnodes.size() - 1; ++i) {
          int yId = rnodes[i];
          if (nodeTypeById_[yId] != NodeType::CUSTOMER)
            continue;
          double demY = instance->getNodeById(yId)->getDemand();
          auto custY = instance->getNodeById(yId);
          int receivable = 0;
          for (int r2 = 0; r2 < (int)workRoutes.size(); ++r2) {
            if (r2 == r)
              continue;
            if (workRoutes[r2].getTotalDemand() + demY <= vehCap)
              receivable++;
          }
          // TW width normalized: wider TW → easier to reinsert elsewhere
          double twWidth = custY->getDueDate() - custY->getReadyTime();
          double twScore = twWidth / 100.0; // normalize to ~same scale as receivable
          double scoreY =
              receivable + twScore - ejectPenalty[yId] * 1000.0;
          if (scoreY > bestYScore) {
            bestYScore = scoreY;
            bestY = yId;
            bestYPos = i;
          }
        }
        if (bestY == -1)
          continue;

        // Thử eject Y, insert X vào r, place Y elsewhere
        std::vector<Route> tryCopy = workRoutes;
        tryCopy[r].removeNode(static_cast<size_t>(bestYPos));
        tryCopy[r].evaluate();

        // Insert X into route r
        PlaceResult xPlace;
        for (size_t pos = 1; pos < tryCopy[r].size(); ++pos) {
          if (!tryCopy[r].canPossiblyInsert(xId, pos))
            continue;
          InsertionResult res = tryCopy[r].checkInsertionCost(xId, pos);
          if (res.isFeasible && res.deltaDistance < xPlace.cost)
            xPlace = {r, pos, res.deltaDistance};
        }
        if (xPlace.r == -1)
          continue;

        tryCopy[r].addNode(xId, xPlace.pos);
        tryCopy[r].evaluate();
        if (!tryCopy[r].isFeasible())
          continue;

        // Place Y elsewhere (direct first, then station-assisted)
        PlaceResult yPlace = findDirect(bestY, tryCopy);
        if (yPlace.r != -1) {
          tryCopy[yPlace.r].addNode(bestY, yPlace.pos);
          tryCopy[yPlace.r].evaluate();
          if (!tryCopy[yPlace.r].isFeasible())
            continue;
        } else {
          if (!applyStation(bestY, tryCopy))
            continue;
        }

        // Validate all
        bool allOk = true;
        for (auto &wr : tryCopy) {
          wr.evaluate();
          if (!wr.isFeasible()) {
            allOk = false;
            break;
          }
        }

        if (!allOk) {
          ejectPenalty[bestY]++; // Penalty vì eject Y nhưng fail
          continue;
        }

        workRoutes = tryCopy;
        placed = true;
      }

      if (!placed)
        stillUnplaced.push_back(xId);
    }
    unplaced = stillUnplaced;
  }

  // ── Stage B2: Ejection depth-2 cho unplaced còn lại ──────────────────
  // ⭐ PERF FIX: Cap inner loops to top-5 candidates by removal savings to
  // prevent O(V³×N³) worst case. Stage B2 fires only when Stage A+B1 already
  // failed, so unplaced is small (1-3 customers). Still safe.
  if (!unplaced.empty()) {
    std::vector<int> stillUnplaced;
    for (int xId : unplaced) {
      bool placed = false;
      for (int r = 0; r < (int)workRoutes.size() && !placed; ++r) {
        // Build top-5 Y candidates sorted by removal savings (most savings
        // first = easier to remove without hurting feasibility)
        struct YCand {
          int yId, yPos;
          double savings;
        };
        std::vector<YCand> yCands;
        const auto &rnodes = workRoutes[r].getNodes();
        for (int i = 1; i < (int)rnodes.size() - 1; ++i) {
          int yId = rnodes[i];
          if (nodeTypeById_[yId] != NodeType::CUSTOMER)
            continue;
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
          int yId = yCands[yi].yId;
          int yPos = yCands[yi].yPos;

          // Eject Y từ r, thử insert X vào r
          std::vector<Route> copy1 = workRoutes;
          copy1[r].removeNode(yPos);
          copy1[r].evaluate();

          // Insert X vào r
          bool xInserted = false;
          for (size_t pos = 1; pos < copy1[r].size() && !xInserted; ++pos) {
            if (!copy1[r].canPossiblyInsert(xId, pos))
              continue;
            InsertionResult res = copy1[r].checkInsertionCost(xId, pos);
            if (!res.isFeasible)
              continue;
            copy1[r].addNode(xId, pos);
            copy1[r].evaluate();
            if (!copy1[r].isFeasible()) {
              copy1[r].removeNode(pos);
              continue;
            }
            xInserted = true;
          }
          if (!xInserted)
            continue;

          // Thử place Y vào routes khác — depth-1 eject nếu cần
          for (int r2 = 0; r2 < (int)copy1.size() && !placed; ++r2) {
            if (r2 == r)
              continue;
            // Direct first
            PlaceResult yp = findDirect(yId, copy1);
            if (yp.r != -1) {
              copy1[yp.r].addNode(yId, yp.pos);
              copy1[yp.r].evaluate();
              if (copy1[yp.r].isFeasible()) {
                bool allOk = true;
                for (auto &wr : copy1)
                  if (!wr.isFeasible()) {
                    allOk = false;
                    break;
                  }
                if (allOk) {
                  workRoutes = copy1;
                  placed = true;
                }
              }
            }
            // depth-2: eject Z từ r2 (top-5 only), insert Y, place Z
            if (!placed) {
              const auto &r2nodes = copy1[r2].getNodes();
              // Cap Z candidates to top-5 by removal savings
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
                                zCands.end(), [](const ZCand &a, const ZCand &b){
                                  return a.savings > b.savings; });

              for (int zi = 0; zi < maxZ && !placed; ++zi) {
                int zId = zCands[zi].zId;
                int zPos = zCands[zi].zPos;
                std::vector<Route> copy2 = copy1;
                copy2[r2].removeNode(zPos);
                copy2[r2].evaluate();
                // Insert Y vào r2
                for (size_t pos2 = 1; pos2 < copy2[r2].size(); ++pos2) {
                  if (!copy2[r2].canPossiblyInsert(yId, pos2))
                    continue;
                  InsertionResult ry = copy2[r2].checkInsertionCost(yId, pos2);
                  if (!ry.isFeasible)
                    continue;
                  copy2[r2].addNode(yId, pos2);
                  copy2[r2].evaluate();
                  if (!copy2[r2].isFeasible())
                    continue;
                  // Place Z anywhere
                  PlaceResult zp = findDirect(zId, copy2);
                  if (zp.r == -1) {
                    applyStation(zId, copy2);
                  } else {
                    copy2[zp.r].addNode(zId, zp.pos);
                    copy2[zp.r].evaluate();
                  }
                  bool allOk = true;
                  for (auto &wr : copy2) {
                    wr.evaluate();
                    if (!wr.isFeasible()) {
                      allOk = false;
                      break;
                    }
                  }
                  if (allOk) {
                    workRoutes = copy2;
                    placed = true;
                    break;
                  }
                }
              }
            }
          }
        }
      }
      if (!placed)
        stillUnplaced.push_back(xId);
    }
    unplaced = stillUnplaced;
  }

  // ── Stage C: Station-assisted cho unplaced còn lại ───────────────────────
  if (!unplaced.empty()) {
    std::vector<int> finalUnplaced;
    for (int cId : unplaced) {
      if (!applyStation(cId, workRoutes))
        finalUnplaced.push_back(cId);
    }
    unplaced = finalUnplaced;
  }

  // Chỉ commit khi TẤT CẢ customers trong pool đã được placed
  if (!unplaced.empty())
    return false;

  for (auto &wr : workRoutes) {
    wr.evaluate();
    if (!wr.isFeasible())
      return false;
  }

  // Commit
  while (solution.getNumRoutes() > 0)
    solution.removeRoute(0);
  for (auto &wr : workRoutes)
    if (!wr.getCustomers().empty())
      solution.addRoute(wr);
  solution.evaluateRoutes();
  return true;
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

  // ⭐ NOTE: route phải đã được evaluated bởi caller trước khi gọi hàm này.
  // Không gọi route.evaluate() ở đây vì route là const ref — vi phạm const-correctness.
  const auto &states = route.getStates();

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

// ============================================================================
// Electricity-Free Vehicle Reduction
//
// Ý tưởng (Cách B — VRP-TW Shadow):
//   1. Chọn 2 routes nhỏ nhất/gần nhau để merge
//   2. Strip tất cả stations ra khỏi cả 2 routes → làm việc trong VRP-TW space
//   3. Thử merge toàn bộ customers vào 1 hoặc 2 routes (chỉ check capacity+TW)
//   4. Nếu merge thành công → chèn stations tối ưu (greedy nearest)
//   5. Nếu feasible với battery → accept, gọi removeRedundantStations
//
// Điểm khác biệt với runSmartMultiRouteMerge:
//   - Bước 3 KHÔNG check battery → tìm được merges mà bản gốc bỏ lỡ
//   - Bước 4 chèn stations SAU khi biết customer order → tối ưu hơn
// ============================================================================
bool LocalSearch::runElectricityFreeVehicleReduction(Solution &solution) {
  auto &routes = solution.getRoutes();
  int numRoutes = (int)routes.size();
  if (numRoutes < 2) return false;

  const double EPSILON = 1e-9;
  double batteryCapacity = routes[0].getVehicle()->getBatteryCapacity();
  double energyRate      = routes[0].getVehicle()->getEnergyConsumptionRate();
  double vehicleCapacity = routes[0].getVehicle()->getCapacity();

  // --- BƯỚC 1: Chọn cặp routes để merge (ưu tiên nhỏ + gần nhau) ---
  int r1_idx = -1, r2_idx = -1;
  double bestScore = -1e9;

  for (int i = 0; i < numRoutes; ++i) {
    for (int j = i + 1; j < numRoutes; ++j) {
      int s1 = (int)routes[i].getCustomers().size();
      int s2 = (int)routes[j].getCustomers().size();
      int sizeSum = s1 + s2;
      if (sizeSum == 0 || sizeSum > 30) continue;

      double dist = calculateEuclideanDistance(computeCentroid(routes[i]),
                                               computeCentroid(routes[j]));
      double score = (1000.0 - sizeSum) - dist;
      if (score > bestScore) {
        bestScore = score;
        r1_idx = i;
        r2_idx = j;
      }
    }
  }
  if (r1_idx == -1) return false;

  // --- BƯỚC 2: Strip stations — lấy chỉ customers ---
  std::vector<int> pool1 = routes[r1_idx].getCustomers();
  std::vector<int> pool2 = routes[r2_idx].getCustomers();
  std::vector<int> allCustomers;
  allCustomers.insert(allCustomers.end(), pool1.begin(), pool1.end());
  allCustomers.insert(allCustomers.end(), pool2.begin(), pool2.end());

  double oldDist = routes[r1_idx].getTotalDistance()
                 + routes[r2_idx].getTotalDistance();

  // Capture tất cả member variables cần thiết bằng [this, &...]
  // để lambda có thể gọi instance, stationIds, removeRedundantStations

  // Helper: build VRP-TW route từ customer list (ignore battery)
  // FIX: Dùng "earliest due date" tie-break thay vì pure nearest-distance
  // để tránh trap: chọn customer xa deadline → cuối sequence không về depot được
  auto buildVRPTWRoute = [this, &EPSILON, vehicleCapacity](
      const std::vector<int> &customers) -> std::vector<int>
  {
    std::vector<int> seq;
    std::vector<bool> visited(customers.size(), false);
    double currentTime = instance->getNodeById(0)->getReadyTime();
    double currentLoad = 0.0;
    int currentNode    = 0;
    const int depotId  = 0;

    while (seq.size() < customers.size()) {
      int    bestNext    = -1;
      double bestScore   = 1e18;
      int    bestIdx     = -1;

      for (size_t k = 0; k < customers.size(); ++k) {
        if (visited[k]) continue;
        int cid   = customers[k];
        auto cust = std::static_pointer_cast<Customer>(
                        instance->getNodeById(cid));

        if (currentLoad + cust->getDemand() > vehicleCapacity + EPSILON) continue;

        double travelTime = instance->getTime(currentNode, cid);
        double arrival    = currentTime + travelTime;
        double startTime  = arrival > cust->getReadyTime()
                            ? arrival : cust->getReadyTime();
        if (startTime > cust->getDueDate() + EPSILON) continue;

        // Check: sau khi thăm cid, còn về depot được không?
        double depTime    = startTime + cust->getServiceTime();
        double tToDepot   = instance->getTime(cid, depotId);
        auto   depot      = instance->getNodeById(depotId);
        if (depTime + tToDepot > depot->getDueDate() + EPSILON) continue;

        // Score = distance (primary) + normalized urgency (secondary tie-break)
        double dist  = instance->getDistance(currentNode, cid);
        double slack = cust->getDueDate() - startTime; // slack nhỏ = urgent
        // Ưu tiên gần + urgent (slack nhỏ) — tránh để lại customers có TW chặt
        double score = dist * 0.8 + slack * 0.2;

        if (score < bestScore) {
          bestScore = score;
          bestNext  = cid;
          bestIdx   = (int)k;
        }
      }

      if (bestNext == -1) break;

      visited[bestIdx] = true;
      seq.push_back(bestNext);

      auto cust2     = std::static_pointer_cast<Customer>(
                           instance->getNodeById(bestNext));
      double travel2 = instance->getTime(currentNode, bestNext);
      double arr2    = currentTime + travel2;
      double start2  = arr2 > cust2->getReadyTime() ? arr2 : cust2->getReadyTime();
      currentTime    = start2 + cust2->getServiceTime();
      currentLoad   += cust2->getDemand();
      currentNode    = bestNext;
    }
    return seq;
  };

  // Helper: insert stations vào customer sequence để restore battery feasibility
  // FIX: Track actual time để check TW feasibility sau khi chèn station
  auto insertStationsGreedy = [this, &EPSILON, batteryCapacity, energyRate,
                                &routes, r1_idx](
      const std::vector<int> &custSeq) -> Route
  {
    auto vehicle = routes[r1_idx].getVehicle();
    Route r(0, vehicle, instance);

    std::vector<int> fullSeq = {0};
    fullSeq.insert(fullSeq.end(), custSeq.begin(), custSeq.end());
    fullSeq.push_back(0);

    double battery     = batteryCapacity;
    double currentTime = instance->getNodeById(0)->getReadyTime();
    std::vector<int> built = {0};

    for (size_t k = 1; k < fullSeq.size(); ++k) {
      int from       = built.back();
      int to         = fullSeq[k];
      double eNeeded = instance->getDistance(from, to) * energyRate;

      if (battery < eNeeded - EPSILON) {
        // Tìm station: ít detour, EV đến được, và KHÔNG vi phạm TW của 'to'
        int    bestSt      = -1;
        double bestDetour  = 1e18;

        for (int sid : stationIds) {
          double eSt = instance->getDistance(from, sid) * energyRate;
          if (eSt > battery + EPSILON) continue; // Không đến được station

          // Tính charge amount tối thiểu để đến được 'to'
          double eStToTo   = instance->getDistance(sid, to) * energyRate;
          double afterSt   = batteryCapacity - eSt; // sạc đủ để đến to
          if (afterSt < eStToTo - EPSILON) continue; // Station quá xa 'to'

          // Check TW: thời gian đến 'to' sau khi đi qua station có hợp lệ không?
          double tSt       = instance->getTime(from, sid);
          double arrSt     = currentTime + tSt;
          // Charge amount = đủ để đến to (không sạc full để tiết kiệm thời gian)
          double chargeAmt = eStToTo - (battery - eSt);
          if (chargeAmt < 0) chargeAmt = 0;
          double chargeTime = chargeAmt / instance->getVehicleEnergyRate();
          double depSt      = arrSt + chargeTime;
          double tStToTo    = instance->getTime(sid, to);
          double arrTo      = depSt + tStToTo;

          // Check TW của 'to' (chỉ với customers, depot không có TW strict)
          auto toNode = instance->getNodeById(to);
          if (toNode->getType() == NodeType::CUSTOMER) {
            if (arrTo > toNode->getDueDate() + EPSILON) continue;
          }

          double detour = instance->getDistance(from, sid)
                        + instance->getDistance(sid, to)
                        - instance->getDistance(from, to);
          if (detour < bestDetour) {
            bestDetour = detour;
            bestSt     = sid;
          }
        }

        if (bestSt != -1) {
          double eSt        = instance->getDistance(from, bestSt) * energyRate;
          double tSt        = instance->getTime(from, bestSt);
          double eStToTo    = instance->getDistance(bestSt, to) * energyRate;
          double battAfterTravel = battery - eSt;
          double chargeAmt  = eStToTo - battAfterTravel;
          if (chargeAmt < 0) chargeAmt = 0;
          double chargeTime = chargeAmt / instance->getVehicleEnergyRate();

          built.push_back(bestSt);
          battery      = battAfterTravel + chargeAmt; // thực tế, không full
          currentTime += tSt + chargeTime;
        }
        // Recalc sau khi đã (có thể) ghé station
        from    = built.back();
        eNeeded = instance->getDistance(from, to) * energyRate;
      }

      // Advance time đến 'to'
      double tFromTo  = instance->getTime(from, to);
      double arrTo    = currentTime + tFromTo;
      auto   toNode2  = instance->getNodeById(to);
      if (toNode2->getType() == NodeType::CUSTOMER) {
        auto cust = std::static_pointer_cast<Customer>(toNode2);
        double startTo = arrTo > cust->getReadyTime() ? arrTo : cust->getReadyTime();
        currentTime    = startTo + cust->getServiceTime();
      } else {
        currentTime = arrTo; // depot
      }

      built.push_back(to);
      battery -= eNeeded;
      if (battery < -EPSILON) battery = 0.0;
    }

    for (size_t k = 1; k + 1 < built.size(); ++k) {
      r.addNode(built[k], k);
    }
    r.evaluate();
    return r;
  };

  // --- THỬ MERGE VÀO 1 ROUTE ---
  {
    std::vector<int> mergedSeq = buildVRPTWRoute(allCustomers);

    if (mergedSeq.size() == allCustomers.size()) {
      Route merged = insertStationsGreedy(mergedSeq);
      merged.evaluate();

      if (merged.isFeasible()) {
        removeRedundantStations(merged);
        merged.evaluate();

        if (merged.isFeasible()) {
          solution.removeRoute(std::max(r1_idx, r2_idx));
          solution.removeRoute(std::min(r1_idx, r2_idx));
          solution.addRoute(merged);
          return true;
        }
      }
    }
  }

  // --- THỬ PHÂN PHỐI LẠI VÀO 2 ROUTES (cải thiện distance) ---
  {
    int    seed1 = -1, seed2 = -1;
    double maxD  = -1.0;
    for (size_t i = 0; i < allCustomers.size(); ++i) {
      for (size_t j = i + 1; j < allCustomers.size(); ++j) {
        double d = instance->getDistance(allCustomers[i], allCustomers[j]);
        if (d > maxD) {
          maxD  = d;
          seed1 = allCustomers[i];
          seed2 = allCustomers[j];
        }
      }
    }
    if (seed1 == -1) return false;

    std::vector<int> group1 = {seed1}, group2 = {seed2};
    for (int cid : allCustomers) {
      if (cid == seed1 || cid == seed2) continue;
      double d1 = instance->getDistance(seed1, cid);
      double d2 = instance->getDistance(seed2, cid);
      if (d1 <= d2) group1.push_back(cid);
      else          group2.push_back(cid);
    }

    std::vector<int> seq1 = buildVRPTWRoute(group1);
    std::vector<int> seq2 = buildVRPTWRoute(group2);

    if (seq1.size() == group1.size() && seq2.size() == group2.size()) {
      Route nr1 = insertStationsGreedy(seq1);
      Route nr2 = insertStationsGreedy(seq2);
      nr1.evaluate();
      nr2.evaluate();

      if (nr1.isFeasible() && nr2.isFeasible()) {
        removeRedundantStations(nr1);
        removeRedundantStations(nr2);
        nr1.evaluate();
        nr2.evaluate();

        if (nr1.isFeasible() && nr2.isFeasible()) {
          double newDist = nr1.getTotalDistance() + nr2.getTotalDistance();
          if (newDist < oldDist * 0.997) {
            solution.removeRoute(std::max(r1_idx, r2_idx));
            solution.removeRoute(std::min(r1_idx, r2_idx));
            solution.addRoute(nr1);
            solution.addRoute(nr2);
            return true;
          }
        }
      }
    }
  }

  return false;
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
      if (sizeSum > 30 || sizeSum == 0)
        continue; // Giảm từ 35 xuống 30 để tránh merge quá lớn (tốn CPU)

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

  // 3.1. Seed Selection (🌟 FIX THÀNH FURTHEST-APART SEEDING)
  // Chọn 2 khách hàng cách xa nhau nhất trong pool làm seed cho 2 route mới
  int seed1 = -1, seed2 = -1;
  double maxDist = -1.0;
  for (size_t i = 0; i < customer_pool.size(); ++i) {
    for (size_t j = i + 1; j < customer_pool.size(); ++j) {
      double d = instance->getDistance(customer_pool[i], customer_pool[j]);
      if (d > maxDist) {
        maxDist = d;
        seed1 = customer_pool[i];
        seed2 = customer_pool[j];
      }
    }
  }

  if (seed1 != -1 && seed2 != -1) {
    newRoutes[0].addNode(seed1, 1);
    newRoutes[1].addNode(seed2, 1);
    customer_pool.erase(
        std::remove(customer_pool.begin(), customer_pool.end(), seed1),
        customer_pool.end());
    customer_pool.erase(
        std::remove(customer_pool.begin(), customer_pool.end(), seed2),
        customer_pool.end());
  } else if (!customer_pool.empty()) {
    newRoutes[0].addNode(customer_pool[0], 1);
    customer_pool.erase(customer_pool.begin());
  }

  // 3.2. Assignment Loop — Station-Aware
  // Với mỗi vòng: tìm globally best (customer, route, position, option)
  // option A = direct, B = [stat,cust], C = [cust,stat]
  bool construction_failed = false;

  struct MergeCandidate {
    int custId = -1;
    int routeIdx = -1;
    size_t pos = 0;
    double cost = 1e18;
    int stationId = -1;
    bool statBefore = true;
  };

  while (!customer_pool.empty() && !construction_failed) {
    MergeCandidate globalBest;

    for (int cust_id : customer_pool) {
      double demand = instance->getNodeById(cust_id)->getDemand();
      int nearStat = findNearestStation(cust_id);

      for (int r_idx = 0; r_idx < 2; ++r_idx) {
        if (newRoutes[r_idx].getTotalDemand() + demand >
            newRoutes[r_idx].getVehicle()->getCapacity())
          continue;

        for (size_t pos = 1; pos < newRoutes[r_idx].size(); ++pos) {
          // Option A: direct
          InsertionResult res =
              newRoutes[r_idx].checkInsertionCost(cust_id, pos);
          if (res.isFeasible && res.deltaDistance < globalBest.cost) {
            globalBest = {cust_id, r_idx, pos, res.deltaDistance, -1, true};
          }

          // Options B & C: station-assisted (only if battery likely limiting)
          if (nearStat != -1) {
            // B: [stat, cust]
            {
              Route copy = newRoutes[r_idx];
              copy.addNode(cust_id, pos);
              copy.addNode(nearStat, pos);
              copy.evaluate();
              if (copy.isFeasible()) {
                double c = copy.getTotalDistance() -
                           newRoutes[r_idx].getTotalDistance();
                if (c < globalBest.cost)
                  globalBest = {cust_id, r_idx, pos, c, nearStat, true};
              }
            }
            // C: [cust, stat]
            {
              Route copy2 = newRoutes[r_idx];
              copy2.addNode(cust_id, pos);
              copy2.addNode(nearStat, pos + 1);
              copy2.evaluate();
              if (copy2.isFeasible()) {
                double c = copy2.getTotalDistance() -
                           newRoutes[r_idx].getTotalDistance();
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

    // Apply best candidate
    if (globalBest.stationId == -1) {
      newRoutes[globalBest.routeIdx].addNode(globalBest.custId, globalBest.pos);
    } else if (globalBest.statBefore) {
      newRoutes[globalBest.routeIdx].addNode(globalBest.custId, globalBest.pos);
      newRoutes[globalBest.routeIdx].addNode(globalBest.stationId,
                                             globalBest.pos);
    } else {
      newRoutes[globalBest.routeIdx].addNode(globalBest.custId, globalBest.pos);
      newRoutes[globalBest.routeIdx].addNode(globalBest.stationId,
                                             globalBest.pos + 1);
    }
    newRoutes[globalBest.routeIdx].evaluate();

    customer_pool.erase(std::remove(customer_pool.begin(), customer_pool.end(),
                                    globalBest.custId),
                        customer_pool.end());
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

  if (finalRoutes.empty()) {
    return false; // Phải có ít nhất 1 route hợp lệ để tiếp tục
  }

  if (finalRoutes.size() < 2 ||
      (finalRoutes.size() == 2 && newTotalDist < oldTotalDist * 0.997)) {
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
  // ⭐ FIX PERF: So sánh bình phương - không cần sqrt/pow
  double dx = c1.x - c2.x;
  double dy = c1.y - c2.y;
  return (dx * dx + dy * dy) < (threshold * threshold);
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

  // ⭐ Cache init: allocate tracking structures for node removal rankings
  searchContext_.removalRankings.assign(numRoutes, {});
  searchContext_.rankingDirty.assign(numRoutes, true); // initially all dirty

  searchContext_.isValid = true;
}

const std::vector<std::pair<int, double>> &
LocalSearch::getCachedRanking(int routeIdx, const Route &route,
                              SearchContext &ctx) {
  if (ctx.rankingDirty[routeIdx]) {
    ctx.removalRankings[routeIdx] = rankNodesByRemovalSavings(route);
    ctx.rankingDirty[routeIdx] = false;
  }
  return ctx.removalRankings[routeIdx];
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

  // ⭐ SMD: Dùng cached removal savings nếu đã được tính sẵn
  // removalSavings > 0 → removal giảm distance đi một lượng removalSavings
  // → contribution của removal vào distanceDelta = -removalSavings
  if (move.cachedRemovalSavings != 0.0) {
    distanceDelta -=
        move.cachedRemovalSavings; // Tương đương -=
                                   // (d(prev,node)+d(node,next)-d(prev,next))
  } else {
    int r1_prev = r1.getNodeAt(move.nodeIdx1 - 1);
    int r1_next = r1.getNodeAt(move.nodeIdx1 + 1);
    distanceDelta -= (instance->getDistance(r1_prev, nodeId) +
                      instance->getDistance(nodeId, r1_next));
    distanceDelta += instance->getDistance(r1_prev, r1_next);
  }
  // Insertion cost bên dưới LUÔN chạy, bất kể dùng cache hay không

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
    if (nodeTypeById_[nodes[i]] == NodeType::STATION) {
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

void LocalSearch::preprocessTWNext() {
  int maxId = 0;
  for (const auto &node : instance->getNodes()) {
    if (node->getId() > maxId) {
      maxId = node->getId();
    }
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