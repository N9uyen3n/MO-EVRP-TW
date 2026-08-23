#include "../../../../include/alns/operators/destroy/InefficientRouteRemoval.h"
#include "../../../../include/core/Customer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <unordered_map>


InefficientRouteRemoval::InefficientRouteRemoval(
    std::shared_ptr<Instance> instance)
    : instance(instance) {}

std::string InefficientRouteRemoval::getName() const {
  return "Inefficient Route Removal";
}

double InefficientRouteRemoval::calculateRouteScore(
    const Route &route, double avgCustomersPerRoute, double avgDistPerRoute,
    double avgWaitPerRoute) const {

  int numCustomers = route.getCustomers().size();
  if (numCustomers == 0)
    return -1e9;

  double score = 0.0;

  // 1. RELATIVE SIZE: route nhỏ hơn average → dễ eliminate hơn
  double relativeSize = (avgCustomersPerRoute - numCustomers) /
                        std::max(1.0, avgCustomersPerRoute);
  score += 45.0 * relativeSize;

  // 2. RELATIVE EFFICIENCY: distance/customer so với average
  double distPerCust = route.getTotalDistance() / numCustomers;
  double relativeEff =
      (distPerCust - avgDistPerRoute) / std::max(1.0, avgDistPerRoute);
  score += 7.0 * relativeEff;

  // 3. WAIT TIME: vẫn giữ nhưng relative
  double avgWait = route.getTotalWaitTime() / numCustomers;
  double relativeWait =
      (avgWait - avgWaitPerRoute) / std::max(1.0, avgWaitPerRoute);
  score += 10.0 * relativeWait;

  // 4. BOTTLENECK: soft penalty
  auto bottlenecks = route.getBottleneckNodes(0.15);
  double bottleneckRatio = (double)bottlenecks.size() / numCustomers;
  score -= std::min(15.0, 25.0 * bottleneckRatio);

  // 5. STATION OVERHEAD
  int stationCount = 0;
  for (int nodeId : route.getNodes())
    if (instance->getNodeById(nodeId)->getType() == NodeType::STATION)
      stationCount++;
  score += 5.0 * ((double)stationCount / numCustomers);

  // 6. RC2-AWARE: MERGABILITY (TW Coverage)
  double twCoverage = 0.0;
  double maxTW = instance->getNodeById(0)->getDueDate();
  for (int custId : route.getCustomers()) {
    auto cust = instance->getNodeById(custId);
    double tw = cust->getDueDate() - cust->getReadyTime();
    twCoverage += tw / std::max(1.0, maxTW);
  }
  double avgTWCoverage = twCoverage / numCustomers;
  score += 20.0 * avgTWCoverage;

  // 7. ENERGY PORTABILITY
  double avgEnergyDemand = 0.0;
  double energyRate = instance->getVehicleEnergyRate();
  for (int custId : route.getCustomers()) {
    double e =
        (instance->getDistance(0, custId) + instance->getDistance(custId, 0)) *
        energyRate;
    avgEnergyDemand += e;
  }
  avgEnergyDemand /= numCustomers;

  double batteryCap = instance->getVehicleBattery();
  double portability = 1.0 - (avgEnergyDemand / std::max(1.0, batteryCap));
  portability = std::max(0.0, portability);
  score += 10.0 * portability;

  return score;
}

std::vector<int> InefficientRouteRemoval::execute(Solution &solution,
                                                  int nodesToRemove,
                                                  std::mt19937 &rng) {
  std::vector<int> removedCustomers;
  auto &routes = solution.getRoutes();

  if (routes.empty())
    return removedCustomers;

  // --- Calculate Context Statistics ---
  int activeRoutes = 0;
  int totalCustomers = 0;
  double totalDistPerCust = 0.0;
  double totalWaitPerCust = 0.0;

  for (const auto &r : routes) {
    if (!r.getCustomers().empty()) {
      activeRoutes++;
      int n = r.getCustomers().size();
      totalCustomers += n;
      totalDistPerCust += r.getTotalDistance();
      totalWaitPerCust += r.getTotalWaitTime();
    }
  }

  if (activeRoutes == 0)
    return removedCustomers;

  double avgCust = (double)totalCustomers / activeRoutes;
  double avgDist = (totalCustomers > 0) ? totalDistPerCust / totalCustomers : 1.0;
  double avgWait = (totalCustomers > 0) ? totalWaitPerCust / totalCustomers : 1.0;

  // --- Calculate Scores ---
  std::vector<std::pair<int, double>> routeScores;
  for (size_t i = 0; i < routes.size(); ++i) {
    if (routes[i].getCustomers().empty())
      continue;
    double score = calculateRouteScore(routes[i], avgCust, avgDist, avgWait);
    routeScores.push_back({static_cast<int>(i), score});
  }

  if (routeScores.empty())
    return removedCustomers;

  // --- Determine Target Removal Count ---
  int avgPerRoute = std::max(1, totalCustomers / activeRoutes);
  int targetToRemove = std::max(1, nodesToRemove / avgPerRoute);
  targetToRemove = std::min(targetToRemove, activeRoutes);

  // --- Tournament Selection ---
  std::vector<int> routesToRemove;
  std::vector<bool> selected(routes.size(), false);

  const int TOURNAMENT_SIZE = 3;
  std::uniform_int_distribution<int> randIdx(0, routeScores.size() - 1);

  while (static_cast<int>(routesToRemove.size()) < targetToRemove &&
         static_cast<int>(routesToRemove.size()) <
             static_cast<int>(routeScores.size())) {

    int bestTournamentIdx = -1;
    double bestTournamentScore = -1e18;

    std::vector<int> sampledIndices;
    int maxTries = TOURNAMENT_SIZE * 4;
    while ((int)sampledIndices.size() < TOURNAMENT_SIZE && maxTries-- > 0) {
      int candidateIdx = randIdx(rng);
      if (std::find(sampledIndices.begin(), sampledIndices.end(), candidateIdx)
          == sampledIndices.end()) {
        sampledIndices.push_back(candidateIdx);
      }
    }

    for (int candidateIdx : sampledIndices) {
      int routeIdx = routeScores[candidateIdx].first;
      double score = routeScores[candidateIdx].second;
      if (!selected[routeIdx] && score > bestTournamentScore) {
        bestTournamentScore = score;
        bestTournamentIdx   = routeIdx;
      }
    }

    if (bestTournamentIdx != -1) {
      selected[bestTournamentIdx] = true;
      routesToRemove.push_back(bestTournamentIdx);
    } else {
      bool found = false;
      for (const auto &p : routeScores) {
        if (!selected[p.first]) {
          selected[p.first] = true;
          routesToRemove.push_back(p.first);
          found = true;
          break;
        }
      }
      if (!found)
        break;
    }
  }

  // =========================================================================
  // [NEW] Neighbor Steal: sau khi chọn xong các route bị xóa, lấy thêm
  // một số customers từ các route lân cận để mở rộng không gian repair.
  //
  // Lý do: khi một route "inefficient" bị xóa, repair operator chỉ nhận
  // được customers của chính route đó. Nếu optimal solution yêu cầu
  // redistribution giữa route bị xóa và route lân cận (ví dụ C33 cần
  // đi cùng C19/C23/C25 từ route khác), repair không có đủ nguyên liệu.
  //
  // Cách hoạt động:
  //   - Với mỗi customer trong route bị xóa, tìm customers ở route KHÁC
  //     có min-distance ≤ STEAL_DIST_THRESHOLD.
  //   - Steal tối đa stealBudget customers (ưu tiên gần nhất).
  //   - Stolen customers được remove khỏi route gốc và thêm vào
  //     removedCustomers để repair operator xử lý cùng.
  // =========================================================================
  // Số customers cần steal: ~30-40% of nodesToRemove, tối thiểu 1
  // const int stealBudget = std::max(1, static_cast<int>(std::round(nodesToRemove * 0.35)));



  // Collect tất cả customers bị destroy (để tính min-dist)
  std::vector<int> destroyedCusts;
  for (int routeIdx : routesToRemove) {
    for (int cid : routes[routeIdx].getCustomers())
      destroyedCusts.push_back(cid);
  }
  const int stealBudget = static_cast<int>(destroyedCusts.size() + 1);

  // Build danh sách ứng viên steal từ các route KHÔNG bị destroy
  struct StealCandidate {
    int routeIdx;
    int custId;
    double minDistToDestroyed; // khoảng cách gần nhất tới bất kỳ destroyed cust
  };
  std::vector<StealCandidate> candidates;

  std::vector<bool> isDestroyedRoute(routes.size(), false);
  for (int ridx : routesToRemove)
    isDestroyedRoute[ridx] = true;

  for (int r = 0; r < static_cast<int>(routes.size()); ++r) {
    if (isDestroyedRoute[r]) continue;
    for (int cid : routes[r].getCustomers()) {
      double minD = std::numeric_limits<double>::max();
      for (int dc : destroyedCusts) {
        double dist = instance->getDistance(cid, dc);
        if (dist < minD) minD = dist;
      }
      candidates.push_back({r, cid, minD});
    }
  }

  // Sort theo khoảng cách tăng dần → gần nhất được steal trước
  std::sort(candidates.begin(), candidates.end(),
            [](const StealCandidate &a, const StealCandidate &b) {
              return a.minDistToDestroyed < b.minDistToDestroyed;
            });

  // Steal top-stealBudget candidates, tracking per-route removal positions
  // Dùng map route → sorted positions (descending) để remove an toàn
  std::unordered_map<int, std::vector<int>> stealPositions; // routeIdx → node positions
  int stolen = 0;

  for (const auto &cand : candidates) {
    if (stolen >= stealBudget) break;

    // Tìm vị trí của customer này trong route
    const auto &nodes = routes[cand.routeIdx].getNodes();
    for (int pos = 0; pos < static_cast<int>(nodes.size()); ++pos) {
      if (nodes[pos] == cand.custId) {
        stealPositions[cand.routeIdx].push_back(pos);
        removedCustomers.push_back(cand.custId);
        stolen++;
        break;
      }
    }
  }

  // Apply steal removals: xóa theo position descending để tránh index shift
  for (auto &[routeIdx, positions] : stealPositions) {
    std::sort(positions.begin(), positions.end(), std::greater<int>());
    for (int pos : positions)
      routes[routeIdx].removeNode(pos);
    routes[routeIdx].evaluate();
  }

  // Remove chosen routes descending to avoid index shift
  std::sort(routesToRemove.begin(), routesToRemove.end(), std::greater<int>());

  for (int routeIdx : routesToRemove) {
    const auto &nodes = routes[routeIdx].getNodes();
    for (int nodeId : nodes) {
      if (instance->getNodeById(nodeId)->getType() == NodeType::CUSTOMER) {
        removedCustomers.push_back(nodeId);
      }
    }
    solution.removeRoute(routeIdx);
  }

  return removedCustomers;
}