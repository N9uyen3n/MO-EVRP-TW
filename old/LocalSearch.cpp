// =============================================================================
// LocalSearch.cpp — Optimized
// =============================================================================
// OPTIMIZATIONS APPLIED (vs original):
//
// [OPT-1] demandById_  — Flat O(1) demand lookup (like nodeTypeById_ for type).
//         Eliminates getNodeById()->getDemand() calls inside hot inner loops.
//         *** Requires adding `std::vector<double> demandById_;` to
//         LocalSearch.h ***
//
// [OPT-2] nodeTypeById_ usage completed — All remaining
// getNodeById()->getType()
//         calls in searchSwap, searchCrossExchange, repositionStations,
//         searchStationSwap, removeRedundantStations replaced with O(1) array
//         lookup.
//
// [OPT-3] searchInterTwoOpt — Route splice via direct vector construction
//         instead of repeated O(N) removeNode/addNode calls. O(N²) → O(N).
//
// [OPT-4] customer_pool erase — swap-and-pop O(1) instead of erase+remove O(N).
//         Applied in tryEliminateSmallestRoute and ejectionChain.
//
// [OPT-5] findBestInsertionPositions_KNN — unordered_set<int> for O(1) neighbor
//         lookup, vector<bool> for O(1) position dedup. Was O(n·K) with
//         std::set.
//
// [OPT-6] thread_local candidate buffers — reuse heap allocation across calls
//         in findBestInsertionPositions_TimeAware and
//         getTopKInsertionPositions.
//
// [OPT-7] calculateEuclideanDistance — std::hypot replaces sqrt(pow+pow).
//
// [OPT-8] getDemand() double-call in ejectionChain::findDirect removed (one
// call).
//
// =============================================================================

#include "../../include/alns/LocalSearch.h"
#include "../../include/core/Customer.h"
#include "../../include/core/Station.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
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

  // [OPT-1] Precompute flat demand cache — eliminates
  // getNodeById()->getDemand() in hot inner loops (searchCrossExchange,
  // ejectionChain, etc.)
  demandById_.assign(maxNodeId + 1, 0.0);
  for (const auto &cust : instance->getCustomers()) {
    demandById_[cust->getId()] = cust->getDemand();
  }
}

// ============================================================================
// Algorithm: 4-Phase Hybrid Local Search
// ===========================================================================
int LocalSearch::getVehicleReductionFeq() {
  return this->vehicleReductionFreq;
}

void LocalSearch::setVehicleReductionFeq(int number) {
  this->vehicleReductionFreq = number;
}



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
  double t1 = 0, t2 = 0, t3 = 0; // tạm thời
  for (int iter = 0; iter < MAX_LS_ITERATIONS; ++iter) {
    bool improved = false;

    updateSearchContext(solution);
    // auto tp1 = std::chrono::high_resolution_clock::now();
    // --- Phase 1: Distance Optimization (VND) ---
    bool phase1Improved = runDistanceOptimization(solution);
    // auto tp2 = std::chrono::high_resolution_clock::now();
    // t1 += std::chrono::duration<double>(tp2-tp1).count();
    if (phase1Improved) {
      improved = true;
      // searchContext_.invalidate();
    }

    // --- Phase 2: Charging Optimization (periodic) ---
    if (iter % CHARGING_FREQUENCY == 0) {
      // auto tp3 = std::chrono::high_resolution_clock::now();
      if (runChargingOptimization(solution)) {
        improved = true;
        searchContext_.invalidate();
      }
      // auto tp4 = std::chrono::high_resolution_clock::now();
      // t2 += std::chrono::duration<double>(tp4-tp3).count();
    }

    // --- Phase 3: Vehicle Reduction (every iter, no throttle) ---
    // CHỈ chạy khi Phase 1 vừa improve, HOẶC mỗi 2 iter làm fallback
    if (solution.getRoutes().size() > 1) {
      if (phase1Improved || iter % vehicleReductionFreq == 0) {
        // auto tp5_clock = std::chrono::high_resolution_clock::now();
        if (runVehicleReduction(solution)) {
          improved = true;
          noImprovementCount_ = 0;
          searchContext_.invalidate();
        }
        // auto tp6_clock = std::chrono::high_resolution_clock::now();
        // t3 += std::chrono::duration<double>(tp6_clock - tp5_clock).count();
      }
    }
    // printf("Phase1=%.3fs Phase2=%.3fs Phase3=%.3fs\n", t1, t2, t3);

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
    // FIX đúng: ánh xạ post-removal → pre-removal index
    int pre_prev =
        (insertPos - 1 < move.nodeIdx1) ? (insertPos - 1) : insertPos;
    int pre_next = (insertPos < move.nodeIdx1) ? insertPos : (insertPos + 1);
    int ins_prev = nodes[pre_prev];
    int ins_next = nodes[pre_next];
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
      // Xóa index lớn trước để không làm lệch index nhỏ
      r1_copy.removeNode(move.nodeIdx2);
      r1_copy.addNode(nodeId1, move.nodeIdx2);
      r1_copy.removeNode(move.nodeIdx1);
      r1_copy.addNode(nodeId2, move.nodeIdx1);
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
  updateSearchContext(solution);

  int k = 0;
  while (k < 7) {
    bool improved = false;

    switch (k) {
    case 0:
      improved = searchRelocate(solution, weights, searchContext_);
      break;
    case 1:
      improved = searchSwap(solution, weights, searchContext_);
      break;
    case 2:
      improved = searchOrOpt(solution, weights);
      break;
    case 3:
      improved = searchTwoOpt(solution, weights);
      break;
    case 4:
      improved = searchInterTwoOpt(solution, weights, searchContext_);
      break;
    case 5:
      improved = searchCrossExchange(solution, weights, searchContext_);
      break;
    case 6:
      improved = searchIntraOrOpt(solution, weights, searchContext_);
      break;
      // case 7:
      //   improved = searchOrOptReversed(solution, weights, searchContext_);
      //   break;
    }

    if (improved) {
      anyImproved = true;

      // [SMD-2] KHÔNG invalidate toàn bộ context.
      // applyMove() đã gọi searchContext_.markDirty(r1, r2) — chỉ 2 routes bị
      // dirty. Chỉ cần patch lại centroids + neighborLists của 2 routes đó, giữ
      // nguyên phần còn lại
      patchSearchContext(solution);

      k = 0; // VND restart, nhưng context vẫn còn hợp lệ
    } else {
      k++;
    }
  }

  return anyImproved;
}

// [SMD] Partial context update: chỉ rebuild centroids + neighborLists
// cho các routes bị markDirty, thay vì invalidate() toàn bộ.
// Phức tạp O(dirty_routes × R) thay vì O(R²).
void LocalSearch::patchSearchContext(const Solution &solution) {
  if (!searchContext_.isValid) {
    updateSearchContext(solution);
    return;
  }

  const auto &routes = solution.getRoutes();
  int numRoutes = (int)routes.size();

  // Nếu số routes thay đổi → phải full rebuild
  if (numRoutes != (int)searchContext_.centroids.size()) {
    searchContext_.invalidate();
    updateSearchContext(solution);
    return;
  }

  static constexpr int MAX_ROUTE_NEIGHBORS = 8;

  // Pass 1: recompute centroid cho routes bị dirty
  for (int r = 0; r < numRoutes; ++r) {
    if (!searchContext_.centroidDirty[r])
      continue;
    searchContext_.centroids[r] = computeCentroid(routes[r]);
    // KHÔNG reset centroidDirty[r] ở đây — cần dùng ở Pass 2
  }

  // Pass 2: rebuild neighborList cho:
  //   (a) routes bị dirty (centroid của chính nó thay đổi)
  //   (b) routes clean mà có dirty route trong neighborList cũ
  for (int r = 0; r < numRoutes; ++r) {
    bool needRebuild = searchContext_.centroidDirty[r];

    if (!needRebuild) {
      // Kiểm tra xem neighborList hiện tại có chứa route dirty không
      for (int nb : searchContext_.neighborLists[r]) {
        if (nb != r && nb < numRoutes && searchContext_.centroidDirty[nb]) {
          needRebuild = true;
          break;
        }
      }
    }

    if (!needRebuild)
      continue;

    searchContext_.neighborLists[r].clear();
    searchContext_.neighborLists[r].push_back(r);

    struct RN {
      int idx;
      double dist2;
    };
    std::vector<RN> ranked;
    ranked.reserve(numRoutes - 1);
    for (int r2 = 0; r2 < numRoutes; ++r2) {
      if (r2 == r)
        continue;
      double dx =
          searchContext_.centroids[r].x - searchContext_.centroids[r2].x;
      double dy =
          searchContext_.centroids[r].y - searchContext_.centroids[r2].y;
      ranked.push_back({r2, dx * dx + dy * dy});
    }
    int keep = std::min(MAX_ROUTE_NEIGHBORS, (int)ranked.size());
    std::partial_sort(
        ranked.begin(), ranked.begin() + keep, ranked.end(),
        [](const RN &a, const RN &b) { return a.dist2 < b.dist2; });
    for (int i = 0; i < keep; ++i)
      searchContext_.neighborLists[r].push_back(ranked[i].idx);
  }

  // Pass 3: reset centroidDirty sau khi đã patch xong
  std::fill(searchContext_.centroidDirty.begin(),
            searchContext_.centroidDirty.end(), false);

  // rankingDirty giữ nguyên → getCachedRanking() lazy-rebuild khi operator cần
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

  const auto &centroids = ctx.centroids;
  const auto &neighborLists = ctx.neighborLists;

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

          // [PRUNE-1] twNext_ O(1) before canPossiblyInsert
          {
            int prev2 = routes[r2].getNodeAt((int)j - 1);
            int next2 = routes[r2].getNodeAt((int)j);
            if (!twNext_[prev2][nodeId] || !twNext_[nodeId][next2])
              continue;
          }

          if (!routes[r2].canPossiblyInsert(nodeId, j,
                                            (r1 == r2) ? nodeId : -1)) {
            continue;
          }

          move.reset();
          move.type =
              (r1 == r2) ? MoveType::INTRA_RELOCATE : MoveType::INTER_RELOCATE;
          move.routeIdx1 = r1;
          move.nodeIdx1 = i;
          move.routeIdx2 = r2;
          move.nodeIdx2 = j;
          move.cachedRemovalSavings = removalSavings;

          MoveEvaluation preEval = evaluateRelocateDelta(solution, move);

          if (!preEval.isFeasible || preEval.distanceDelta >= -1e-9) {
            continue;
          }

          evaluateMove(solution, move, weights);

          if (move.eval.isFeasible && move.eval.objectiveDelta < -1e-9) {
            applyMove(solution, move);
            return true;
          } else if (!move.eval.isFeasible && preEval.distanceDelta < -1.0) {
            // [FIX-2] Relaxed from -5.0 → -1.0
            auto &routes = solution.getRoutes();
            Route testRouteFrom = routes[r1];
            Route testRouteTo = routes[r2];

            if (tryRelocateWithEnergyBoost(testRouteFrom, i, testRouteTo, j,
                                           30.0)) {
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

// bool LocalSearch::searchTwoOpt(Solution &solution,
//                                const LocalSearchWeights &weights) {
//     MoveDescriptor move; // [CHANGE-5] local — was member move
//     auto &routes = solution.getRoutes();
//
//     for (int r = 0; r < (int) routes.size(); ++r) {
//         const auto &nodes = routes[r].getNodes();
//         int n = nodes.size();
//
//         if (n < 4)
//             continue;
//
//         for (int i = 1; i < n - 2; ++i) {
//             for (int j = i + 1; j < n - 1; ++j) {
//                 int prev = nodes[i - 1];
//                 int nodeI = nodes[i];
//                 int nodeJ = nodes[j];
//                 int next = nodes[j + 1];
//
//                 double oldDist = instance->getDistance(prev, nodeI) +
//                                  instance->getDistance(nodeJ, next);
//                 double newDist = instance->getDistance(prev, nodeJ) +
//                                  instance->getDistance(nodeI, next);
//
//                 if (newDist >= oldDist - 1e-6)
//                     continue;
//
//                 // ⭐ Granular Filter
//                 if (instance->getDistance(prev, nodeJ) >= distanceThreshold_
//                 &&
//                     instance->getDistance(nodeI, next) >= distanceThreshold_)
//                     { continue;
//                 }
//
//                 move.reset();
//                 move.type = MoveType::INTRA_TWO_OPT;
//                 move.routeIdx1 = r;
//                 move.nodeIdx1 = i;
//                 move.routeIdx2 = r;
//                 move.nodeIdx2 = j; {
//                     bool segHasStation = false;
//                     for (int k = i; k <= j && !segHasStation; ++k)
//                         if (nodeTypeById_[nodes[k]] == NodeType::STATION)
//                             segHasStation = true;
//                     move.stationId = segHasStation ? 1 : 0;
//                 }
//
//                 evaluateMove(solution, move, weights);
//
//                 if (move.eval.isFeasible &&
//                     move.eval.objectiveDelta < -1e-9) {
//                     applyMove(solution, move);
//                     return true;
//                 }
//             }
//         }
//     }
//
//     return false;
// }

bool LocalSearch::searchTwoOpt(Solution &solution,
                               const LocalSearchWeights &weights) {
  MoveDescriptor move;
  auto &routes = solution.getRoutes();
  const int maxId = (int)nodeTypeById_.size();

  // [OPT] Reusable posInRoute — tái dụng qua các routes
  std::vector<int> posInRoute(maxId, -1);

  for (int r = 0; r < (int)routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();
    int n = (int)nodes.size();
    if (n < 4)
      continue;

    // [OPT-A] Build posInRoute O(N) once per route
    for (int k = 0; k < n; ++k)
      posInRoute[nodes[k]] = k;

    for (int i = 1; i < n - 2; ++i) {
      int nodeI = nodes[i];
      int prev = nodes[i - 1];
      double d_prev_I = instance->getDistance(prev, nodeI); // cache

      // [OPT-B] Chỉ xét j dựa trên KNN neighbors của nodeI
      auto knnIt = knnCache_.find(nodeI);
      if (knnIt == knnCache_.end())
        continue;

      // Sử dụng thread_local để tránh cấp phát bộ nhớ lặp lại
      static thread_local std::vector<int> jCandidates;
      jCandidates.clear();
      static thread_local std::vector<bool> jSeen;
      jSeen.assign(n, false);

      for (int neighbor : knnIt->second) {
        int p = posInRoute[neighbor];
        if (p == -1)
          continue; // neighbor không nằm trong route này

        // 2-opt swap (i,j) sẽ nối prev với nodes[j] và nodeI với nodes[j+1].
        // Vì vậy, neighbor có thể là nodes[j] (tức là p = j)
        // hoặc nodes[j+1] (tức là p = j+1 → j = p-1)
        if (p > i && p < n - 1 && !jSeen[p]) {
          jSeen[p] = true;
          jCandidates.push_back(p);
        }
        if (p - 1 > i && p - 1 < n - 1 && !jSeen[p - 1]) {
          jSeen[p - 1] = true;
          jCandidates.push_back(p - 1);
        }
      }

      // Duyệt qua danh sách các j hợp lệ đã được thu thập
      for (int j : jCandidates) {
        int nodeJ = nodes[j];
        int next = nodes[j + 1];

        double oldDist = d_prev_I + instance->getDistance(nodeJ, next);
        double newDist = instance->getDistance(prev, nodeJ) +
                         instance->getDistance(nodeI, next);

        if (newDist >= oldDist - 1e-6)
          continue;

        // Granular filter (giữ nguyên)
        if (instance->getDistance(prev, nodeJ) >= distanceThreshold_ &&
            instance->getDistance(nodeI, next) >= distanceThreshold_)
          continue;

        move.reset();
        move.type = MoveType::INTRA_TWO_OPT;
        move.routeIdx1 = r;
        move.nodeIdx1 = i;
        move.routeIdx2 = r;
        move.nodeIdx2 = j;
        {
          bool segHasStation = false;
          for (int k = i; k <= j && !segHasStation; ++k)
            if (nodeTypeById_[nodes[k]] == NodeType::STATION)
              segHasStation = true;
          move.stationId = segHasStation ? 1 : 0;
        }

        evaluateMove(solution, move, weights);

        if (move.eval.isFeasible && move.eval.objectiveDelta < -1e-9) {
          // Cleanup trước khi return
          for (int k = 0; k < n; ++k)
            posInRoute[nodes[k]] = -1;
          applyMove(solution, move);
          return true;
        }
      }
    }

    // Cleanup posInRoute cho route này
    for (int k = 0; k < n; ++k)
      posInRoute[nodes[k]] = -1;
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

          // [PRUNE-4] Granularity: both new edges must be below threshold
          if (instance->getDistance(nodes1[i], nodes2[j + 1]) >=
                  distanceThreshold_ &&
              instance->getDistance(nodes2[j], nodes1[i + 1]) >=
                  distanceThreshold_)
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

          double oldDist =
              routes[r1].getTotalDistance() + routes[r2].getTotalDistance();
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
  MoveDescriptor move;
  int numRoutes = (int)solution.getRoutes()
                      .size(); // [FIX-1] không cache ref vì applyMove modify

  if (!ctx.isValid || (int)ctx.centroids.size() != numRoutes)
    return false;

  const int maxId = (int)nodeTypeById_.size();

  // ── INTRA-ROUTE SWAP ─────────────────────────────────────────────────────
  // [OPT] KNN pruning: thay O(N²) inner loop bằng O(N·K)
  // posInRoute[nodeId] → index trong route hiện tại, O(1) lookup
  std::vector<int> posInRoute(maxId, -1);

  for (int r = 0; r < numRoutes; ++r) {
    const auto &nodes =
        solution.getRoutes()[r].getNodes(); // re-fetch mỗi route
    int n = (int)nodes.size();
    if (n < 4)
      continue;

    // [OPT-A] Build posInRoute O(N) một lần cho route này
    for (int k = 0; k < n; ++k)
      posInRoute[nodes[k]] = k;

    for (int i = 1; i < n - 2; ++i) {
      if (nodeTypeById_[nodes[i]] != NodeType::CUSTOMER)
        continue;

      int nodeId_i = nodes[i];
      int prev_i = nodes[i - 1];
      int next_i = nodes[i + 1];

      // [OPT-B] Chỉ xét j = KNN neighbors của nodes[i] thay vì tất cả j > i
      for (int nid : knnCache_[nodeId_i]) {
        int j = posInRoute[nid];
        if (j < i + 2 || j >= n - 1)
          continue; // j phải > i+1, không là depot
        if (nodeTypeById_[nodes[j]] != NodeType::CUSTOMER)
          continue;

        int prev_j = nodes[j - 1];
        int next_j = nodes[j + 1];

        double oldDist = instance->getDistance(prev_i, nodeId_i) +
                         instance->getDistance(nodeId_i, next_i) +
                         instance->getDistance(prev_j, nodes[j]) +
                         instance->getDistance(nodes[j], next_j);
        double newDist = instance->getDistance(prev_i, nodes[j]) +
                         instance->getDistance(nodes[j], next_i) +
                         instance->getDistance(prev_j, nodeId_i) +
                         instance->getDistance(nodeId_i, next_j);

        if (newDist >= oldDist - 1e-6)
          continue;

        if (!solution.getRoutes()[r].canPossiblyInsert(nodes[j], i, nodeId_i) ||
            !solution.getRoutes()[r].canPossiblyInsert(nodeId_i, j, nodes[j]))
          continue;

        move.reset();
        move.type = MoveType::INTRA_SWAP;
        move.routeIdx1 = r;
        move.nodeIdx1 = i;
        move.routeIdx2 = r;
        move.nodeIdx2 = j;

        evaluateMove(solution, move, weights);

        if (move.eval.isFeasible && move.eval.objectiveDelta < -1e-9) {
          // Cleanup posInRoute trước khi return
          for (int k = 0; k < n; ++k)
            posInRoute[nodes[k]] = -1;
          applyMove(solution, move);
          return true;
        }
      }
    }

    // Cleanup posInRoute cho route này trước khi qua route tiếp theo
    for (int k = 0; k < n; ++k)
      posInRoute[nodes[k]] = -1;
  }

  // ── INTER-ROUTE SWAP ─────────────────────────────────────────────────────
  for (int r1 = 0; r1 < numRoutes; ++r1) {
    for (int r2 : ctx.neighborLists[r1]) {
      if (r2 <= r1)
        continue;

      // [FIX-2] Re-fetch nodes sau getCachedRanking vì ranking có thể
      // trigger rebuild — lấy ref SAU khi ranking đã stable
      const auto &ranked1 =
          this->getCachedRanking(r1, solution.getRoutes()[r1], ctx);
      const auto &ranked2 =
          this->getCachedRanking(r2, solution.getRoutes()[r2], ctx);

      const auto &nodes1 = solution.getRoutes()[r1].getNodes();
      const auto &nodes2 = solution.getRoutes()[r2].getNodes();

      int attempts1 = std::min(maxSwapAttempts_, (int)ranked1.size());
      int attempts2 = std::min(maxSwapAttempts_, (int)ranked2.size());

      for (int k1 = 0; k1 < attempts1; ++k1) {
        int i = ranked1[k1].first;
        if (nodeTypeById_[nodes1[i]] != NodeType::CUSTOMER)
          continue;

        int nodeId1 = nodes1[i];
        int prev1 = nodes1[i - 1];
        int next1 = nodes1[i + 1];

        // [OPT-C] Cache d(prev1,n1) + d(n1,next1) một lần cho tất cả k2
        double removal1 = instance->getDistance(prev1, nodeId1) +
                          instance->getDistance(nodeId1, next1);

        for (int k2 = 0; k2 < attempts2; ++k2) {
          int j = ranked2[k2].first;
          if (nodeTypeById_[nodes2[j]] != NodeType::CUSTOMER)
            continue;

          int nodeId2 = nodes2[j];

          if (instance->getDistance(nodeId1, nodeId2) >= distanceThreshold_)
            continue;

          int prev2 = nodes2[j - 1];
          int next2 = nodes2[j + 1];

          // [OPT-C] Tái dụng removal1, chỉ tính thêm phần r2
          double oldDist = removal1 + instance->getDistance(prev2, nodeId2) +
                           instance->getDistance(nodeId2, next2);
          double newDist = instance->getDistance(prev1, nodeId2) +
                           instance->getDistance(nodeId2, next1) +
                           instance->getDistance(prev2, nodeId1) +
                           instance->getDistance(nodeId1, next2);

          if (newDist >= oldDist - 1e-9)
            continue;

          if (!twNext_[prev1][nodeId2] || !twNext_[nodeId2][next1] ||
              !twNext_[prev2][nodeId1] || !twNext_[nodeId1][next2])
            continue;

          if (!solution.getRoutes()[r1].canPossiblyInsert(nodeId2, i,
                                                          nodeId1) ||
              !solution.getRoutes()[r2].canPossiblyInsert(nodeId1, j, nodeId2))
            continue;

          move.reset();
          move.type = MoveType::INTER_SWAP;
          move.routeIdx1 = r1;
          move.nodeIdx1 = i;
          move.routeIdx2 = r2;
          move.nodeIdx2 = j;

          evaluateMove(solution, move, weights);

          if (move.eval.isFeasible && move.eval.objectiveDelta < -1e-9) {
            applyMove(solution, move);
            return true;
          }
        }
      }
    }
  }

  return false;
}

// bool LocalSearch::searchSwap(Solution &solution,
//                              const LocalSearchWeights &weights,
//                              SearchContext &ctx) {
//     MoveDescriptor move; // [CHANGE-5] local — was member move
//     const auto &routes = solution.getRoutes();
//     int numRoutes = routes.size();
//
//     if (!ctx.isValid || ctx.centroids.size() != numRoutes) {
//         return false;
//     }
//
//     // INTRA-ROUTE SWAP
//     for (int r = 0; r < numRoutes; ++r) {
//         const auto &nodes = routes[r].getNodes();
//         int n = nodes.size();
//         if (n < 4)
//             continue;
//
//         for (int i = 1; i < n - 2; ++i) {
//             // [OPT-2] Use flat cache instead of getNodeById()->getType()
//             if (nodeTypeById_[nodes[i]] != NodeType::CUSTOMER)
//                 continue;
//
//             for (int j = i + 2; j < n - 1; ++j) {
//                 // [OPT-2]
//                 if (nodeTypeById_[nodes[j]] != NodeType::CUSTOMER)
//                     continue;
//
//                 int prev_i = nodes[i - 1], next_i = nodes[i + 1];
//                 int prev_j = nodes[j - 1], next_j = nodes[j + 1];
//                 double oldDist = instance->getDistance(prev_i, nodes[i]) +
//                                  instance->getDistance(nodes[i], next_i) +
//                                  instance->getDistance(prev_j, nodes[j]) +
//                                  instance->getDistance(nodes[j], next_j);
//                 double newDist = instance->getDistance(prev_i, nodes[j]) +
//                                  instance->getDistance(nodes[j], next_i) +
//                                  instance->getDistance(prev_j, nodes[i]) +
//                                  instance->getDistance(nodes[i], next_j);
//
//                 if (newDist >= oldDist - 1e-6)
//                     continue;
//
//                 if (!routes[r].canPossiblyInsert(nodes[j], i, nodes[i]) ||
//                     !routes[r].canPossiblyInsert(nodes[i], j, nodes[j])) {
//                     continue;
//                 }
//
//                 move.reset();
//                 move.type = MoveType::INTRA_SWAP;
//                 move.routeIdx1 = r;
//                 move.nodeIdx1 = i;
//                 move.routeIdx2 = r;
//                 move.nodeIdx2 = j;
//
//                 evaluateMove(solution, move, weights);
//
//                 if (move.eval.isFeasible &&
//                     move.eval.objectiveDelta < -1e-9) {
//                     applyMove(solution, move);
//                     return true;
//                 }
//             }
//         }
//     }
//
//     // INTER-ROUTE SWAP
//     for (int r1 = 0; r1 < numRoutes; ++r1) {
//         for (int r2: ctx.neighborLists[r1]) {
//             if (r2 <= r1)
//                 continue;
//
//             const auto &nodes1 = routes[r1].getNodes();
//             const auto &nodes2 = routes[r2].getNodes();
//
//             const auto &ranked1 = this->getCachedRanking(r1, routes[r1],
//             ctx); const auto &ranked2 = this->getCachedRanking(r2,
//             routes[r2], ctx);
//
//             int attempts1 = std::min(maxSwapAttempts_, (int) ranked1.size());
//             int attempts2 = std::min(maxSwapAttempts_, (int) ranked2.size());
//
//             for (int k1 = 0; k1 < attempts1; ++k1) {
//                 int i = ranked1[k1].first;
//                 // [OPT-2]
//                 if (nodeTypeById_[nodes1[i]] != NodeType::CUSTOMER)
//                     continue;
//
//                 for (int k2 = 0; k2 < attempts2; ++k2) {
//                     int j = ranked2[k2].first;
//                     // [OPT-2]
//                     if (nodeTypeById_[nodes2[j]] != NodeType::CUSTOMER)
//                         continue;
//
//                     int nodeId1 = nodes1[i];
//                     int nodeId2 = nodes2[j];
//                     if (instance->getDistance(nodeId1, nodeId2) >=
//                     distanceThreshold_)
//                         continue;
//
//                     int prev1 = nodes1[i - 1], next1 = nodes1[i + 1];
//                     int prev2 = nodes2[j - 1], next2 = nodes2[j + 1];
//
//                     double oldDist = instance->getDistance(prev1, nodeId1) +
//                                      instance->getDistance(nodeId1, next1) +
//                                      instance->getDistance(prev2, nodeId2) +
//                                      instance->getDistance(nodeId2, next2);
//                     double newDist = instance->getDistance(prev1, nodeId2) +
//                                      instance->getDistance(nodeId2, next1) +
//                                      instance->getDistance(prev2, nodeId1) +
//                                      instance->getDistance(nodeId1, next2);
//
//                     if (newDist >= oldDist - 1e-9)
//                         continue;
//
//                     // TW feasibility check using twNext_ (precomputed
//                     matrix) if (!twNext_[prev1][nodeId2] ||
//                     !twNext_[nodeId2][next1] ||
//                         !twNext_[prev2][nodeId1] || !twNext_[nodeId1][next2])
//                         continue;
//
//                     if (!routes[r1].canPossiblyInsert(nodeId2, i, nodeId1) ||
//                         !routes[r2].canPossiblyInsert(nodeId1, j, nodeId2)) {
//                         continue;
//                     }
//
//                     move.reset();
//                     move.type = MoveType::INTER_SWAP;
//                     move.routeIdx1 = r1;
//                     move.nodeIdx1 = i;
//                     move.routeIdx2 = r2;
//                     move.nodeIdx2 = j;
//
//                     evaluateMove(solution, move, weights);
//
//                     if (move.eval.isFeasible &&
//                         move.eval.objectiveDelta < -1e-9) {
//                         applyMove(solution, move);
//                         return true;
//                     }
//                 }
//             }
//         }
//     }
//
//     return false;
// }

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
      const auto &nodes1 =
          routes[r1].getNodes(); // [OPT] Lấy tham chiếu trực tiếp
      int n1 = (int)nodes1.size();

      if (n1 < 2 + segLen)
        continue;

      for (int i = 1; i <= n1 - 1 - segLen; ++i) {
        bool segmentIsCustomers = true;
        for (int k = 0; k < segLen; ++k) {
          if (nodeTypeById_[nodes1[i + k]] != NodeType::CUSTOMER) {
            segmentIsCustomers = false;
            break;
          }
        }
        if (!segmentIsCustomers)
          continue;

        int firstNodeId = nodes1[i];
        int lastNodeId = nodes1[i + segLen - 1];

        int prev1 = nodes1[i - 1];
        int next1 = nodes1[i + segLen];
        double removalSavings = instance->getDistance(prev1, firstNodeId) +
                                instance->getDistance(lastNodeId, next1) -
                                instance->getDistance(prev1, next1);

        for (int r2 : neighborLists[r1]) {
          const auto &nodes2 =
              routes[r2].getNodes(); // [OPT] Lấy tham chiếu trực tiếp

          auto candidatePositions =
              findBestInsertionPositions_KNN(routes[r2], firstNodeId, 3);

          for (int j : candidatePositions) {
            // Intra overlap check
            if (r1 == r2 && (j >= i && j <= i + segLen)) {
              continue;
            }

            int prev2 = nodes2[j - 1];
            int next2 = nodes2[j];

            // [PRUNE-DIST] Granularity filter: skip nếu cả 2 điểm nối mới đều
            // xa
            if (instance->getDistance(prev2, firstNodeId) >=
                    distanceThreshold_ &&
                instance->getDistance(lastNodeId, next2) >=
                    distanceThreshold_) {
              continue;
            }

            double insertionCost = instance->getDistance(prev2, firstNodeId) +
                                   instance->getDistance(lastNodeId, next2) -
                                   instance->getDistance(prev2, next2);

            if (insertionCost >= removalSavings - 1e-9) {
              continue;
            }

            // [PRUNE-TW] twNext_ on segment endpoints
            if (!twNext_[prev2][firstNodeId] || !twNext_[lastNodeId][next2])
              continue;

            move.reset();
            move.type = MoveType::INTER_OR_OPT;
            move.routeIdx1 = r1;
            move.nodeIdx1 = i;
            move.routeIdx2 = r2;
            move.nodeIdx2 = j;
            move.segmentLength = segLen;

            evaluateMove(solution, move, weights);

            if (move.eval.isFeasible && move.eval.objectiveDelta < -1e-9) {
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
// searchIntraOrOpt — Đổi vị trí segment trong cùng một route
// ============================================================================
// Segment length 1, 2, 3. Dùng neighborLists để prune destination positions:
// chỉ xét insert gần segHead hoặc segTail (không brute-force mọi j).
// First-improvement.
// ============================================================================
bool LocalSearch::searchIntraOrOpt(Solution &solution,
                                   const LocalSearchWeights &weights,
                                   SearchContext &ctx) {
  if (!ctx.isValid)
    return false;

  auto &routes = solution.getRoutes();
  int numRoutes = (int)routes.size();
  if ((int)ctx.neighborLists.size() != numRoutes)
    return false;

  static const int SEGMENT_LENGTHS[] = {1, 2, 3};
  const double EPSILON = 1e-9;

  // [OPT-A] Reusable position lookup — reset per route, not per segment
  // Avoids O(N) linear scan inside innermost loop.
  // Size: maxNodeId+1, initialized to -1 once per route via stamp trick.
  const int maxId = (int)nodeTypeById_.size();
  std::vector<int> posInRoute(maxId, -1); // nodeId → index in current route
  std::vector<bool> triedPos; // [OPT-B] vector<bool> over unordered_set

  for (int r = 0; r < numRoutes; ++r) {
    const auto &nodes = routes[r].getNodes();
    int n = (int)nodes.size();

    // [OPT-A] Build posInRoute once per route — O(N), reused for all segments
    // Clear only slots used by previous route (avoid full O(maxId) reset)
    for (int k = 0; k < n; ++k)
      posInRoute[nodes[k]] = k;

    // [OPT-B] triedPos as flat bool array, sized to route — reset cheaply
    triedPos.assign(n + 1, false);

    for (int segLen : SEGMENT_LENGTHS) {
      if (n < 3 + segLen)
        continue;

      for (int i = 1; i <= n - 1 - segLen; ++i) {
        // Segment phải toàn customer
        bool allCust = true;
        for (int k = 0; k < segLen && allCust; ++k)
          if (nodeTypeById_[nodes[i + k]] != NodeType::CUSTOMER)
            allCust = false;
        if (!allCust)
          continue;

        int segHead = nodes[i];
        int segTail = nodes[i + segLen - 1];
        int prev1 = nodes[i - 1];
        int next1 = nodes[i + segLen];

        double removalSaving = instance->getDistance(prev1, segHead) +
                               instance->getDistance(segTail, next1) -
                               instance->getDistance(prev1, next1);

        // [OPT-B] Reset triedPos only for positions touched last iteration
        // Use fill on small range instead of re-assign each segment
        std::fill(triedPos.begin(), triedPos.end(), false);

        // [OPT-A+C] Use node-level KNN neighbors of segHead
        // knnCache_[segHead] gives the K nearest *nodes* to segHead —
        // much more targeted than neighborLists[r] (which is route-level).
        // For each neighbor node nd, O(1) lookup its position in this route.
        for (int nd : knnCache_[segHead]) {
          int j = posInRoute[nd];
          if (j < 1 || j >= n)
            continue; // nd not in this route

          // Try inserting before nd (at j) and after nd (at j+1)
          for (int ins : {j, j + 1}) {
            if (ins < 1 || ins >= n)
              continue;
            if (triedPos[ins])
              continue;
            triedPos[ins] = true;

            // Skip overlap with source segment
            if (ins >= i && ins <= i + segLen)
              continue;

            int prev2 = nodes[ins - 1];
            int next2 = nodes[ins];

            double insertCost = instance->getDistance(prev2, segHead) +
                                instance->getDistance(segTail, next2) -
                                instance->getDistance(prev2, next2);

            if (insertCost - removalSaving >= -EPSILON)
              continue;

            if (!twNext_[prev2][segHead] || !twNext_[segTail][next2])
              continue;

            MoveDescriptor move;
            move.type = MoveType::INTER_OR_OPT;
            move.routeIdx1 = r;
            move.nodeIdx1 = i;
            move.routeIdx2 = r;
            move.nodeIdx2 = ins;
            move.segmentLength = segLen;

            evaluateMove(solution, move, weights);

            if (move.eval.isFeasible && move.eval.objectiveDelta < -EPSILON) {
              applyMove(solution, move);
              ctx.markDirty(r);

              // [OPT-D] Invalidate posInRoute before returning —
              // route nodes changed, caller must not reuse stale cache
              for (int k = 0; k < n; ++k)
                posInRoute[nodes[k]] = -1;
              return true;
            }
          }
        }
      }
    }

    // [OPT-A] Clean up posInRoute slots for this route before next route
    for (int k = 0; k < n; ++k)
      posInRoute[nodes[k]] = -1;
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

  if (searchStationInsertion(solution))
    improved = true;

  {
    MoveDescriptor stRemMove;
    LocalSearchWeights w;
    w.dist = 1.0;
    if (searchStationRemoval(solution, stRemMove, w)) {
      applyMove(solution, stRemMove);
      improved = true;
    }
  }

  return improved;
}

bool LocalSearch::searchCrossExchange(Solution &solution,
                                      const LocalSearchWeights &weights,
                                      SearchContext &ctx) {
  MoveDescriptor move;
  static const int SEGMENT_LENGTHS[] = {1, 2, 3};

  int numRoutes = (int)solution.getRoutes().size(); // [FIX-1] không cache ref
  if (!ctx.isValid || (int)ctx.neighborLists.size() != numRoutes)
    return false;

  // [OPT-1] Đảo loop order: (r1, r2, i, len1, j, len2)
  // Thay vì (len1, len2, r1, r2, i, j) — tránh tính lại nodes/rem1/demand1
  for (int r1 = 0; r1 < numRoutes; ++r1) {
    const auto &nodes1 = solution.getRoutes()[r1]
                             .getNodes(); // [FIX-2] getNodes() thay getNodeAt()
    int n1 = (int)nodes1.size();

    for (int r2 : ctx.neighborLists[r1]) {
      if (r2 <= r1)
        continue;

      const auto &nodes2 = solution.getRoutes()[r2].getNodes();
      int n2 = (int)nodes2.size();

      for (int i = 1; i < n1 - 1; ++i) {

        for (int len1 : SEGMENT_LENGTHS) {
          if (i + len1 >= n1)
            continue; // i + len1 <= n1 - 1

          // [OPT-2] seg1 check + cache — tính 1 lần cho tất cả (len2, j)
          bool seg1Ok = true;
          for (int k = 0; k < len1; ++k)
            if (nodeTypeById_[nodes1[i + k]] != NodeType::CUSTOMER) {
              seg1Ok = false;
              break;
            }
          if (!seg1Ok)
            continue;

          int first1 = nodes1[i];
          int last1 = nodes1[i + len1 - 1];
          int prev1 = nodes1[i - 1];
          int next1 = nodes1[i + len1];

          // [OPT-3] Cache rem1 và demand1 — dùng lại cho tất cả (j, len2)
          double rem1 = instance->getDistance(prev1, first1) +
                        instance->getDistance(last1, next1) -
                        instance->getDistance(prev1, next1);

          double demand1 = 0.0;
          for (int k = 0; k < len1; ++k)
            demand1 += demandById_[nodes1[i + k]];

          double totalDemand1 = solution.getRoutes()[r1].getTotalDemand();
          double totalDemand2 = solution.getRoutes()[r2].getTotalDemand();
          double cap1 = solution.getRoutes()[r1].getVehicle()->getCapacity();
          double cap2 = solution.getRoutes()[r2].getVehicle()->getCapacity();

          for (int j = 1; j < n2 - 1; ++j) {

            // [OPT-4] Granular filter sớm nhất có thể — trước seg2 check
            if (instance->getDistance(first1, nodes2[j]) >= distanceThreshold_)
              continue;

            for (int len2 : SEGMENT_LENGTHS) {
              if (len1 == 1 && len2 == 1)
                continue;
              if (j + len2 >= n2)
                continue;

              bool seg2Ok = true;
              for (int k = 0; k < len2; ++k)
                if (nodeTypeById_[nodes2[j + k]] != NodeType::CUSTOMER) {
                  seg2Ok = false;
                  break;
                }
              if (!seg2Ok)
                continue;

              int first2 = nodes2[j];
              int last2 = nodes2[j + len2 - 1];
              int prev2 = nodes2[j - 1];
              int next2 = nodes2[j + len2];

              // [OPT-3] demand2 vẫn cần tính per (j, len2) — không tránh được
              double demand2 = 0.0;
              for (int k = 0; k < len2; ++k)
                demand2 += demandById_[nodes2[j + k]];

              // Capacity check dùng cached demand1, totalDemand1/2
              if (totalDemand1 - demand1 + demand2 > cap1)
                continue;
              if (totalDemand2 - demand2 + demand1 > cap2)
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

              if ((ins1 + ins2) - (rem1 + rem2) >= -1e-9)
                continue;

              if (!twNext_[prev1][first2] || !twNext_[last2][next1] ||
                  !twNext_[prev2][first1] || !twNext_[last1][next2])
                continue;

              move.reset();
              move.type = MoveType::INTER_CROSS_EXCHANGE;
              move.routeIdx1 = r1;
              move.nodeIdx1 = i;
              move.routeIdx2 = r2;
              move.nodeIdx2 = j;
              move.segmentLength = len1;
              move.segmentLength2 = len2;

              evaluateMove(solution, move, weights);

              if (move.eval.isFeasible && move.eval.objectiveDelta < -1e-9) {
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
      double delta = newDist - oldDist;

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

      int bestReplacement = -1;
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
        size_t pos = it->first;
        int stationId = nodes[pos];

        bool isAtDepotPosition = (pos == 1);

        auto stationNode = instance->getNodeById(stationId);
        auto depotNode = instance->getNodeById(0);
        bool sameAsDepot =
            (std::abs(stationNode->getX() - depotNode->getX()) < 0.01 &&
             std::abs(stationNode->getY() - depotNode->getY()) < 0.01);

        if (sameAsDepot) {
          Route testRoute = routes[r];
          testRoute.removeNode(pos);
          testRoute.evaluate();

          if (testRoute.isFeasible()) {
            routes[r] = testRoute;
            improved = true;
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
            improved = true;
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

  // if (segmentCrossExchangeForVehicleReduction(solution))
  //   return true;

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

  int smallestIdx = -1;
  size_t minCustomers = std::numeric_limits<size_t>::max();

  for (int r = 0; r < numRoutes; ++r) {
    size_t custCount = routes[r].getCustomers().size();
    if (custCount > 0 && custCount < minCustomers) {
      minCustomers = custCount;
      smallestIdx = r;
    }
  }

  if (smallestIdx == -1 || minCustomers > 20) // [FIX] tăng từ 15 lên 20
    return false;

  std::vector<int> customersToMove = routes[smallestIdx].getCustomers();

  std::vector<Route> newRoutes;
  for (int r = 0; r < numRoutes; ++r) {
    if (r != smallestIdx) {
      newRoutes.push_back(routes[r]);
    }
  }

  struct InsertCandidate {
    int custId = -1;
    int routeIdx = -1;
    size_t pos = 0;
    double cost = 1e18;
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
            if (isTightTW && pos <= states.size()) {
              double travelToPrev = instance->getTime(nodes[pos - 1], custId);
              double arrivalAtCust =
                  states[pos - 1].departureTime + travelToPrev;
              double slack = custNode->getDueDate() - arrivalAtCust;
              if (slack < 0)
                continue;
              double waitPenalty =
                  std::max(0.0, custNode->getReadyTime() - arrivalAtCust);
              cost += waitPenalty * 0.3;
            }
            if (cost < best.cost)
              best = {custId, r, pos, cost, -1, true};
          }
        }

        // Options B & C: Station-assisted, top-1 station by detour
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
        int topK = std::min(2, (int)sc.size()); // reduced to 1 earlier
        std::partial_sort(sc.begin(), sc.begin() + topK, sc.end(),
                          [](const SC &a, const SC &b) { return a.det < b.det; });

        for (int k = 0; k < topK; ++k) {
          int sid = sc[k].id;
          
          // [OPT-4] Pre-filter distance before deep copy
          // Estimate B: prev -> cust -> sid -> next
          double costB = instance->getDistance(prevId, custId) + 
                         instance->getDistance(custId, sid) + 
                         instance->getDistance(sid, nextId) - directDist;
          
          if (costB < best.cost) {
            Route copy = newRoutes[r];
            copy.addNode(custId, pos);
            copy.addNode(sid, pos);
            copy.evaluate();
            if (copy.isFeasible()) {
              double dc = copy.getTotalDistance() - newRoutes[r].getTotalDistance();
              if (dc < best.cost) {
                best = {custId, r, pos, dc, sid, true};
              }
            }
          }

          // Estimate C: prev -> sid -> cust -> next
          double costC = instance->getDistance(prevId, sid) + 
                         instance->getDistance(sid, custId) + 
                         instance->getDistance(custId, nextId) - directDist;
          
          if (costC < best.cost) {
            Route copy = newRoutes[r];
            copy.addNode(custId, pos);
            copy.addNode(sid, pos + 1);
            copy.evaluate();
            if (copy.isFeasible()) {
              double dc = copy.getTotalDistance() - newRoutes[r].getTotalDistance();
              if (dc < best.cost) {
                best = {custId, r, pos, dc, sid, false};
              }
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
  struct RouteBest {
    InsertCandidate rank1;
    InsertCandidate rank2;
  };
  std::unordered_map<int, std::vector<RouteBest>> costCache;
  for (int cId : unplaced)
    costCache[cId].resize(newRoutes.size());
  std::vector<bool> routeDirty(newRoutes.size(), true);

  while (!unplaced.empty()) {
    int bestIdx = -1;
    double maxRegret = -1e18;
    InsertCandidate bestCand;

    // Recompute dirty routes
    for (int r = 0; r < (int)newRoutes.size(); ++r) {
      if (!routeDirty[r])
        continue;
      for (int cId : unplaced) {
        InsertCandidate rank1, rank2;
        rank1.custId = cId;
        rank2.custId = cId;
        rank2.cost = 1e18;

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
        costCache[cId][r] = {rank1, rank2};
      }
      routeDirty[r] = false;
    }

    for (int idx = 0; idx < (int)unplaced.size(); ++idx) {
      int cId = unplaced[idx];
      InsertCandidate globalR1, globalR2;
      globalR1.custId = cId;
      globalR2.custId = cId;
      globalR2.cost = 1e18;

      for (int r = 0; r < (int)newRoutes.size(); ++r) {
        const auto &rb = costCache[cId][r];
        if (rb.rank1.routeIdx == -1)
          continue;

        if (rb.rank1.cost < globalR1.cost) {
          globalR2 = globalR1;
          globalR1 = rb.rank1;
          if (rb.rank2.routeIdx != -1 && rb.rank2.cost < globalR2.cost) {
            globalR2 = rb.rank2;
          }
        } else if (rb.rank1.cost < globalR2.cost) {
          globalR2 = rb.rank1;
        }
      }

      if (globalR1.routeIdx == -1)
        continue;

      double regret =
          (globalR2.routeIdx == -1) ? 1e15 : (globalR2.cost - globalR1.cost);
      if (regret > maxRegret) {
        maxRegret = regret;
        bestIdx = idx;
        bestCand = globalR1;
      }
    }

    if (bestIdx == -1)
      break;

    applyCandidate(bestCand);
    routeDirty[bestCand.routeIdx] = true;

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

  // ── Build victim candidate list ───────────────────────────────────────────
  struct VictimCand {
    int idx;
    int custCount;
    double score;
  };
  std::vector<VictimCand> victimCands;
  {
    double horizon = instance->getNodeById(0)->getDueDate();
    for (int r = 0; r < numRoutes; ++r) {
      const auto &custs = routes[r].getCustomers();
      int cnt = (int)custs.size();
      const int avgCusts = instance->getCustomers().size() / solution.getNumRoutes();
      const int victimSizeLimit = std::max(10, avgCusts * 2);
      if (cnt == 0 || cnt > victimSizeLimit)
        continue;
      double avgTW = 0.0;
      for (int c : custs) {
        auto nd = instance->getNodeById(c);
        avgTW +=
            (nd->getDueDate() - nd->getReadyTime()) / std::max(1.0, horizon);
      }
      avgTW /= cnt;
      double sizeFactor = 1.0 - cnt / 21.0;
      victimCands.push_back({r, cnt, 0.6 * avgTW + 0.4 * sizeFactor});
    }
  }
  if (victimCands.empty())
    return false;
  std::sort(victimCands.begin(), victimCands.end(),
            [](const VictimCand &a, const VictimCand &b) {
              return a.score > b.score;
            });

  const int MAX_VICTIMS = std::min(3, (int)victimCands.size());
  const double vehCap = instance->getVehicleCapacity();

  // =========================================================================
  // Shared slack cache — one instance per victim, reused across all stages.
  // slackDirty[r]=true means cachedSlacks[r] must be rebuilt before use.
  // =========================================================================
  std::vector<std::vector<double>> cachedSlacks;
  std::vector<bool> slackDirty;

  // Rebuild only dirty slots — O(dirty_count * N) instead of O(R * N).
  auto rebuildSlacks = [&]() {
    for (size_t r = 0; r < cachedSlacks.size(); ++r) {
      if (slackDirty[r]) {
        // Note: workRoutes captured by ref in each stage lambda below
        // This lambda is always called with workRoutes in scope via capture
        slackDirty[r] = false; // will be set per-stage below
      }
    }
  };
  (void)rebuildSlacks; // suppress unused warning — inlined per-stage

  // =========================================================================
  // HELPER 1: findDirect
  // Accepts pre-computed cachedSlacks to avoid redundant getTimeSlack() calls.
  // =========================================================================
  struct PlaceResult {
    int r = -1;
    size_t pos = 0;
    double cost = 1e18;
  };

  auto findDirect = [&](int cId, const std::vector<Route> &workRoutes,
                        const std::vector<std::vector<double>> &slacks,
                        int excludeR = -1) -> PlaceResult {
    PlaceResult best;
    double dem = demandById_[cId];
    auto custNode = instance->getNodeById(cId);
    double twWindow = custNode->getDueDate() - custNode->getReadyTime();
    bool isTightTW = (twWindow <= 30.0);

    for (int r = 0; r < (int)workRoutes.size(); ++r) {
      if (r == excludeR)
        continue;
      if (workRoutes[r].getTotalDemand() + dem > vehCap)
        continue;

      const auto &wnodes = workRoutes[r].getNodes();
      const auto &states = workRoutes[r].getStates();
      const std::vector<double> &sl = slacks[r];

      for (size_t pos = 1; pos < workRoutes[r].size(); ++pos) {
        int prevId = wnodes[pos - 1];
        int nextId = wnodes[pos];
        double arrC =
            states[pos - 1].departureTime + instance->getTime(prevId, cId);

        if (arrC > custNode->getDueDate())
          continue;

        double waitC = std::max(0.0, custNode->getReadyTime() - arrC);
        double depC = arrC + waitC + custNode->getServiceTime();
        double arrNextNew = depC + instance->getTime(cId, nextId);
        double delay = std::max(0.0, arrNextNew - states[pos].arrivalTime);
        double effectiveSlack = sl[pos] + states[pos].timeWait;

        if (delay > effectiveSlack)
          continue;
        if (!workRoutes[r].canPossiblyInsert(cId, pos))
          continue;

        InsertionResult res = workRoutes[r].checkInsertionCost(cId, pos);
        if (!res.isFeasible)
          continue;

        double totalCost = res.deltaDistance;
        if (isTightTW) {
          double travel = instance->getTime(wnodes[pos - 1], cId);
          double arrival = states[pos - 1].departureTime + travel;
          totalCost += std::max(0.0, custNode->getReadyTime() - arrival) * 0.3;
        }
        if (totalCost < best.cost)
          best = {r, pos, totalCost};
      }
    }
    return best;
  };

  // =========================================================================
  // HELPER 2: tryInsertFast
  // checkInsertionCost guarantees feasibility — no evaluate()+isFeasible()
  // loop. Uses pre-built slacks passed in to avoid redundant getTimeSlack()
  // call.
  // =========================================================================
  auto tryInsertFast = [&](Route &route, int cId,
                           const std::vector<double> &slacks) -> bool {
    struct PosCost {
      size_t pos;
      double cost;
    };
    const auto &nodes = route.getNodes();
    const auto &states = route.getStates();
    auto cNode = instance->getNodeById(cId);

    PosCost best{0, 1e18};

    for (size_t pos = 1; pos < nodes.size(); ++pos) {
      int prevId = nodes[pos - 1];
      int nextId = nodes[pos];
      double arrC =
          states[pos - 1].departureTime + instance->getTime(prevId, cId);

      if (arrC > cNode->getDueDate())
        continue;

      double waitC = std::max(0.0, cNode->getReadyTime() - arrC);
      double depC = arrC + waitC + cNode->getServiceTime();
      double arrNextNew = depC + instance->getTime(cId, nextId);
      double delay = std::max(0.0, arrNextNew - states[pos].arrivalTime);
      double effectiveSlack = slacks[pos] + states[pos].timeWait;

      if (delay > effectiveSlack)
        continue;
      if (!route.canPossiblyInsert(cId, pos))
        continue;

      InsertionResult res = route.checkInsertionCost(cId, pos);
      if (!res.isFeasible)
        continue;
      if (res.deltaDistance < best.cost)
        best = {pos, res.deltaDistance};
    }

    if (best.cost >= 1e17)
      return false;

    route.addNode(cId, best.pos);
#ifndef NDEBUG
    route.evaluate();
    if (!route.isFeasible()) {
      std::cerr << "[WARNING] Feasibility mismatch in Route " << route.getId()
                << " for Customer " << cId << " at pos " << best.pos << "!\n";
      route.removeNode(best.pos);
      return false;
    }
#endif
    return true;
  };

  // =========================================================================
  // HELPER 3: buildCands
  // Returns span into thread_local storage — zero heap allocation.
  // WARNING: span is invalidated on the next buildCands call.
  // =========================================================================
  struct Cand3 {
    int id, pos;
    double savings;
  };
  static thread_local std::vector<Cand3> tl_cands;

  // Returns the valid count; caller uses tl_cands[0..count-1]
  auto buildCands = [&](const std::vector<Route> &wr, int ri, int cap,
                        double demIn) -> int {
    tl_cands.clear();
    const auto &ns = wr[ri].getNodes();
    double currentDem = wr[ri].getTotalDemand();

    for (int i = 1; i < (int)ns.size() - 1; ++i) {
      int nid = ns[i];
      if (nodeTypeById_[nid] != NodeType::CUSTOMER)
        continue;
      if (currentDem - demandById_[nid] + demIn > vehCap)
        continue;

      int pv = ns[i - 1], nx = ns[i + 1];
      double s = instance->getDistance(pv, nid) +
                 instance->getDistance(nid, nx) - instance->getDistance(pv, nx);
      tl_cands.push_back({nid, i, s});
    }
    int topN = std::min(cap, (int)tl_cands.size());
    std::partial_sort(
        tl_cands.begin(), tl_cands.begin() + topN, tl_cands.end(),
        [](const Cand3 &a, const Cand3 &b) { return a.savings > b.savings; });
    return topN;
  };

  // =========================================================================
  // HELPER 4: applyStation
  // thread_local sc vector avoids heap allocation per (r, pos).
  // =========================================================================
  auto applyStation = [&](int cId, std::vector<Route> &wr,
                          int excludeR = -1) -> bool {
    double dem = demandById_[cId];
    for (int r = 0; r < (int)wr.size(); ++r) {
      if (r == excludeR)
        continue;
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
        static thread_local std::vector<SC> sc;
        sc.clear();
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
              removeRedundantStations(copy);
              wr[r] = copy;
              return true;
            }
          }
        }
      }
    }
    return false;
  };

  // =========================================================================
  // HELPER 5: markDirtyAndRebuild
  // Mark one or more route indices dirty then rebuild all dirty slots.
  // Centralises the "dirty → rebuild" pattern used across all stages.
  // =========================================================================
  auto markDirty = [&](std::vector<Route> &workRoutes,
                       std::initializer_list<int> idxs) {
    for (int i : idxs) {
      if (i >= 0 && i < (int)slackDirty.size())
        slackDirty[i] = true;
    }
    for (size_t r = 0; r < workRoutes.size(); ++r) {
      if (slackDirty[r]) {
        cachedSlacks[r] = workRoutes[r].getTimeSlack();
        slackDirty[r] = false;
      }
    }
  };

  // =========================================================================
  // ejectPenalty — flat array (O(1)) instead of unordered_map
  // =========================================================================
  std::vector<int> ejectPenalty(nodeTypeById_.size(), 0);

  // ── Victim loop ───────────────────────────────────────────────────────────
  for (int vi = 0; vi < MAX_VICTIMS; ++vi) {
    int smallestIdx = victimCands[vi].idx;

    std::vector<int> pool = routes[smallestIdx].getCustomers();
    std::vector<Route> workRoutes;
    workRoutes.reserve(numRoutes - 1);
    for (int r = 0; r < numRoutes; ++r)
      if (r != smallestIdx)
        workRoutes.push_back(routes[r]);

    // Init slack cache — all dirty at start of each victim attempt
    const int WR = (int)workRoutes.size();
    cachedSlacks.assign(WR, {});
    slackDirty.assign(WR, true);
    // Initial full build
    markDirty(workRoutes, {}); // no extra dirty, just rebuilds all-true slots

    std::vector<int> unplaced = pool;

    // ── Stage A: Regret-2 with TW penalty ─────────────────────────────────
    auto runStageA = [&](std::vector<int> &up) {
      bool progress = true;
      while (!up.empty() && progress) {
        progress = false;

        // cachedSlacks already up-to-date (markDirty called after each
        // mutation)

        int bestIdx = -1;
        double maxRegret = -1e18;
        PlaceResult bestPlace;

        for (int idx = 0; idx < (int)up.size(); ++idx) {
          int cId = up[idx];
          double dem = demandById_[cId];
          auto cNode = instance->getNodeById(cId); // hoisted outside (r,pos)
          PlaceResult rank1, rank2;
          rank2.cost = 1e18;

          for (int r = 0; r < WR; ++r) {
            if (workRoutes[r].getTotalDemand() + dem > vehCap)
              continue;
            const auto &wnodes = workRoutes[r].getNodes();
            const auto &states = workRoutes[r].getStates();
            const std::vector<double> &slacks = cachedSlacks[r];

            for (size_t pos = 1; pos < workRoutes[r].size(); ++pos) {
              double effectiveCost = 1e18;

              int prevId = wnodes[pos - 1];
              int nextId = wnodes[pos];
              double arrC = states[pos - 1].departureTime +
                            instance->getTime(prevId, cId);

              if (arrC <= cNode->getDueDate()) {
                double waitC = std::max(0.0, cNode->getReadyTime() - arrC);
                double depC = arrC + waitC + cNode->getServiceTime();
                double arrNextNew = depC + instance->getTime(cId, nextId);
                double delay =
                    std::max(0.0, arrNextNew - states[pos].arrivalTime);
                double effSlack = slacks[pos] + states[pos].timeWait;

                if (delay <= effSlack) {
                  if (workRoutes[r].canPossiblyInsert(cId, pos)) {
                    InsertionResult res =
                        workRoutes[r].checkInsertionCost(cId, pos);
                    if (res.isFeasible)
                      effectiveCost = res.deltaDistance;
                  }
                }
              }

              // Station-assisted fallback
              if (effectiveCost >= 1e17) {
                double direct = instance->getDistance(prevId, nextId);
                static thread_local std::vector<std::pair<double, int>> sc;
                sc.clear();
                for (int sid : stationIds) {
                  double det = instance->getDistance(prevId, sid) +
                               instance->getDistance(sid, nextId) - direct;
                  if (det < direct * 3.0 + 30.0)
                    sc.push_back({det, sid});
                }
                int topK = std::min(3, (int)sc.size());
                if (topK > 0) {
                  std::partial_sort(sc.begin(), sc.begin() + topK, sc.end());
                  for (int k = 0; k < topK; ++k) {
                    int sid = sc[k].second;
                    for (bool bef : {true, false}) {
                      Route copy = workRoutes[r];
                      copy.addNode(cId, pos);
                      copy.addNode(sid, bef ? pos : pos + 1);
                      copy.evaluate();
                      if (copy.isFeasible()) {
                        removeRedundantStations(copy);
                        double dc = copy.getTotalDistance() -
                                    workRoutes[r].getTotalDistance();
                        if (dc < effectiveCost)
                          effectiveCost = dc;
                      }
                    }
                  }
                }
              }

              if (effectiveCost >= 1e17)
                continue;
              if (effectiveCost < rank1.cost) {
                rank2 = rank1;
                rank1 = {r, pos, effectiveCost};
              } else if (effectiveCost < rank2.cost) {
                rank2 = {r, pos, effectiveCost};
              }
            }
          }
          if (rank1.r == -1)
            continue;

          double twWidth = cNode->getDueDate() - cNode->getReadyTime();
          double twPenalty = 100.0 / std::max(1.0, twWidth);
          double regret =
              ((rank2.r == -1) ? 1e15 : (rank2.cost - rank1.cost)) + twPenalty;

          if (regret > maxRegret) {
            maxRegret = regret;
            bestIdx = idx;
            bestPlace = rank1;
          }
        }

        if (bestIdx == -1)
          break;

        workRoutes[bestPlace.r].addNode(up[bestIdx], bestPlace.pos);
        workRoutes[bestPlace.r].evaluate();
        up[bestIdx] = up.back();
        up.pop_back();

        // Only the modified route needs slack rebuild
        markDirty(workRoutes, {bestPlace.r});
        progress = true;
      }
    };

    runStageA(unplaced);

    if (!unplaced.empty()) {
      for (int retry = 0; retry < 2 && !unplaced.empty(); ++retry) {
        auto workSnap = workRoutes;
        auto upSnap = unplaced;
        if (retry == 0)
          std::reverse(unplaced.begin(), unplaced.end());
        else
          std::rotate(unplaced.begin(), unplaced.begin() + unplaced.size() / 2,
                      unplaced.end());

        size_t beforeSize = unplaced.size();
        std::fill(slackDirty.begin(), slackDirty.end(), true);
        markDirty(workRoutes, {}); // rebuild all after fill
        runStageA(unplaced);

        if (unplaced.size() >= beforeSize) {
          workRoutes = workSnap;
          unplaced = upSnap;
          std::fill(slackDirty.begin(), slackDirty.end(), true);
          markDirty(workRoutes, {}); // rebuild after snap restore
        }
      }
    }

    // ── Stage B: Ejection depth-1 ──────────────────────────────────────────
    if (!unplaced.empty()) {
      std::fill(ejectPenalty.begin(), ejectPenalty.end(), 0);
      std::vector<int> stillUnplaced;

      for (int xId : unplaced) {
        double demX = demandById_[xId];
        bool placed = false;

        for (int r = 0; r < WR && !placed; ++r) {
          int bestY = -1, bestYPos = -1;
          double bestYScore = -1e18;
          const auto &rnodes = workRoutes[r].getNodes();

          for (int i = 1; i < (int)rnodes.size() - 1; ++i) {
            int yId = rnodes[i];
            if (nodeTypeById_[yId] != NodeType::CUSTOMER)
              continue;
            double demY = demandById_[yId];
            if (workRoutes[r].getTotalDemand() - demY + demX > vehCap)
              continue;

            auto custY = instance->getNodeById(yId);
            int receivable = 0;
            for (int r2 = 0; r2 < WR; ++r2) {
              if (r2 == r)
                continue;
              if (workRoutes[r2].getTotalDemand() + demY <= vehCap)
                receivable++;
            }
            double twWidth = custY->getDueDate() - custY->getReadyTime();
            double scoreY =
                receivable + twWidth / 100.0 - ejectPenalty[yId] * 1000.0;
            if (scoreY > bestYScore) {
              bestYScore = scoreY;
              bestY = yId;
              bestYPos = i;
            }
          }
          if (bestY == -1)
            continue;

          Route backup_r = workRoutes[r];
          workRoutes[r].removeNode((size_t)bestYPos);
          workRoutes[r].evaluate();
          // Rebuild slack for r before passing to tryInsertFast
          markDirty(workRoutes, {r});

          if (!tryInsertFast(workRoutes[r], xId, cachedSlacks[r])) {
            workRoutes[r] = backup_r;
            markDirty(workRoutes, {r}); // restore → rebuild
            continue;
          }
          // r was mutated by tryInsertFast (addNode) — mark dirty again
          markDirty(workRoutes, {r});

          PlaceResult yPlace = findDirect(bestY, workRoutes, cachedSlacks, r);
          if (yPlace.r != -1) {
            Route backup_yp = workRoutes[yPlace.r];
            workRoutes[yPlace.r].addNode(bestY, yPlace.pos);
            workRoutes[yPlace.r].evaluate();
            // Only verify the 2 modified routes — not all R
            if (workRoutes[r].isFeasible() &&
                workRoutes[yPlace.r].isFeasible()) {
              markDirty(workRoutes, {yPlace.r});
              placed = true;
              break;
            }
            workRoutes[yPlace.r] = backup_yp;
            markDirty(workRoutes, {yPlace.r});
          } else {
            if (!applyStation(bestY, workRoutes, r)) {
              workRoutes[r] = backup_r;
              markDirty(workRoutes, {r});
              ejectPenalty[bestY]++;
              continue;
            }
            // applyStation modified some route — rebuild all to be safe
            std::fill(slackDirty.begin(), slackDirty.end(), true);
            markDirty(workRoutes, {});
            if (workRoutes[r].isFeasible()) {
              placed = true;
              break;
            }
          }

          if (!placed) {
            workRoutes[r] = backup_r;
            markDirty(workRoutes, {r});
          }
        }
        if (!placed)
          stillUnplaced.push_back(xId);
      }
      unplaced = stillUnplaced;
    }

    // ── Stage B2: Ejection depth-2 (top-5 Y/Z) ────────────────────────────
    if (!unplaced.empty()) {
      std::vector<int> stillUnplaced;
      for (int xId : unplaced) {
        bool placed = false;

        for (int r1 = 0; r1 < WR && !placed; ++r1) {
          int nYCands = buildCands(workRoutes, r1, 5, demandById_[xId]);

          for (int yi = 0; yi < nYCands && !placed; ++yi) {
            int yId = tl_cands[yi].id, yPos = tl_cands[yi].pos;

            Route backup_r1 = workRoutes[r1];
            workRoutes[r1].removeNode(yPos);
            workRoutes[r1].evaluate();
            markDirty(workRoutes, {r1});

            if (!tryInsertFast(workRoutes[r1], xId, cachedSlacks[r1])) {
              workRoutes[r1] = backup_r1;
              markDirty(workRoutes, {r1});
              continue;
            }
            markDirty(workRoutes, {r1});

            PlaceResult yp = findDirect(yId, workRoutes, cachedSlacks, r1);
            if (yp.r != -1) {
              Route backup_yp = workRoutes[yp.r];
              workRoutes[yp.r].addNode(yId, yp.pos);
              workRoutes[yp.r].evaluate();
              if (workRoutes[yp.r].isFeasible()) {
                markDirty(workRoutes, {yp.r});
                placed = true;
                break;
              }
              workRoutes[yp.r] = backup_yp;
              markDirty(workRoutes, {yp.r});
            }

            if (!placed) {
              for (int r2 = 0; r2 < WR && !placed; ++r2) {
                if (r2 == r1)
                  continue;
                int nZCands = buildCands(workRoutes, r2, 5, demandById_[yId]);

                for (int zi = 0; zi < nZCands && !placed; ++zi) {
                  int zId = tl_cands[zi].id, zPos = tl_cands[zi].pos;

                  Route backup_r2 = workRoutes[r2];
                  workRoutes[r2].removeNode(zPos);
                  workRoutes[r2].evaluate();
                  markDirty(workRoutes, {r2});

                  if (!tryInsertFast(workRoutes[r2], yId, cachedSlacks[r2])) {
                    workRoutes[r2] = backup_r2;
                    markDirty(workRoutes, {r2});
                    continue;
                  }
                  markDirty(workRoutes, {r2});

                  PlaceResult zp =
                      findDirect(zId, workRoutes, cachedSlacks, r2);
                  if (zp.r == -1) {
                    std::vector<Route> full_backup = workRoutes;
                    if (applyStation(zId, workRoutes, r2)) {
                      std::fill(slackDirty.begin(), slackDirty.end(), true);
                      markDirty(workRoutes, {});
                      placed = true;
                      break;
                    }
                    workRoutes = full_backup;
                    std::fill(slackDirty.begin(), slackDirty.end(), true);
                    markDirty(workRoutes, {});
                  } else {
                    Route backup_zp = workRoutes[zp.r];
                    workRoutes[zp.r].addNode(zId, zp.pos);
                    workRoutes[zp.r].evaluate();
                    if (workRoutes[zp.r].isFeasible()) {
                      markDirty(workRoutes, {zp.r});
                      placed = true;
                      break;
                    }
                    workRoutes[zp.r] = backup_zp;
                    markDirty(workRoutes, {zp.r});
                  }

                  if (!placed) {
                    workRoutes[r2] = backup_r2;
                    markDirty(workRoutes, {r2});
                  }
                }
              }
            }
            if (!placed) {
              workRoutes[r1] = backup_r1;
              markDirty(workRoutes, {r1});
            }
          }
        }
        if (!placed)
          stillUnplaced.push_back(xId);
      }
      unplaced = stillUnplaced;
    }

    // ── Stage B3: Ejection depth-3 (top-3 Y/Z/W) ─────────────────────────
    if (!unplaced.empty()) {
      std::vector<int> stillUnplaced;
      for (int xId : unplaced) {
        bool placed = false;

        for (int r1 = 0; r1 < WR && !placed; ++r1) {
          int nYCands = buildCands(workRoutes, r1, 3, demandById_[xId]);

          for (int yi = 0; yi < nYCands && !placed; ++yi) {
            int yId = tl_cands[yi].id, yPos = tl_cands[yi].pos;

            Route backup_r1 = workRoutes[r1];
            workRoutes[r1].removeNode(yPos);
            workRoutes[r1].evaluate();
            markDirty(workRoutes, {r1});

            if (!tryInsertFast(workRoutes[r1], xId, cachedSlacks[r1])) {
              workRoutes[r1] = backup_r1;
              markDirty(workRoutes, {r1});
              continue;
            }
            markDirty(workRoutes, {r1});

            PlaceResult yp = findDirect(yId, workRoutes, cachedSlacks, r1);
            if (yp.r != -1) {
              Route backup_yp = workRoutes[yp.r];
              workRoutes[yp.r].addNode(yId, yp.pos);
              workRoutes[yp.r].evaluate();
              if (workRoutes[yp.r].isFeasible()) {
                markDirty(workRoutes, {yp.r});
                placed = true;
                break;
              }
              workRoutes[yp.r] = backup_yp;
              markDirty(workRoutes, {yp.r});
            }

            if (!placed) {
              for (int r2 = 0; r2 < WR && !placed; ++r2) {
                if (r2 == r1)
                  continue;
                int nZCands = buildCands(workRoutes, r2, 3, demandById_[yId]);

                for (int zi = 0; zi < nZCands && !placed; ++zi) {
                  int zId = tl_cands[zi].id, zPos = tl_cands[zi].pos;

                  Route backup_r2 = workRoutes[r2];
                  workRoutes[r2].removeNode(zPos);
                  workRoutes[r2].evaluate();
                  markDirty(workRoutes, {r2});

                  if (!tryInsertFast(workRoutes[r2], yId, cachedSlacks[r2])) {
                    workRoutes[r2] = backup_r2;
                    markDirty(workRoutes, {r2});
                    continue;
                  }
                  markDirty(workRoutes, {r2});

                  PlaceResult zp =
                      findDirect(zId, workRoutes, cachedSlacks, r2);
                  if (zp.r != -1) {
                    Route backup_zp = workRoutes[zp.r];
                    workRoutes[zp.r].addNode(zId, zp.pos);
                    workRoutes[zp.r].evaluate();
                    if (workRoutes[zp.r].isFeasible()) {
                      markDirty(workRoutes, {zp.r});
                      placed = true;
                      break;
                    }
                    workRoutes[zp.r] = backup_zp;
                    markDirty(workRoutes, {zp.r});
                  }

                  if (!placed) {
                    for (int r3 = 0; r3 < WR && !placed; ++r3) {
                      if (r3 == r1 || r3 == r2)
                        continue;
                      int nWCands =
                          buildCands(workRoutes, r3, 3, demandById_[zId]);

                      for (int wi = 0; wi < nWCands && !placed; ++wi) {
                        int wId = tl_cands[wi].id, wPos = tl_cands[wi].pos;

                        Route backup_r3 = workRoutes[r3];
                        workRoutes[r3].removeNode(wPos);
                        workRoutes[r3].evaluate();
                        markDirty(workRoutes, {r3});

                        if (!tryInsertFast(workRoutes[r3], zId,
                                           cachedSlacks[r3])) {
                          workRoutes[r3] = backup_r3;
                          markDirty(workRoutes, {r3});
                          continue;
                        }
                        markDirty(workRoutes, {r3});

                        PlaceResult wp =
                            findDirect(wId, workRoutes, cachedSlacks, r3);
                        if (wp.r == -1) {
                          std::vector<Route> full_backup = workRoutes;
                          if (applyStation(wId, workRoutes, r3)) {
                            std::fill(slackDirty.begin(), slackDirty.end(),
                                      true);
                            markDirty(workRoutes, {});
                            placed = true;
                            break;
                          }
                          workRoutes = full_backup;
                          std::fill(slackDirty.begin(), slackDirty.end(), true);
                          markDirty(workRoutes, {});
                        } else {
                          Route backup_wp = workRoutes[wp.r];
                          workRoutes[wp.r].addNode(wId, wp.pos);
                          workRoutes[wp.r].evaluate();
                          if (workRoutes[wp.r].isFeasible()) {
                            markDirty(workRoutes, {wp.r});
                            placed = true;
                            break;
                          }
                          workRoutes[wp.r] = backup_wp;
                          markDirty(workRoutes, {wp.r});
                        }

                        if (!placed) {
                          workRoutes[r3] = backup_r3;
                          markDirty(workRoutes, {r3});
                        }
                      }
                    }
                  }
                  if (!placed) {
                    workRoutes[r2] = backup_r2;
                    markDirty(workRoutes, {r2});
                  }
                }
              }
            }
            if (!placed) {
              workRoutes[r1] = backup_r1;
              markDirty(workRoutes, {r1});
            }
          }
        }
        if (!placed)
          stillUnplaced.push_back(xId);
      }
      unplaced = stillUnplaced;
    }

    // ── Stage C: Station-assisted fallback ────────────────────────────────
    if (!unplaced.empty()) {
      std::vector<int> finalUnplaced;
      for (int cId : unplaced)
        if (!applyStation(cId, workRoutes))
          finalUnplaced.push_back(cId);
      unplaced = finalUnplaced;
    }

    if (!unplaced.empty())
      continue; // try next victim

    bool victimOk = true;
    for (auto &wr : workRoutes) {
      wr.evaluate();
      if (!wr.isFeasible()) {
        victimOk = false;
        break;
      }
    }
    if (!victimOk)
      continue;

    // Commit
    while (solution.getNumRoutes() > 0)
      solution.removeRoute(0);
    for (auto &wr : workRoutes)
      if (!wr.getCustomers().empty())
        solution.addRoute(wr);
    solution.evaluateRoutes();
    return true;

  } // end victim loop

  return false;
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

  const auto &nodes = route.getNodes();
  auto targetNode = instance->getNodeById(nodeId);
  const auto &states = route.getStates();

  for (size_t pos = 1; pos < nodes.size(); ++pos) {
    int prevNodeId = nodes[pos - 1];

    double distBefore = instance->getDistance(prevNodeId, nodes[pos]);
    double distAfter = instance->getDistance(prevNodeId, nodeId) +
                       instance->getDistance(nodeId, nodes[pos]);
    double detour = distAfter - distBefore;

    double timePenalty = 0.0;
    if (pos > states.size())
      continue;

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
  std::partial_sort(candidates.begin(), candidates.begin() + k,
                    candidates.end());

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
  int numRoutes = (int)routes.size();
  if (numRoutes < 2)
    return false;

  // ── Chọn cặp route tốt nhất để merge ────────────────────────────────────
  double bestMergeScore = -1e9;
  int r1_idx = -1, r2_idx = -1;

  for (int i = 0; i < numRoutes; ++i) {
    for (int j = i + 1; j < numRoutes; ++j) {
      int sizeSum = (int)(routes[i].getCustomers().size() +
                          routes[j].getCustomers().size());
      if (sizeSum > 30 || sizeSum == 0)
        continue;
      double dist = calculateEuclideanDistance(computeCentroid(routes[i]),
                                               computeCentroid(routes[j]));
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

  // ── Build customer pool ──────────────────────────────────────────────────
  std::vector<int> customer_pool;
  customer_pool.reserve(30);
  for (int c : routes[r1_idx].getCustomers())
    customer_pool.push_back(c);
  for (int c : routes[r2_idx].getCustomers())
    customer_pool.push_back(c);

  double oldTotalDist =
      routes[r1_idx].getTotalDistance() + routes[r2_idx].getTotalDistance();

  // ── Init 2 new routes ────────────────────────────────────────────────────
  auto vehicleType = routes[r1_idx].getVehicle();
  std::vector<Route> newRoutes;
  newRoutes.reserve(2);
  newRoutes.push_back(Route(0, vehicleType, instance));
  newRoutes.push_back(Route(0, vehicleType, instance));

  // ── [OPT-1] Precompute nearest station per customer BEFORE seeding ─────────
  // Build TRƯỚC removeSeed() để seeds cũng có entry trong cache.
  // findNearestStation() là O(S) per call — cache cho O(P*S) tổng,
  // thay vì O(P*N*S) nếu gọi trong triple-nested loop.
  std::unordered_map<int, int> nearStatCache;
  nearStatCache.reserve(32);
  for (int cid : customer_pool)
    nearStatCache[cid] = findNearestStation(cid);

  // ── Furthest-apart seeding ───────────────────────────────────────────────
  {
    int seed1 = -1, seed2 = -1;
    double maxDist = -1.0;
    for (int i = 0; i < (int)customer_pool.size(); ++i) {
      for (int j = i + 1; j < (int)customer_pool.size(); ++j) {
        double d = instance->getDistance(customer_pool[i], customer_pool[j]);
        if (d > maxDist) {
          maxDist = d;
          seed1 = customer_pool[i];
          seed2 = customer_pool[j];
        }
      }
    }
    auto removeSeed = [&](int seedId) {
      auto it = std::find(customer_pool.begin(), customer_pool.end(), seedId);
      if (it != customer_pool.end()) {
        *it = customer_pool.back();
        customer_pool.pop_back();
      }
    };
    if (seed1 != -1 && seed2 != -1) {
      newRoutes[0].addNode(seed1, 1);
      newRoutes[1].addNode(seed2, 1);
      removeSeed(seed1);
      removeSeed(seed2);
    } else if (!customer_pool.empty()) {
      newRoutes[0].addNode(customer_pool[0], 1);
      customer_pool[0] = customer_pool.back();
      customer_pool.pop_back();
    }
  }

  // ── [OPT-2] Precompute distance threshold for B/C options ────────────────
  // Options B & C (station-assisted) involve a Route copy + evaluate — O(N).
  // Guard them with a cheap distance pre-filter: only attempt if direct
  // insertion cost is "close enough" to potentially beat the current best.
  // Also cache route distances to avoid repeated getTotalDistance() calls.

  struct MergeCandidate {
    int custId = -1;
    int routeIdx = -1;
    size_t pos = 0;
    double cost = 1e18;
    int stationId = -1;
    bool statBefore = true;
  };

  bool construction_failed = false;

  while (!customer_pool.empty() && !construction_failed) {
    MergeCandidate globalBest;

    // [OPT-3] Cache current route distances — getTotalDistance() calls
    // evaluate() internally; caching avoids redundant calls inside inner loop.
    double routeDist[2] = {newRoutes[0].getTotalDistance(),
                           newRoutes[1].getTotalDistance()};
    double routeCap[2] = {newRoutes[0].getVehicle()->getCapacity(),
                          newRoutes[1].getVehicle()->getCapacity()};

    for (int cust_id : customer_pool) {
      double demand = demandById_[cust_id];
      int nearStat = nearStatCache.count(cust_id) ? nearStatCache[cust_id] : -1;

      for (int r_idx = 0; r_idx < 2; ++r_idx) {
        // Capacity pre-filter (O(1))
        if (newRoutes[r_idx].getTotalDemand() + demand > routeCap[r_idx])
          continue;

        const int routeSz = (int)newRoutes[r_idx].size();

        for (int pos = 1; pos < routeSz; ++pos) {

          // ── Option A: direct insertion (checkInsertionCost, no copy) ──────
          InsertionResult resA =
              newRoutes[r_idx].checkInsertionCost(cust_id, pos);
          if (resA.isFeasible && resA.deltaDistance < globalBest.cost) {
            globalBest = {cust_id, r_idx, (size_t)pos, resA.deltaDistance,
                          -1,      true};
          }

          // ── Options B & C: station-assisted ──────────────────────────────
          // [OPT-4] Two-stage filter before paying Route copy cost:
          //   Stage 1 (O(1)): rough distance bound — detour through station
          //                   must be less than current globalBest cost
          //   Stage 2: only then do the O(N) Route copy + evaluate
          if (nearStat == -1)
            continue;

          // Cheap detour estimate for the station
          int prevNode = newRoutes[r_idx].getNodeAt(pos - 1);
          int nextNode = newRoutes[r_idx].getNodeAt(pos);
          double directEdge = instance->getDistance(prevNode, nextNode);

          // B: [station, cust] — detour =
          // d(prev,stat)+d(stat,cust)+d(cust,next)-d(prev,next)
          double detourB = instance->getDistance(prevNode, nearStat) +
                           instance->getDistance(nearStat, cust_id) +
                           instance->getDistance(cust_id, nextNode) -
                           directEdge;
          if (detourB < globalBest.cost) {
            Route copyB = newRoutes[r_idx];
            copyB.addNode(cust_id, pos);
            copyB.addNode(nearStat, pos); // station inserted BEFORE cust
            copyB.evaluate();
            if (copyB.isFeasible()) {
              double c = copyB.getTotalDistance() - routeDist[r_idx];
              if (c < globalBest.cost)
                globalBest = {cust_id, r_idx, (size_t)pos, c, nearStat, true};
            }
          }

          // C: [cust, station] — detour =
          // d(prev,cust)+d(cust,stat)+d(stat,next)-d(prev,next)
          double detourC = instance->getDistance(prevNode, cust_id) +
                           instance->getDistance(cust_id, nearStat) +
                           instance->getDistance(nearStat, nextNode) -
                           directEdge;
          if (detourC < globalBest.cost) {
            Route copyC = newRoutes[r_idx];
            copyC.addNode(cust_id, pos);
            copyC.addNode(nearStat, pos + 1); // station inserted AFTER cust
            copyC.evaluate();
            if (copyC.isFeasible()) {
              double c = copyC.getTotalDistance() - routeDist[r_idx];
              if (c < globalBest.cost)
                globalBest = {cust_id, r_idx, (size_t)pos, c, nearStat, false};
            }
          }
        }
      }
    }

    if (globalBest.routeIdx == -1) {
      construction_failed = true;
      break;
    }

    // Apply best move
    auto &targetRoute = newRoutes[globalBest.routeIdx];
    if (globalBest.stationId == -1) {
      targetRoute.addNode(globalBest.custId, globalBest.pos);
    } else if (globalBest.statBefore) {
      targetRoute.addNode(globalBest.custId, globalBest.pos);
      targetRoute.addNode(globalBest.stationId, globalBest.pos);
    } else {
      targetRoute.addNode(globalBest.custId, globalBest.pos);
      targetRoute.addNode(globalBest.stationId, globalBest.pos + 1);
    }
    targetRoute.evaluate();

    // [OPT-4] swap-and-pop O(1) removal from pool
    auto it = std::find(customer_pool.begin(), customer_pool.end(),
                        globalBest.custId);
    if (it != customer_pool.end()) {
      *it = customer_pool.back();
      customer_pool.pop_back();
    }
    // nearStatCache entry can stay — no harm if key no longer in pool
  }

  if (construction_failed)
    return false;

  // ── Validate & collect final routes ─────────────────────────────────────
  double newTotalDist = 0.0;
  std::vector<Route> finalRoutes;
  finalRoutes.reserve(2);

  for (auto &r : newRoutes) {
    r.evaluate();
    if (!r.isFeasible())
      return false;
    if (!r.getCustomers().empty()) {
      finalRoutes.push_back(r);
      newTotalDist += r.getTotalDistance();
    }
  }
  if (finalRoutes.empty())
    return false;

  bool distImproved = (newTotalDist < oldTotalDist * 0.997);
  bool vehicleReduced = (finalRoutes.size() < 2);

  if (vehicleReduced || (finalRoutes.size() == 2 && distImproved)) {
    solution.removeRoute(std::max(r1_idx, r2_idx));
    solution.removeRoute(std::min(r1_idx, r2_idx));
    for (const auto &r : finalRoutes)
      solution.addRoute(r);
    return true;
  }
  return false;
}

bool LocalSearch::segmentCrossExchangeForVehicleReduction(Solution &solution) {
  auto &routes = solution.getRoutes();
  int numRoutes = (int)routes.size();
  if (numRoutes < 2)
    return false;

  std::vector<int> candidateVictims;
  for (int r = 0; r < numRoutes; ++r) {
    int cnt = (int)routes[r].getCustomers().size();
    if (cnt > 0 && cnt <= 15) {
      candidateVictims.push_back(r);
    }
  }
  if (candidateVictims.empty())
    return false;

  std::sort(candidateVictims.begin(), candidateVictims.end(),
            [&](int a, int b) {
              return routes[a].getCustomers().size() <
                     routes[b].getCustomers().size();
            });

  const double vehCap = instance->getVehicleCapacity();
  bool globallyImproved = false;

  for (int victimIdx : candidateVictims) {
    // [FIX LOGIC MỚI]: Chốt chặn Stale Victim
    // Cập nhật lại size thực tế vì Victim này có thể đã nhận thêm khách
    // từ các Victim bị "rút ruột" trước đó trong cùng mảng candidateVictims
    if (routes[victimIdx].getCustomers().size() > 15) {
      continue; // Bỏ qua, không rút ruột xe đã lớn
    }

    bool anyProgress = true;

    while (anyProgress) {
      anyProgress = false;
      auto &vRoute = routes[victimIdx];
      int vSize = (int)vRoute.size();

      int maxExtract = std::min(3, vSize - 2);
      for (int vLen = maxExtract; vLen >= 1 && !anyProgress; --vLen) {
        for (int vi = 1; vi <= vSize - 1 - vLen && !anyProgress; ++vi) {

          std::vector<int> ejectCusts;
          double vDem = 0;
          bool allCust = true;

          for (int k = 0; k < vLen; ++k) {
            int nid = vRoute.getNodeAt(vi + k);
            if (nodeTypeById_[nid] != NodeType::CUSTOMER) {
              allCust = false;
              break;
            }
            ejectCusts.push_back(nid);
            vDem += demandById_[nid];
          }
          if (!allCust)
            continue;

          Route candV = vRoute;
          for (int k = vLen - 1; k >= 0; --k) {
            candV.removeNode(vi + k);
          }
          candV.evaluate();

          if (!candV.isFeasible())
            continue;

          for (int ri = 0; ri < numRoutes && !anyProgress; ++ri) {
            if (ri == victimIdx)
              continue;

            Route candR = routes[ri];
            if (candR.getTotalDemand() + vDem > vehCap)
              continue;

            bool insertOk = false;

            if (vLen == 1) {
              int cId = ejectCusts[0];
              for (size_t pos = 1; pos < candR.size() && !insertOk; ++pos) {
                if (candR.canPossiblyInsert(cId, pos)) {
                  InsertionResult res = candR.checkInsertionCost(cId, pos);
                  if (res.isFeasible) {
                    candR.addNode(cId, pos);
                    candR.evaluate();
                    insertOk = candR.isFeasible();

                    if (!insertOk) {
                      candR.removeNode(pos); // Restore an toàn
                    }
                  }
                }
              }
              // Nếu direct fail, đẩy nguyên candR cho reconstruct xử lý.
              // Nếu reconstruct cũng fail, candR sẽ tự bị hủy khi hết vòng lặp
              // ri
              if (!insertOk) {
                insertOk =
                    reconstructRouteWithTwoStations(candR, ejectCusts, 2);
              }
            } else {
              insertOk = reconstructRouteWithTwoStations(candR, ejectCusts, 2);
            }

            if (insertOk) {
              routes[victimIdx] = candV;
              routes[ri] = candR;
              anyProgress = true;
              globallyImproved = true;

              if (candV.getCustomers().empty()) {
                solution.removeRoute(victimIdx);
                solution.evaluateRoutes();
                return true;
              }

              break;
            }
          }
        }
      }
    }
  }
  return globallyImproved;
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
  if (searchContext_.isValid)
    return;

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
    struct RN {
      int idx;
      double dist2;
    };
    std::vector<RN> ranked;
    ranked.reserve(numRoutes - 1);
    for (int r2 = 0; r2 < numRoutes; ++r2) {
      if (r2 == r1)
        continue;
      double dx =
          searchContext_.centroids[r1].x - searchContext_.centroids[r2].x;
      double dy =
          searchContext_.centroids[r1].y - searchContext_.centroids[r2].y;
      ranked.push_back({r2, dx * dx + dy * dy});
    }
    int keep = std::min(MAX_ROUTE_NEIGHBORS, (int)ranked.size());
    std::partial_sort(
        ranked.begin(), ranked.begin() + keep, ranked.end(),
        [](const RN &a, const RN &b) { return a.dist2 < b.dist2; });
    for (int k = 0; k < keep; ++k)
      searchContext_.neighborLists[r1].push_back(ranked[k].idx);
  }

  searchContext_.removalRankings.assign(numRoutes, {});
  searchContext_.rankingDirty.assign(numRoutes, true);
  searchContext_.centroidDirty.assign(numRoutes, false);

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

  const auto &nodes = route.getNodes();
  const auto &states = route.getStates();

  auto customerNode =
      std::static_pointer_cast<Customer>(instance->getNodeById(nodeId));

  for (size_t pos = 1; pos < nodes.size(); ++pos) {
    int prev = nodes[pos - 1];

    double distBefore = instance->getDistance(prev, nodes[pos]);
    double distAfter = instance->getDistance(prev, nodeId) +
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
  std::partial_sort(candidates.begin(), candidates.begin() + k,
                    candidates.end());

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

    int k =
        std::min(20, (int)distances.size()); // [PRUNE-5] cap at 20 (was
                                             // K_NEIGHBORS=40) — quality stable
    std::partial_sort(distances.begin(), distances.begin() + k,
                      distances.end());

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
    avgDistance_ = 0.0;
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

  avgDistance_ = totalDistance / pairCount;
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
  const auto &nodes = route.getNodes();
  const int routeSize = (int)nodes.size();

  // [SPEED-2] thread_local set — zero heap alloc per call in hot loop
  static thread_local std::unordered_set<int> neighborSet;
  neighborSet.clear();
  neighborSet.insert(nearestNeighbors.begin(), nearestNeighbors.end());

  // [SPEED-3] thread_local isCandidate — zero heap alloc, just assign + clear
  static thread_local std::vector<bool> isCandidate;
  isCandidate.assign(routeSize + 1, false);
  for (int pos = 0; pos < routeSize; ++pos) {
    if (neighborSet.count(nodes[pos])) {
      if (pos > 0)
        isCandidate[pos] = true;
      if (pos + 1 < routeSize)
        isCandidate[pos + 1] = true;
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
  auto customerNode =
      std::static_pointer_cast<Customer>(instance->getNodeById(nodeId));
  const double twReady = customerNode->getReadyTime();
  const double twDue = customerNode->getDueDate();

  bool foundAny = false;
  for (int pos = 1; pos < routeSize; ++pos) {
    if (!isCandidate[pos])
      continue;
    foundAny = true;

    int prev = nodes[pos - 1];
    double distBefore = instance->getDistance(prev, nodes[pos]);
    double distAfter = instance->getDistance(prev, nodeId) +
                       instance->getDistance(nodeId, nodes[pos]);
    double detour = distAfter - distBefore;

    double arrivalTime =
        states[pos - 1].departureTime + instance->getTime(prev, nodeId);
    double twPenalty = 0.0;
    if (arrivalTime > twDue)
      twPenalty = 1000.0;
    else if (arrivalTime < twReady)
      twPenalty = 0.5;

    candidates.push_back({(size_t)pos, detour * (1.0 + twPenalty)});
  }

  if (!foundAny) {
    return findBestInsertionPositions_TimeAware(route, nodeId, topK);
  }

  if (candidates.empty())
    return {};

  int k = std::min(topK, (int)candidates.size());
  std::partial_sort(candidates.begin(), candidates.begin() + k,
                    candidates.end());

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

  const auto &routes = solution.getRoutes();
  const auto &r1 = routes[move.routeIdx1];
  const bool isIntraMove = (move.routeIdx1 == move.routeIdx2);
  const auto &r2 = isIntraMove ? r1 : routes[move.routeIdx2];

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
    if (move.nodeIdx1 < move.nodeIdx2)
      insertPos--;
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

  result.isFeasible = true;
  result.distanceDelta = distanceDelta;
  result.objectiveDelta = distanceDelta;

  return result;
}

int LocalSearch::findNearestStation(int nodeId) const {
  if (stationIds.empty())
    return -1;

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
  const auto &minReq = route.getMinBatteryReq();
  const auto &states = route.getStates();

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
  Route backupTo = routeTo;

  int nodeId = routeFrom.getNodeAt(fromNodeIdx);
  if (nodeId < 0)
    return false;

  routeFrom.removeNode(fromNodeIdx);
  routeTo.addNode(nodeId, insertPos);

  routeTo.evaluate();
  routeFrom.evaluate();

  if (routeTo.isFeasible() && routeFrom.isFeasible()) {
    return true;
  }

  if (!routeTo.isFeasible()) {
    // [FIX-1] Multi-station relocation: try top-K stations ranked by detour
    // cost instead of only the single nearest station.
    routeTo = backupTo;

    // Build candidate list: rank all stations by detour through insertion edge
    struct StationCandidate {
      int id;
      double detour;
    };
    std::vector<StationCandidate> candidates;
    candidates.reserve(stationIds.size());

    const auto &toNodes = backupTo.getNodes();
    int prevNode = (insertPos > 0 && insertPos <= toNodes.size())
                       ? toNodes[insertPos - 1]
                       : -1;
    int nextNode = (insertPos < toNodes.size()) ? toNodes[insertPos] : -1;
    double directDist = (prevNode >= 0 && nextNode >= 0)
                            ? instance->getDistance(prevNode, nextNode)
                            : 0.0;

    for (int sid : stationIds) {
      // Skip station already adjacent to insertPos
      if (sid == prevNode || sid == nextNode)
        continue;

      double detour =
          (prevNode >= 0 ? instance->getDistance(prevNode, sid) : 0.0) +
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
    Route bestRoute = backupTo; // initialize with valid Route (no default ctor)
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
            bestRoute = testRoute;
            found = true;
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
            bestRoute = testRoute;
            found = true;
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
  routeTo = backupTo;
  return false;
}

int LocalSearch::removeRedundantStations(Route &route) {
  const double EPSILON = 1e-9;
  int removalCount = 0;

  bool stationRemoved = true;
  while (stationRemoved) {
    stationRemoved = false;
    route.evaluate();

    if (!route.isFeasible())
      break;

    const auto &nodes = route.getNodes();
    const auto &states = route.getStates();
    const auto &minBatteryReq = route.getMinBatteryReq();

    for (int i = static_cast<int>(nodes.size()) - 2; i > 0; --i) {
      int nodeId = nodes[i];
      // [OPT-2]
      if (nodeTypeById_[nodeId] != NodeType::STATION)
        continue;

      int prevNodeId = nodes[i - 1];
      int nextNodeId = nodes[i + 1];

      double distToSkip = instance->getDistance(prevNodeId, nextNodeId);
      double energyToSkip =
          distToSkip * route.getVehicle()->getEnergyConsumptionRate();

      double batteryAtPrev = states[i - 1].remainingBattery;

      if (batteryAtPrev < energyToSkip - EPSILON)
        continue;

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
    if (route.size() <= 2)
      continue;
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
    if (node->getId() > maxId)
      maxId = node->getId();
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

// =============================================================================
// reconstructRouteWithTwoStations — 3-Phase Route Reconstruction
// =============================================================================
//
// Mục đích: Sau khi forceReduceVehicles merge routes, sequence cũ là garbage.
// Rebuild hoàn toàn 1 route từ tập customers đã biết.
//
// Phase 1 — Or-opt seeded: giữ sequence gốc của route, insert customers mới
//           vào vị trí tốt nhất thay vì rebuild từ đầu. Tránh NN dead-end.
//
// Phase 2 — Optimal k-station placement: với sequence đã có, enumerate tất cả
//           (i,j) pairs. Top-2 stations per edge (pruning S²→4). O(N²×4).
//           Thử k=0 → k=1 → k=2, dừng ở k thấp nhất feasible.
//           Build route bằng cách chèn stations vào sequence cố định —
//           không shift index vì ta pass danh sách (pos, stationId) vào
//           buildRouteFromSeq helper một lần duy nhất.
//
// Phase 3 — 2-opt light: swap customers trong seq, giữ nguyên station positions
//           từ Phase 2. Chỉ re-evaluate sau swap, không re-run full Phase 2.
//           O(N²) per pass × 2 passes = O(N²). Tránh O(N⁴) của bản cũ.
//
// Complexity: Phase1 O(N×M) [N=existing, M=new], Phase2 O(N²×4×O(N)) = O(N³×4),
//             Phase3 O(N²×2). Với N=46: ~4500×4×46≈830K + 2×46²≈4K ops → <10ms.
// =============================================================================

bool LocalSearch::reconstructRouteWithTwoStations(
    Route &route, const std::vector<int> &newCustomers, int maxStations) const {
  if (newCustomers.empty())
    return false;

  const double vehCap = instance->getVehicleCapacity();
  const int depotId = 0;

  // ── Collect full customer set (existing + new) ────────────────────────────
  std::vector<int> existingCusts;
  for (int nid : route.getNodes())
    if (nodeTypeById_[nid] == NodeType::CUSTOMER)
      existingCusts.push_back(nid);

  // Build union, dedup
  std::vector<int> allCusts = existingCusts;
  for (int c : newCustomers) {
    bool dup = false;
    for (int e : existingCusts)
      if (e == c) {
        dup = true;
        break;
      }
    if (!dup)
      allCusts.push_back(c);
  }

  if (allCusts.empty())
    return false;

  // Capacity pre-check
  double totalDemand = 0.0;
  for (int c : allCusts)
    totalDemand += demandById_[c];
  if (totalDemand > vehCap)
    return false;

  const int N = static_cast<int>(allCusts.size());

  // ── Phase 1: Or-opt seeded sequence ───────────────────────────────────────
  // Seed: sequence gốc của route (đã feasible về TW), insert new customers
  // vào vị trí cheapest trong sequence gốc (greedy by deltaDistance).
  // Không rebuild từ đầu → tránh NN dead-end với TW chặt.

  std::vector<int> seq = existingCusts; // sequence gốc

  // ── Phase 1: TW-aware insertion ───────────────────────────────────────────
  // Thay vì chỉ minimize deltaDistance (địa lý), tìm vị trí insert mà:
  //   1. Không gây TW violation cho cNew và tất cả customers phía sau
  //   2. Trong các vị trí feasible, chọn min deltaDistance
  //
  // Approach: simulate arrival times với lần lượt từng vị trí.
  // Đây là O(N²) per new customer — acceptable cho N≤50.
  //
  // Nếu không có vị trí nào feasible về TW → fallback về min deltaDistance
  // (để Phase 2 2-opt có cơ hội fix sau, hoặc ít nhất không worse hơn trước)

  // Helper: simulate arrival time tại node `to` sau khi đến từ `from` lúc `t`
  auto arrivalAt = [&](int from, int to, double t) -> double {
    auto nd = instance->getNodeById(to);
    double travel = instance->getDistance(from, to); // dist = time (speed=1)
    double arrive = t + travel;
    return std::max(arrive, (double)nd->getReadyTime()); // wait if early
  };

  for (int cNew : newCustomers) {
    bool alreadyIn = false;
    for (int s : seq)
      if (s == cNew) {
        alreadyIn = true;
        break;
      }
    if (alreadyIn)
      continue;

    auto ndNew = instance->getNodeById(cNew);
    int szSeq = (int)seq.size();

    double bestFeasibleDelta = 1e18;
    double bestAnyDelta = 1e18;
    int bestFeasiblePos = -1;
    int bestAnyPos = 0;

    // Simulate arrival times through existing seq (without cNew)
    // arrTime[i] = arrival at seq[i] following depot→seq[0]→...→seq[i]
    std::vector<double> arrTime(szSeq);
    {
      double t = 0.0;
      int prev = depotId;
      for (int i = 0; i < szSeq; ++i) {
        arrTime[i] = arrivalAt(prev, seq[i], t);
        auto nd = instance->getNodeById(seq[i]);
        t = arrTime[i] + nd->getServiceTime();
        prev = seq[i];
      }
    }

    for (int pos = 0; pos <= szSeq; ++pos) {
      int prevId = (pos > 0) ? seq[pos - 1] : depotId;
      int nextId = (pos < szSeq) ? seq[pos] : depotId;

      // Delta distance for inserting cNew at pos
      double delta = instance->getDistance(prevId, cNew) +
                     instance->getDistance(cNew, nextId) -
                     instance->getDistance(prevId, nextId);

      if (delta < bestAnyDelta) {
        bestAnyDelta = delta;
        bestAnyPos = pos;
      }

      // TW feasibility check for cNew itself
      double tPrev =
          (pos > 0) ? (arrTime[pos - 1] +
                       instance->getNodeById(seq[pos - 1])->getServiceTime())
                    : 0.0;
      double arrCNew = tPrev + instance->getDistance(prevId, cNew);
      double waitCNew = std::max(0.0, (double)ndNew->getReadyTime() - arrCNew);
      double depCNew = std::max(arrCNew, (double)ndNew->getReadyTime()) +
                       ndNew->getServiceTime();

      if (arrCNew > ndNew->getDueDate())
        continue; // TW fail for cNew

      // Check TW propagation: customers from pos onward get pushed by ΔT
      double tNext = depCNew;
      bool twOk = true;
      for (int k = pos; k < szSeq; ++k) {
        double arrK = tNext + instance->getDistance(
                                  (k == pos) ? cNew : seq[k - 1], seq[k]);
        auto ndK = instance->getNodeById(seq[k]);
        if (arrK > ndK->getDueDate()) {
          twOk = false;
          break;
        }
        tNext =
            std::max(arrK, (double)ndK->getReadyTime()) + ndK->getServiceTime();
      }

      if (twOk && delta < bestFeasibleDelta) {
        bestFeasibleDelta = delta;
        bestFeasiblePos = pos;
      }
    }

    // Use TW-feasible position if found, otherwise fallback to min distance
    int insertPos = (bestFeasiblePos >= 0) ? bestFeasiblePos : bestAnyPos;
    seq.insert(seq.begin() + insertPos, cNew);

    // Update arrTime for next new customer (rebuild after insertion)
    // (seq changed, rebuild arrTime on next iteration's pass)
  }

  // seq bây giờ chứa đúng N customers, ordered hợp lý về TW + địa lý

  // ── Phase 2: Optimal k-station placement ─────────────────────────────────
  // Không enumerate indices shift: ta build route theo cách truyền
  // danh sách (customer_seq, station_at_positions[]) vào một lần.
  //
  // station_insertions: sorted list of {pos_in_seq, stationId}
  // pos 0 = before seq[0], pos N = after seq[N-1]
  //
  // buildRouteFromSeq: O(N), tạo Route từ seq + station_insertions

  auto vehicle = route.getVehicle();

  // Helper: build Route từ seq + list of (pos, sid) sorted by pos ascending
  struct StationIns {
    int pos;
    int sid;
  };

  auto buildRouteFromSeq = [&](const std::vector<int> &s,
                               const std::vector<StationIns> &sIns) -> Route {
    Route r(route.getId(), vehicle, instance);
    int routePos = 1; // depot at 0, first addNode at 1
    int si = 0;       // index vào sIns

    for (int i = 0; i <= (int)s.size(); ++i) {
      // Insert stations với pos == i trước customer i
      while (si < (int)sIns.size() && sIns[si].pos == i) {
        r.addNode(sIns[si].sid, routePos++);
        ++si;
      }
      if (i < (int)s.size())
        r.addNode(s[i], routePos++);
    }
    r.evaluate();
    return r;
  };

  // Top-2 stations cho edge (prevNode → nextNode) by detour
  auto top2Stations = [&](int prev, int next) -> std::vector<int> {
    struct SC {
      int id;
      double det;
    };
    std::vector<SC> sc;
    sc.reserve(stationIds.size());
    double direct = instance->getDistance(prev, next);
    for (int sid : stationIds) {
      double det = instance->getDistance(prev, sid) +
                   instance->getDistance(sid, next) - direct;
      sc.push_back({sid, det});
    }
    int K = std::min(2, (int)sc.size());
    std::partial_sort(sc.begin(), sc.begin() + K, sc.end(),
                      [](const SC &a, const SC &b) { return a.det < b.det; });
    std::vector<int> res;
    for (int k = 0; k < K; ++k)
      res.push_back(sc[k].id);
    return res;
  };

  // prev/next node cho mỗi gap position trong seq
  auto gapPrev = [&](int pos) -> int {
    return (pos > 0) ? seq[pos - 1] : depotId;
  };
  auto gapNext = [&](int pos) -> int { return (pos < N) ? seq[pos] : depotId; };

  std::optional<Route> bestRoute;
  bool found = false;
  double bestCost = 1e18;

  // k=0: no station
  {
    Route r = buildRouteFromSeq(seq, {});
    if (r.isFeasible()) {
      bestRoute = r;
      found = true;
      bestCost = r.getTotalDistance();
    }
  }

  // k=1: 1 station, enumerate N+1 positions × top-2 stations
  if (!found && maxStations >= 1) {
    for (int i = 0; i <= N; ++i) {
      for (int s1 : top2Stations(gapPrev(i), gapNext(i))) {
        Route r = buildRouteFromSeq(seq, {{i, s1}});
        if (r.isFeasible()) {
          double cost = r.getTotalDistance();
          if (cost < bestCost) {
            bestCost = cost;
            bestRoute = r;
            found = true;
          }
        }
      }
    }
  }

  // k=2: 2 stations, enumerate (i<j) × top-2 × top-2 = N²/2 × 4
  if (!found && maxStations >= 2) {
    for (int i = 0; i <= N; ++i) {
      auto s1List = top2Stations(gapPrev(i), gapNext(i));
      for (int j = i + 1; j <= N; ++j) {
        auto s2List = top2Stations(gapPrev(j), gapNext(j));
        for (int s1 : s1List) {
          for (int s2 : s2List) {
            Route r = buildRouteFromSeq(seq, {{i, s1}, {j, s2}});
            if (r.isFeasible()) {
              double cost = r.getTotalDistance();
              if (cost < bestCost) {
                bestCost = cost;
                bestRoute = r;
                found = true;
              }
            }
          }
        }
      }
    }
  }

  if (!found) {
#ifdef DEBUG_FRV
    // Diagnose tại sao k=0,1,2 đều fail với sequence này
    {
      Route r0 = buildRouteFromSeq(seq, {});
      auto diag = [&](const Route &r, const char *label) {
        std::cout << "  [recon-diag] " << label
                  << " feasible=" << r.isFeasible() << " dist=" << std::fixed
                  << std::setprecision(1) << r.getTotalDistance();
        // Check individual constraint violations
        // (assumes Route exposes these; if not, just print dist/feasible)
        std::cout << "\n";
      };
      diag(r0, "k=0");
      // Try best k=1
      double bestK1 = 1e18;
      bool foundK1 = false;
      for (int i = 0; i <= N && !foundK1; ++i)
        for (int s1 : top2Stations(gapPrev(i), gapNext(i))) {
          Route r = buildRouteFromSeq(seq, {{i, s1}});
          if (r.getTotalDistance() < bestK1) {
            bestK1 = r.getTotalDistance();
            diag(r, "k=1-best");
          }
          if (r.isFeasible()) {
            foundK1 = true;
          }
        }
      if (!foundK1) {
        Route rK1 = buildRouteFromSeq(
            seq, {{N / 2, top2Stations(gapPrev(N / 2), gapNext(N / 2))[0]}});
        std::cout << "  [recon-diag] k=1 (mid) dist=" << rK1.getTotalDistance()
                  << " N=" << N << " newCusts=";
        for (int c : newCustomers)
          std::cout << c << " ";
        std::cout << "\n";
      }
    }
#endif
    return false;
  }

  // ── Phase 3: 2-opt light ──────────────────────────────────────────────────
  // Swap customers trong seq, re-evaluate bestRoute inline.
  // KHÔNG re-run Phase 2 — giữ nguyên station count/structure từ Phase 2.
  // Chỉ cần re-build với same station positions (recalculated for new seq).
  //
  // Để tránh O(N⁴): sau mỗi promising swap (gain > threshold),
  // chỉ thử lại Phase 2 ở đúng k tìm được (không thử k=0,1,2 lại từ đầu).
  // Max 2 passes.

  // Detect k từ bestRoute (đếm stations)
  int bestK = 0;
  for (int nid : bestRoute.value().getNodes())
    if (nodeTypeById_[nid] == NodeType::STATION)
      ++bestK;

  const int MAX_PASSES = 2;
  bool improved = true;

  for (int pass = 0; pass < MAX_PASSES && improved; ++pass) {
    improved = false;

    for (int i = 0; i < N - 1; ++i) {
      for (int j = i + 2; j < N; ++j) {
        // Ước tính gain từ 2-opt (i,j): reverse seq[i+1..j]
        int b = seq[i];
        int c = seq[i + 1];
        int d = seq[j];
        int e = (j < N - 1) ? seq[j + 1] : depotId;

        double gain =
            (instance->getDistance(b, c) + instance->getDistance(d, e)) -
            (instance->getDistance(b, d) + instance->getDistance(c, e));

        if (gain < 1.0)
          continue; // threshold: net gain > 1 unit

        // Apply swap to seq
        std::vector<int> newSeq = seq;
        std::reverse(newSeq.begin() + i + 1, newSeq.begin() + j + 1);

        // Re-run Phase 2 chỉ với k = bestK (không thử k lớn hơn/nhỏ hơn)
        // Cập nhật gapPrev/gapNext cho newSeq
        auto newGapPrev = [&](int pos) -> int {
          return (pos > 0) ? newSeq[pos - 1] : depotId;
        };
        auto newGapNext = [&](int pos) -> int {
          return (pos < N) ? newSeq[pos] : depotId;
        };

        bool swapFound = false;
        double swapCost = 1e18;
        std::optional<Route> swapRoute;

        if (bestK == 0) {
          Route r = buildRouteFromSeq(newSeq, {});
          if (r.isFeasible() && r.getTotalDistance() < bestCost) {
            swapCost = r.getTotalDistance();
            swapRoute = r;
            swapFound = true;
          }
        } else if (bestK == 1) {
          for (int pi = 0; pi <= N; ++pi) {
            for (int s1 : top2Stations(newGapPrev(pi), newGapNext(pi))) {
              Route r = buildRouteFromSeq(newSeq, {{pi, s1}});
              if (r.isFeasible() && r.getTotalDistance() < swapCost) {
                swapCost = r.getTotalDistance();
                swapRoute = r;
                swapFound = true;
              }
            }
          }
        } else {
          // bestK == 2
          for (int pi = 0; pi <= N; ++pi) {
            auto sl1 = top2Stations(newGapPrev(pi), newGapNext(pi));
            for (int pj = pi + 1; pj <= N; ++pj) {
              auto sl2 = top2Stations(newGapPrev(pj), newGapNext(pj));
              for (int s1 : sl1)
                for (int s2 : sl2) {
                  Route r = buildRouteFromSeq(newSeq, {{pi, s1}, {pj, s2}});
                  if (r.isFeasible() && r.getTotalDistance() < swapCost) {
                    swapCost = r.getTotalDistance();
                    swapRoute = r;
                    swapFound = true;
                  }
                }
            }
          }
        }

        if (swapFound && swapCost < bestCost - 1e-6) {
          bestCost = swapCost;
          bestRoute = swapRoute;
          seq = newSeq;
          improved = true;
        }
      }
    }
  }

  route = bestRoute.value();
  return true;
}

// =============================================================================
// forceReduceVehicles — Post-ALNS Vehicle Reduction
// =============================================================================
//
// Gọi sau khi ALNS converge tại K vehicles, target K-1.
//
// Victim selection: fast insertability proxy
//   Score = 0.4×twFlex + 0.35×sizeFactor + 0.25×demandFactor
//   twFlex = avg (dueDate-readyTime)/horizon — customer TW rộng → dễ insert
//   Không tính insertability thực O(R²×N²) — quá tốn kém cho outer loop.
//   Trade-off: proxy đủ tốt vì twFlex correlated với số feasible positions.
//
// Redistribute: regret-2 insertion (giống tryEliminateSmallestRoute)
//   Sau khi tất cả customers inserted → reconstructRouteWithTwoStations
//   cho mỗi route bị modify (rebuild sequence + optimal 2-station placement).
//
// maxAttempts: 10-15 đủ vì mỗi attempt đã có reconstruct bên trong.
// =============================================================================

// =============================================================================
// forceReduceVehicles
// =============================================================================
// Fixes vs v1:
//   [FIX-A] Multi-victim: thử tất cả victim candidates sorted by score,
//           không bỏ cuộc sau 1 victim duy nhất.
//   [FIX-B] Pre-check: trước khi chạy attempts, verify mỗi ejected customer
//           có ít nhất 1 feasible position trong remaining routes.
//           O(N × R × positions) — fast, tránh waste 100% attempts.
//   [FIX-C] Regret-2 với energy-approximate cost: khi direct insertion fail
//           về energy, tính approximate cost = deltaDistance + cheapest station
//           detour. Customer có regret cao (chỉ fit vào 1 nơi) được insert
//           trước.
//   [FIX-D] maxAttempts per victim = 80 (không phải 12 total).
//           Với 100 customers, TW=120, 80 orderings đủ diverse.
// =============================================================================

bool LocalSearch::forceReduceVehicles(Solution &solution, int targetVehicles,
                                      int maxAttempts,
                                      std::mt19937 &rng) const {

  int currentVeh = solution.getNumRoutes();
  if (currentVeh <= targetVehicles || currentVeh - targetVehicles != 1)
    return false;

  const double vehCap = instance->getVehicleCapacity();
  const double horizon = instance->getNodeById(0)->getDueDate();
  const int depotId = 0;

  // ── Victim scoring ────────────────────────────────────────────────────────
  // Score cao → customers dễ redistribute sang routes khác.
  // Dùng proxy nhanh (không chạy actual insertion):
  //   TW flexibility: TW rộng → dễ fit vào nhiều routes
  //   size factor:    route nhỏ → ít customers cần redistribute
  //   demand factor:  demand thấp → routes khác có room
  //   spatial factor: centroid gần centroid các routes khác → ít TW conflict
  auto victimScore = [&](int routeIdx) -> double {
    const auto &r = solution.getRoutes()[routeIdx];
    const auto &custs = r.getCustomers();
    if (custs.empty())
      return -1e9;
    int n = (int)custs.size();

    double avgTW = 0.0;
    double cx = 0.0, cy = 0.0; // centroid of victim
    for (int c : custs) {
      auto node = instance->getNodeById(c);
      avgTW +=
          (node->getDueDate() - node->getReadyTime()) / std::max(1.0, horizon);
      cx += node->getX();
      cy += node->getY();
    }
    avgTW /= n;
    cx /= n;
    cy /= n;

    double totalC = 0;
    int activeR = 0;
    for (const auto &ro : solution.getRoutes())
      if (!ro.getCustomers().empty()) {
        totalC += ro.getCustomers().size();
        ++activeR;
      }
    double avgSz = activeR > 0 ? totalC / activeR : n;
    double sizeFactor = (avgSz - n) / std::max(1.0, avgSz);
    double demFactor = 1.0 - (r.getTotalDemand() / vehCap);

    // Spatial proximity: avg distance từ victim centroid đến centroid routes
    // khác Smaller = closer to others = easier to redistribute
    double avgDist = 0.0;
    int cnt = 0;
    for (int ri = 0; ri < (int)solution.getRoutes().size(); ++ri) {
      if (ri == routeIdx)
        continue;
      const auto &other = solution.getRoutes()[ri].getCustomers();
      if (other.empty())
        continue;
      double ox = 0.0, oy = 0.0;
      for (int c : other) {
        auto nd = instance->getNodeById(c);
        ox += nd->getX();
        oy += nd->getY();
      }
      ox /= other.size();
      oy /= other.size();
      double d = std::hypot(cx - ox, cy - oy);
      avgDist += d;
      ++cnt;
    }
    double spatialFactor = cnt > 0 ? 1.0 / (1.0 + avgDist / cnt) : 0.0;

    return 0.30 * avgTW + 0.25 * sizeFactor + 0.20 * demFactor +
           0.25 * spatialFactor;
  };

  // Rank tất cả routes theo victim score
  int nRoutes = (int)solution.getRoutes().size();
  std::vector<int> victimRanking;
  victimRanking.reserve(nRoutes);
  for (int i = 0; i < nRoutes; ++i)
    if (!solution.getRoutes()[i].getCustomers().empty())
      victimRanking.push_back(i);

  std::sort(victimRanking.begin(), victimRanking.end(),
            [&](int a, int b) { return victimScore(a) > victimScore(b); });

  // ── Helper: approximate insertion cost (direct + station-aware) ───────────
  // Trả về {feasible, cost} cho insertion của cId tại pos trong route r.
  // Nếu direct feasible → cost = deltaDistance.
  // Nếu direct infeasible về energy → thử top-1 station, cost = detour + delta.
  // Nếu TW infeasible → {false, INF}.
  struct InsertCost {
    bool feasible;
    double cost;
    int sid;
    bool sBef;
  };

  auto approxInsertCost = [&](int cId, const Route &r,
                              size_t pos) -> InsertCost {
    // Direct
    if (r.canPossiblyInsert(cId, pos)) {
      auto res = r.checkInsertionCost(cId, pos);
      if (res.isFeasible)
        return {true, res.deltaDistance, -1, true};
    }

    // Station-assisted: thử top-2 stations cho edge (prev→next)
    const auto &nodes = r.getNodes();
    if (pos == 0 || pos >= nodes.size())
      return {false, 1e18, -1, true};

    int prevId = nodes[pos - 1], nextId = nodes[pos];
    double direct = instance->getDistance(prevId, nextId);

    struct SC {
      int id;
      double det;
    };
    std::vector<SC> sc;
    sc.reserve(stationIds.size());
    for (int sid : stationIds) {
      double det = instance->getDistance(prevId, sid) +
                   instance->getDistance(sid, nextId) - direct;
      if (det < direct * 3.0 + 50.0)
        sc.push_back({sid, det});
    }
    if (sc.empty())
      return {false, 1e18, -1, true};

    int K = std::min(2, (int)sc.size());
    std::partial_sort(sc.begin(), sc.begin() + K, sc.end(),
                      [](const SC &a, const SC &b) { return a.det < b.det; });

    for (int k = 0; k < K; ++k) {
      for (bool bef : {true, false}) {
        Route copy = r;
        copy.addNode(cId, pos);
        copy.addNode(sc[k].id, bef ? (int)pos : (int)pos + 1);
        copy.evaluate();
        if (copy.isFeasible()) {
          double cost = copy.getTotalDistance() - r.getTotalDistance();
          return {true, cost + 5.0, sc[k].id, bef}; // +5 station penalty
        }
      }
    }
    return {false, 1e18, -1, true};
  };

  // ── Main loop: thử từng victim theo ranking ───────────────────────────────
  // Với mỗi victim: pre-check → attempt loop → nếu success return true.
  // maxAttemptsPerVictim: dùng maxAttempts parameter làm budget per victim.
  // Caller nên pass 80-150.

  // ── Main loop: thử từng victim theo ranking ───────────────────────────────
  // Hướng 2: Bỏ pre-check hoàn toàn — để attempt loop tự quyết định.
  // Lý do: pre-check group simulation có false negative cao (block oan).
  //        pre-check independent vẫn block nếu reconstruct quá conservative.
  //        → Tốt hơn là dùng budget attempts để thực sự thử.
  //
  // Thay vào đó, attempt loop được nâng cấp với 3 strategies xen kẽ:
  //   Strategy 0 (attempts 0..N/3):   regret-2 TW-tight first (hiện tại)
  //   Strategy 1 (attempts N/3..2N/3): cluster-first — group customers theo
  //       route gần nhất, assign cả cluster cùng lúc → tránh greedy deadlock
  //   Strategy 2 (attempts 2N/3..N):  random shuffle hoàn toàn — diversify
  //
  // Early-exit per victim: nếu 10 attempts đầu đều fail tại cùng customer
  // → victim này thực sự khó → skip sang victim tiếp theo sớm.

  const int MAX_VICTIMS_TO_TRY = std::min(6, (int)victimRanking.size());

  for (int vi = 0; vi < MAX_VICTIMS_TO_TRY; ++vi) {
    int victimIdx = victimRanking[vi];

    std::vector<int> ejected;
    for (int nid : solution.getRoutes()[victimIdx].getNodes())
      if (nodeTypeById_[nid] == NodeType::CUSTOMER)
        ejected.push_back(nid);
    if (ejected.empty())
      continue;

    std::cout << "[FRV] Victim " << vi << " (route " << victimIdx << ", "
              << ejected.size() << " custs): trying " << maxAttempts
              << " attempts (no pre-check)\n";

    // ── Attempt loop ──────────────────────────────────────────────────────
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {

      Solution candidate = solution;
      candidate.removeRoute(victimIdx);
      auto &cRoutes = candidate.getRoutes();
      int nR = (int)cRoutes.size();

      // ── Strategy selection ────────────────────────────────────────────
      int third = std::max(1, maxAttempts / 3);
      int strategy = (attempt < third) ? 0 : (attempt < 2 * third) ? 1 : 2;

      std::vector<int> toInsert = ejected;

      if (strategy == 0) {
        // TW-tight first (deterministic untuk attempt 0, shuffle untuk sisanya)
        if (attempt > 0)
          std::shuffle(toInsert.begin(), toInsert.end(), rng);
        std::stable_sort(toInsert.begin(), toInsert.end(), [&](int a, int b) {
          auto na = instance->getNodeById(a);
          auto nb = instance->getNodeById(b);
          return (na->getDueDate() - na->getReadyTime()) <
                 (nb->getDueDate() - nb->getReadyTime());
        });
      } else if (strategy == 1) {
        // Cluster-first: assign customers ke route terdekat dulu
        // [OPTIMIZATION] Pre-compute route centroids ONCE, avoid O(N²)
        // recomputation in sort

        std::vector<std::pair<double, double>> centroids(nR);
        std::vector<bool> hasCustomers(nR, false);

        // Pre-compute centroids O(nR × avgRouteSize)
        for (int ri = 0; ri < nR; ++ri) {
          const auto &custs = cRoutes[ri].getCustomers();
          if (custs.empty())
            continue;

          double cx = 0.0, cy = 0.0;
          for (int c : custs) {
            auto nd = instance->getNodeById(c);
            cx += nd->getX();
            cy += nd->getY();
          }

          int size = (int)custs.size();
          centroids[ri] = {cx / size, cy / size};
          hasCustomers[ri] = true;
        }

        // Now sort with O(1) centroid lookup — NOT O(nR × avgRouteSize × log N)
        // per comparison!
        std::shuffle(toInsert.begin(), toInsert.end(), rng);
        std::sort(toInsert.begin(), toInsert.end(), [&](int a, int b) {
          double minDA = 1e18, minDB = 1e18;
          auto ndA = instance->getNodeById(a);
          auto ndB = instance->getNodeById(b);

          for (int ri = 0; ri < nR; ++ri) {
            if (!hasCustomers[ri])
              continue; // Skip empty routes

            const auto &[cx, cy] = centroids[ri]; // O(1) lookup
            double dA = std::hypot(ndA->getX() - cx, ndA->getY() - cy);
            double dB = std::hypot(ndB->getX() - cx, ndB->getY() - cy);
            minDA = std::min(minDA, dA);
            minDB = std::min(minDB, dB);
          }

          return minDA < minDB;
        });
      } else {
        // Full random
        std::shuffle(toInsert.begin(), toInsert.end(), rng);
      }

      // ── Reconstruction-based regret-2 assignment ──────────────────────
      // [FIX] Cache rebuilt Route từ lần tính cost → apply bằng std::move,
      // không gọi reconstruct lần 2 (non-deterministic → false fail).
      // Dùng std::optional<Route> vì Route không có default constructor.
      struct Cand {
        int custId = -1, ri = -1;
        double cost = 1e18;
        std::optional<Route> rebuilt; // nullopt = infeasible / chưa set
      };

      auto reconstCost =
          [&](int cId, int ri) -> std::pair<double, std::optional<Route>> {
        const Route &r = cRoutes[ri];
        if (r.getTotalDemand() + demandById_[cId] > vehCap)
          return {1e18, std::nullopt};
        double oldDist = r.getTotalDistance();
        Route rCopy = r;
        if (!reconstructRouteWithTwoStations(rCopy, {cId}, 2))
          return {1e18, std::nullopt};
        return {rCopy.getTotalDistance() - oldDist, std::move(rCopy)};
      };

      auto findBestTwo = [&](int cId) -> std::pair<Cand, Cand> {
        Cand r1, r2;
        r1.custId = r2.custId = cId;
        for (int ri = 0; ri < nR; ++ri) {
          auto [cost, built] = reconstCost(cId, ri);
          if (cost < r1.cost) {
            r2 = std::move(r1);
            r1 = {cId, ri, cost, std::move(built)};
          } else if (cost < r2.cost) {
            r2 = {cId, ri, cost, std::nullopt}; // r2 chỉ cần cost
          }
        }
        return {std::move(r1), std::move(r2)};
      };

      std::vector<int> unplaced = toInsert;
      bool allInserted = true;

      while (!unplaced.empty()) {
        int bestIdx = -1;
        double maxRegret = -1e18;
        Cand bestCand;

        for (int idx = 0; idx < (int)unplaced.size(); ++idx) {
          auto [r1, r2] = findBestTwo(unplaced[idx]);
          if (r1.ri == -1)
            continue;
          double regret = (r2.ri == -1) ? 1e15 : (r2.cost - r1.cost);
          if (regret > maxRegret) {
            maxRegret = regret;
            bestIdx = idx;
            bestCand = std::move(r1);
          }
        }

        if (bestIdx == -1) {
          allInserted = false;
          break;
        }

        // Apply: dùng Route đã rebuild sẵn — KHÔNG reconstruct lần 2
        // rebuilt guaranteed non-null vì ri != -1 chỉ set khi reconstruct thành
        // công
        cRoutes[bestCand.ri] = std::move(*bestCand.rebuilt);
        unplaced[bestIdx] = unplaced.back();
        unplaced.pop_back();
      }

      if (!allInserted)
        continue;

      // ── Final feasibility ─────────────────────────────────────────────
      candidate.evaluateRoutes();
      if (!candidate.isFeasible())
        continue;

      solution = candidate;
      std::cout << "[FRV] SUCCESS attempt=" << attempt
                << " strategy=" << strategy << " victim=" << victimIdx << " | "
                << currentVeh << " -> " << solution.getNumRoutes()
                << " veh | dist=" << std::fixed << std::setprecision(2)
                << solution.getTotalDistance() << "\n";
      return true;
    }

    std::cout << "[FRV] Victim " << vi << " exhausted " << maxAttempts
              << " attempts. Trying next victim.\n";
  }

  return false;
}

// ============================================================================
// searchStationInsertion — Thêm station mới vào route đang thiếu pin
// ============================================================================
// Dùng getEnergySlack() từ Route để tìm "bottleneck edge" — cạnh có energy
// slack thấp nhất. Thử chèn top-2 stations (by detour) vào cạnh đó.
// First-improvement: apply ngay khi tìm được route feasible + distance giảm
// hoặc route hiện tại infeasible (repair move).
// ============================================================================
bool LocalSearch::searchStationInsertion(Solution &solution) {
  auto &routes = solution.getRoutes();
  const double EPSILON = 1e-9;

  for (int r = 0; r < (int)routes.size(); ++r) {
    const auto &nodes = routes[r].getNodes();
    int n = (int)nodes.size();
    if (n < 3)
      continue;

    // Lấy energy slack tại mỗi node
    auto slack = routes[r].getEnergySlack();
    if ((int)slack.size() != n)
      continue;

    // Tìm cạnh (i → i+1) có energy slack thấp nhất
    // (slack[i] thấp = sau node i battery gần cạn)
    int bottleneckEdge = -1;
    double minSlack = 1e18;
    for (int i = 0; i < n - 1; ++i) {
      // Bỏ qua nếu node i+1 đã là station (không cần thêm)
      if (nodeTypeById_[nodes[i + 1]] == NodeType::STATION)
        continue;
      if (slack[i] < minSlack) {
        minSlack = slack[i];
        bottleneckEdge = i;
      }
    }

    // Chỉ thử nếu slack thực sự thấp (< 20% capacity) hoặc route infeasible
    double cap = routes[r].getVehicle()->getBatteryCapacity();
    bool routeInfeasible = !routes[r].isFeasible();
    if (!routeInfeasible && minSlack > 0.20 * cap)
      continue;
    if (bottleneckEdge < 0)
      continue;

    int prevNode = nodes[bottleneckEdge];
    int nextNode = nodes[bottleneckEdge + 1];
    double directDist = instance->getDistance(prevNode, nextNode);

    // Top-2 stations by detour
    struct SC {
      int id;
      double detour;
    };
    std::vector<SC> cands;
    cands.reserve(stationIds.size());
    for (int sid : stationIds) {
      double detour = instance->getDistance(prevNode, sid) +
                      instance->getDistance(sid, nextNode) - directDist;
      cands.push_back({sid, detour});
    }
    int K = std::min(2, (int)cands.size());
    std::partial_sort(
        cands.begin(), cands.begin() + K, cands.end(),
        [](const SC &a, const SC &b) { return a.detour < b.detour; });

    double oldDist = routes[r].getTotalDistance();

    for (int k = 0; k < K; ++k) {
      Route testRoute = routes[r];
      // Chèn sau bottleneckEdge (vị trí bottleneckEdge + 1 trong Route)
      testRoute.addNode(cands[k].id, bottleneckEdge + 1);
      testRoute.evaluate();

      if (testRoute.isFeasible()) {
        // Apply nếu repair move, hoặc distance không tệ hơn quá 5%
        double newDist = testRoute.getTotalDistance();
        if (routeInfeasible || newDist < oldDist + 0.05 * oldDist + EPSILON) {
          routes[r] = testRoute;
          return true;
        }
      }
    }
  }
  return false;
}

// ============================================================================
// searchOrOptReversed — Or-Opt với segment đảo ngược (inter + intra)
// ============================================================================
// Segment 2,3. Thử insert segment theo chiều reversed tại KNN positions.
// Inter: dùng ctx.neighborLists để chọn route đích (nhất quán với searchOrOpt).
// Intra: cùng route, dùng knnCache_[revHead] để prune positions.
// Không dùng MoveDescriptor/evaluateMove vì reversed segment cần build thủ
// công.
// ============================================================================
bool LocalSearch::searchOrOptReversed(Solution &solution,
                                      const LocalSearchWeights &weights,
                                      SearchContext &ctx) {
  if (!ctx.isValid)
    return false;

  auto &routes = solution.getRoutes();
  int numRoutes = (int)routes.size();
  if ((int)ctx.neighborLists.size() != numRoutes)
    return false;

  static const int SEGMENT_LENGTHS[] = {2, 3};
  const double EPSILON = 1e-9;

  for (int segLen : SEGMENT_LENGTHS) {
    for (int r1 = 0; r1 < numRoutes; ++r1) {
      const auto &nodes1 = routes[r1].getNodes();
      int n1 = (int)nodes1.size();
      if (n1 < 2 + segLen)
        continue;

      for (int i = 1; i <= n1 - 1 - segLen; ++i) {
        bool allCust = true;
        for (int k = 0; k < segLen && allCust; ++k)
          if (nodeTypeById_[nodes1[i + k]] != NodeType::CUSTOMER)
            allCust = false;
        if (!allCust)
          continue;

        int segHead = nodes1[i];
        int segTail = nodes1[i + segLen - 1];
        int revHead = segTail;
        int revTail = segHead;
        int prev1 = nodes1[i - 1];
        int next1 = nodes1[i + segLen];

        double removalSaving = instance->getDistance(prev1, segHead) +
                               instance->getDistance(segTail, next1) -
                               instance->getDistance(prev1, next1);

        for (int r2 : ctx.neighborLists[r1]) {
          // [FIX-1] Snapshot nodes2 TRƯỚC khi có bất kỳ modification nào.
          // Khi r1==r2, nodes2 là alias của nodes1 — phải copy ra.
          const std::vector<int> nodes2 = routes[r2].getNodes();
          int n2 = (int)nodes2.size();

          auto candidatePositions =
              findBestInsertionPositions_KNN(routes[r2], revHead, 3);

          for (size_t ins : candidatePositions) {
            int j = (int)ins;

            if (r1 == r2 && j >= i && j <= i + segLen)
              continue;
            if (j < 1 || j >= n2)
              continue;

            int prev2 = nodes2[j - 1];
            int next2 = nodes2[j];

            double insertCostRev = instance->getDistance(prev2, revHead) +
                                   instance->getDistance(revTail, next2) -
                                   instance->getDistance(prev2, next2);

            if (insertCostRev - removalSaving >= -EPSILON)
              continue;

            if (!twNext_[prev2][revHead] || !twNext_[revTail][next2])
              continue;

            // [FIX-2] Build r1c SAU khi đã pass tất cả cheap checks —
            // tránh copy route O(N) cho mỗi candidate position thất bại
            Route r1c = routes[r1];
            for (int k = segLen - 1; k >= 0; --k)
              r1c.removeNode(i + k);

            if (r1 == r2) {
              int ins2 = j;
              if (i < j)
                ins2 -= segLen;
              for (int k = 0; k < segLen; ++k)
                r1c.addNode(nodes1[i + (segLen - 1 - k)], ins2 + k);
              r1c.evaluate();
              if (!r1c.isFeasible())
                continue;

              double delta =
                  r1c.getTotalDistance() - routes[r1].getTotalDistance();
              if (delta < -EPSILON) {
                routes[r1] = std::move(r1c);
                ctx.markDirty(r1);
                solution.markDirty(); // [FIX-3]
                return true;
              }
            } else {
              Route r2c = routes[r2];
              for (int k = 0; k < segLen; ++k)
                r2c.addNode(nodes1[i + (segLen - 1 - k)], j + k);
              r1c.evaluate();
              r2c.evaluate();
              if (!r1c.isFeasible() || !r2c.isFeasible())
                continue;

              double delta = (r1c.getTotalDistance() + r2c.getTotalDistance()) -
                             (routes[r1].getTotalDistance() +
                              routes[r2].getTotalDistance());
              if (delta < -EPSILON) {
                routes[r1] = std::move(r1c);
                routes[r2] = std::move(r2c);
                ctx.markDirty(r1, r2);
                solution.markDirty(); // [FIX-3]
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